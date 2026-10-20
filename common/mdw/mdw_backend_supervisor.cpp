/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <mdw/mdw_backend_supervisor.h>
#include <mdw/mdw_nng_transport.h>

#include <kiid.h>

#include <wx/filename.h>
#include <wx/log.h>
#include <wx/process.h>
#include <wx/utils.h>

#include <chrono>
#include <cstdint>
#include <thread>

#if defined( __unix__ ) || defined( __APPLE__ )
#include <cerrno>
#include <sys/wait.h>
#endif

static const wxChar* const MDW_TRACE = wxT( "KICAD_MDW" );


/// The restart policy counts attempts on a monotonic clock, so wall-clock jumps
/// cannot make a crash loop look like an idle backend.
static std::int64_t monotonicNowMs()
{
    using namespace std::chrono;

    return duration_cast<milliseconds>( steady_clock::now().time_since_epoch() ).count();
}


MDW_BACKEND_SUPERVISOR::MDW_BACKEND_SUPERVISOR( const MDW_BACKEND_MANIFEST& aManifest ) :
        m_manifest( aManifest )
{
}


MDW_BACKEND_SUPERVISOR::~MDW_BACKEND_SUPERVISOR()
{
    Shutdown();
}


bool MDW_BACKEND_SUPERVISOR::spawnProcess()
{
    wxString runtimeRoot;

    if( !wxGetEnv( wxS( "XDG_RUNTIME_DIR" ), &runtimeRoot ) || runtimeRoot.IsEmpty() )
        runtimeRoot = wxFileName::GetTempDir();

    // A private 0700 directory per spawn holds the owner-only ipc sockets.
    KIID     spawnId;
    wxString token = KIID().AsString();

    wxFileName dir( runtimeRoot, wxEmptyString );
    dir.AppendDir( wxS( "kicad" ) );
    dir.AppendDir( wxS( "mdw" ) );
    dir.AppendDir( spawnId.AsString() );

    if( !dir.Mkdir( 0700, wxPATH_MKDIR_FULL ) && !dir.DirExists() )
    {
        wxLogTrace( MDW_TRACE, wxS( "MDW supervisor could not create runtime dir %s" ),
                    dir.GetPath() );
        return false;
    }

    m_runtimeDir = dir.GetPath();
    m_channelToken = token;
    m_controlUrl = wxS( "ipc://" ) + m_runtimeDir + wxFileName::GetPathSeparator() + wxS( "control.sock" );

    wxString eventsUrl =
            wxS( "ipc://" ) + m_runtimeDir + wxFileName::GetPathSeparator() + wxS( "events.sock" );

    // Every path argument is quoted; wxExecute re-splits the command on
    // whitespace, so an unquoted runtime dir containing a space would truncate.
    wxString command =
            wxString::Format( wxS( "\"%s\" --kicad-mdw --control \"%s\" --events \"%s\" --parent-pid %ld" ),
                              m_manifest.executable_path, m_controlUrl, eventsUrl,
                              static_cast<long>( wxGetProcessId() ) );

    // The channel token travels in the environment, never on the command line.
    wxExecuteEnv env;
    wxGetEnvMap( &env.env );
    env.env[wxS( "KICAD_MDW_TOKEN" )] = m_channelToken;

    m_pid = wxExecute( command, wxEXEC_ASYNC, nullptr, &env );

    if( m_pid <= 0 )
    {
        wxLogTrace( MDW_TRACE, wxS( "MDW supervisor failed to launch %s" ),
                    m_manifest.executable_path );
        return false;
    }

    return true;
}


bool MDW_BACKEND_SUPERVISOR::Launch( const wxString& aKicadVersion, int aHandshakeTimeoutMs )
{
    m_shutdownRequested = false;

    if( !spawnProcess() )
        return false;

    wxString eventsUrl =
            wxS( "ipc://" ) + m_runtimeDir + wxFileName::GetPathSeparator() + wxS( "events.sock" );

    auto transport = std::make_unique<MDW_NNG_TRANSPORT>( std::string( m_controlUrl.utf8_string() ) );

    m_backend = std::make_unique<MDW_BACKEND>( std::move( transport ), m_channelToken );

    if( !m_backend->Start() )
    {
        terminateProcess();
        m_backend.reset();
        return false;
    }

    if( !m_backend->Handshake( aKicadVersion, eventsUrl, aHandshakeTimeoutMs ) )
    {
        wxLogTrace( MDW_TRACE, wxS( "MDW handshake with %s failed" ), m_manifest.backend_id );
        terminateProcess();
        m_backend.reset();
        return false;
    }

    m_online.store( true );

    return true;
}


bool MDW_BACKEND_SUPERVISOR::Ping( int aTimeoutMs )
{
    if( !m_backend )
        return false;

    bool alive = m_backend->Ping( aTimeoutMs );
    m_online.store( alive );

    return alive;
}


void MDW_BACKEND_SUPERVISOR::terminateProcess()
{
    if( m_pid <= 0 )
        return;

    if( wxProcess::Exists( m_pid ) )
    {
        wxKill( m_pid, wxSIGTERM );

        for( int i = 0; i < 30 && wxProcess::Exists( m_pid ); ++i )
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

        if( wxProcess::Exists( m_pid ) )
            wxKill( m_pid, wxSIGKILL );
    }

#if defined( __unix__ ) || defined( __APPLE__ )
    // Reap the child so it does not linger as a zombie; we spawned it without a
    // wxProcess, so nothing else waits on it.
    int status = 0;
    waitpid( static_cast<pid_t>( m_pid ), &status, 0 );
#endif

    m_pid = 0;
}


void MDW_BACKEND_SUPERVISOR::Shutdown()
{
    m_shutdownRequested = true;
    m_online.store( false );

    if( m_backend )
    {
        m_backend->Bye( 2000 );
        m_backend->Stop();
    }

    terminateProcess();
    m_backend.reset();
}


bool MDW_BACKEND_SUPERVISOR::reapIfExited()
{
    if( m_pid <= 0 )
        return false;

#if defined( __unix__ ) || defined( __APPLE__ )
    int   status = 0;
    pid_t reaped = waitpid( static_cast<pid_t>( m_pid ), &status, WNOHANG );

    // Zero says the child is still running and an interrupted wait says nothing
    // either way, so only a collected child, or the ECHILD of one something else
    // collected first, counts as gone.
    if( reaped == 0 || ( reaped < 0 && errno != ECHILD ) )
        return false;
#else
    if( wxProcess::Exists( m_pid ) )
        return false;
#endif

    m_pid = 0;

    return true;
}


bool MDW_BACKEND_SUPERVISOR::CrashDetected()
{
    if( m_shutdownRequested || !reapIfExited() )
        return false;

    m_online.store( false );

    wxLogTrace( MDW_TRACE, wxS( "MDW backend %s exited without being asked to" ),
                m_manifest.backend_id );

    return true;
}


bool MDW_BACKEND_SUPERVISOR::Restart( const wxString& aKicadVersion, int aHandshakeTimeoutMs )
{
    if( m_gaveUp || m_shutdownRequested )
        return false;

    int delay = m_restartPolicy.NextDelayMs( monotonicNowMs() );

    if( delay < 0 )
    {
        m_gaveUp = true;
        m_online.store( false );

        wxLogTrace( MDW_TRACE, wxS( "MDW backend %s restarted too often; leaving it offline" ),
                    m_manifest.backend_id );

        return false;
    }

    if( delay > 0 )
        std::this_thread::sleep_for( std::chrono::milliseconds( delay ) );

    // Whatever survives the dead process goes before the replacement starts. No
    // Bye is sent; there is nothing left to answer it.
    if( m_backend )
    {
        m_backend->Stop();
        m_backend.reset();
    }

    terminateProcess();

    // Launch() mints a new runtime directory, channel token and MDW_BACKEND, so
    // on success the owning session must discard and rebuild everything it built
    // from the old ones. MDW_WORKSPACE_SERVICE captured the previous MDW_BACKEND*,
    // and the event ingress and client PUB server are bound to sockets inside the
    // previous runtime directory that the new process knows nothing about.
    return Launch( aKicadVersion, aHandshakeTimeoutMs );
}
