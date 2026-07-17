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
#include <thread>

static const wxChar* const MDW_TRACE = wxT( "KICAD_MDW" );


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

    m_pid = 0;
}


void MDW_BACKEND_SUPERVISOR::Shutdown()
{
    m_online.store( false );

    if( m_backend )
    {
        m_backend->Bye( 2000 );
        m_backend->Stop();
    }

    terminateProcess();
    m_backend.reset();
}
