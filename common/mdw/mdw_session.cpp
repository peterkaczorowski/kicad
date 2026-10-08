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

#include <mdw/mdw_session.h>
#include <mdw/mdw_credential_store.h>
#include <mdw/mdw_event_ingress.h>
#include <mdw/mdw_event_pub_bridge.h>

#include <kinng.h>

#include <api/mdw/backend_base.pb.h>

#include <wx/filename.h>


MDW_SESSION::MDW_SESSION( const wxString& aProjectPath, const MDW_PROJECT_BINDING& aBinding,
                          std::unique_ptr<MDW_BACKEND_SUPERVISOR> aSupervisor ) :
        m_projectPath( aProjectPath ),
        m_binding( aBinding ),
        m_supervisor( std::move( aSupervisor ) )
{
}


bool MDW_SESSION::Authenticate( MDW_CREDENTIAL_STORE& aStore, int aTimeoutMs )
{
    MDW_BACKEND* backend = Backend();

    if( !backend )
        return false;

    // A backend that needs no credentials answers IsAuthenticated true from the
    // start, so asking first keeps the keychain out of the git backend's path
    // entirely rather than prompting for secrets it will ignore.
    kiapi::mdw::IsAuthenticatedRequest probe;
    kiapi::mdw::IsAuthenticatedReply   probeReply;

    if( backend->CallSync( probe, probeReply, aTimeoutMs ) == MDW_ERROR_CODE::SUCCESS
        && probeReply.authenticated() )
    {
        m_authenticated = true;
        return true;
    }

    kiapi::mdw::AuthenticateRequest request;
    kiapi::mdw::Credentials*        creds = request.mutable_credentials();

    wxString token;

    if( aStore.LoadRefreshToken( m_binding.backend_id, m_binding.workspace_id, token ).IsOk()
        && !token.IsEmpty() )
    {
        creds->set_refresh_token( std::string( token.utf8_string() ) );
        creds->set_method( "token" );
    }
    else
    {
        creds->set_method( "password" );
    }

    for( const wxString& field : { wxS( "username" ), wxS( "password" ) } )
    {
        wxString value;

        if( aStore.Load( m_binding.backend_id, m_binding.workspace_id, field, value ).IsOk()
            && !value.IsEmpty() )
        {
            ( *creds->mutable_fields() )[std::string( field.utf8_string() )] =
                    std::string( value.utf8_string() );
        }
    }

    kiapi::mdw::AuthResult reply;

    if( backend->CallSync( request, reply, aTimeoutMs ) != MDW_ERROR_CODE::SUCCESS )
    {
        m_authenticated = false;
        return false;
    }

    // A rotated token replaces the stored one; the old value is useless once the
    // backend has minted its successor.
    if( !reply.refresh_token().empty() )
    {
        aStore.StoreRefreshToken( m_binding.backend_id, m_binding.workspace_id,
                                  wxString::FromUTF8( reply.refresh_token() ) );
    }

    m_authenticated = reply.authenticated();

    return m_authenticated;
}


MDW_SESSION::~MDW_SESSION()
{
    // Stop the backend and join its transport threads before the event bus,
    // trackers, service and PUB bridge they dispatch into are destroyed, so no
    // in-flight completion or event touches freed session state.
    if( m_supervisor )
        m_supervisor->Shutdown();

    m_ingress.reset();
    m_service.reset();
    m_pubBridge.reset();
    m_pubServer.reset();
}


MDW_BACKEND* MDW_SESSION::Backend() const
{
    return m_supervisor ? m_supervisor->Backend() : nullptr;
}


bool MDW_SESSION::Online() const
{
    return m_supervisor && m_supervisor->Online();
}


MDW_WORKSPACE_SERVICE* MDW_SESSION::Service()
{
    if( !m_service )
    {
        MDW_BACKEND* backend = Backend();

        if( !backend )
            return nullptr;

        m_service = std::make_unique<MDW_WORKSPACE_SERVICE>( backend, &m_eventBus, &m_lockTracker,
                                                             &m_dirtyTracker, m_binding.workspace_id,
                                                             wxS( "kicad" ) );
    }

    return m_service.get();
}


bool MDW_SESSION::StartEventIngress()
{
    if( m_ingress )
        return true;

    if( !m_supervisor || m_supervisor->RuntimeDir().IsEmpty() )
        return false;

    // The same path the supervisor puts in the child's environment at spawn.
    const wxString url = wxS( "ipc://" ) + m_supervisor->RuntimeDir()
                         + wxFileName::GetPathSeparator() + wxS( "events.sock" );

    auto ingress = std::make_unique<MDW_EVENT_INGRESS>( &m_eventBus,
                                                        m_supervisor->ChannelToken() );

    if( !ingress->Start( url ) )
        return false;

    m_ingress = std::move( ingress );

    return true;
}


bool MDW_SESSION::SubscribeEvents( wxString& aUrl, wxString& aTopicPrefix, std::uint64_t& aSeq )
{
    if( !m_pubServer )
    {
        if( !m_supervisor || m_supervisor->RuntimeDir().IsEmpty() )
            return false;

        wxString url = wxS( "ipc://" ) + m_supervisor->RuntimeDir()
                       + wxFileName::GetPathSeparator() + wxS( "client-events.sock" );

        auto server = std::make_unique<KINNG_PUB_SERVER>( url.utf8_string() );

        if( !server->Start() )
            return false;

        m_pubServer = std::move( server );
        m_pubBridge = std::make_unique<MDW_EVENT_PUB_BRIDGE>( &m_eventBus, m_pubServer.get() );
    }

    aUrl = wxString::FromUTF8( m_pubServer->SocketPath() );
    aTopicPrefix = wxString::FromUTF8( MDW_EVENT_PUB_BRIDGE::TopicPrefixFor( m_binding.workspace_id ) );
    aSeq = m_pubBridge->CurrentSeq();

    return true;
}
