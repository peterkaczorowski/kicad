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

#include <mdw/mdw_event_ingress.h>
#include <mdw/mdw_event_bus.h>

#include <kinng.h>

#include <api/mdw/mdw_envelope.pb.h>
#include <api/workspace/workspace_types.pb.h>

// The envelope carries the event type as the shared wire enum, recovered here by
// cast. See the matching asserts in mdw_backend.h.
static_assert( static_cast<int>( MDW_EVENT_TYPE::BACKEND_EVENT )
                       == static_cast<int>( kiapi::workspace::ET_BACKEND_EVENT ),
               "MDW_EVENT_TYPE has drifted from kiapi.workspace.EventType" );


MDW_EVENT_INGRESS::MDW_EVENT_INGRESS( MDW_EVENT_BUS* aBus, const wxString& aChannelToken ) :
        m_bus( aBus ),
        m_channelToken( aChannelToken.utf8_string() )
{
}


MDW_EVENT_INGRESS::~MDW_EVENT_INGRESS()
{
    Stop();
}


bool MDW_EVENT_INGRESS::Start( const wxString& aUrl )
{
    if( m_server )
        return true;

    auto server = std::make_unique<KINNG_REQUEST_SERVER>( aUrl.utf8_string() );

    server->SetCallback( [this]( std::string* aMessage ) { onMessage( aMessage ); } );

    if( !server->Start() )
        return false;

    m_server = std::move( server );

    return true;
}


void MDW_EVENT_INGRESS::Stop()
{
    // Stopping joins the listen thread, so no callback is in flight afterwards
    // and the bus this holds is safe to destroy.
    if( m_server )
        m_server->Stop();

    m_server.reset();
}


bool MDW_EVENT_INGRESS::Running() const
{
    return m_server && m_server->Running();
}


void MDW_EVENT_INGRESS::onMessage( std::string* aMessage )
{
    // The backend retries anything it does not see acknowledged, so a malformed
    // or unauthenticated message is acknowledged and dropped rather than left to
    // be resent forever.
    auto acknowledge = [this]() { m_server->Reply( std::string() ); };

    kiapi::mdw::PublishEvent publish;

    if( !aMessage || !publish.ParseFromString( *aMessage ) )
    {
        acknowledge();
        return;
    }

    if( publish.channel_token() != m_channelToken )
    {
        acknowledge();
        return;
    }

    const kiapi::workspace::EventEnvelope& envelope = publish.event();

    MDW_EVENT event;
    event.type = static_cast<MDW_EVENT_TYPE>( envelope.type() );
    event.workspace_id = wxString::FromUTF8( envelope.workspace_id() );
    event.item_id = wxString::FromUTF8( envelope.item_id() );
    event.client_identity = wxString::FromUTF8( envelope.client_identity() );
    event.payload = wxString::FromUTF8( envelope.payload() );
    event.backend_event_type = wxString::FromUTF8( envelope.backend_event_type() );
    event.seq = envelope.seq();

    if( envelope.timestamp_unix() != 0 )
        event.timestamp = wxDateTime( static_cast<time_t>( envelope.timestamp_unix() ) );

    // Acknowledge before dispatching. A subscriber that throws or blocks must not
    // make the backend resend an event the framework has already accepted.
    acknowledge();

    if( m_bus )
        m_bus->PublishFromBackend( event );
}
