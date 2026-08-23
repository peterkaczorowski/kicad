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

#include <mdw/mdw_event_pub_bridge.h>
#include <mdw/mdw_event_bus.h>

#include <kinng.h>

#include <api/workspace/workspace_types.pb.h>


static const char* eventTypeName( MDW_EVENT_TYPE aType )
{
    switch( aType )
    {
    case MDW_EVENT_TYPE::WORKSPACE_BOUND:            return "workspace_bound";
    case MDW_EVENT_TYPE::WORKSPACE_UNBOUND:          return "workspace_unbound";
    case MDW_EVENT_TYPE::WORKSPACE_RECONNECTED:      return "workspace_reconnected";
    case MDW_EVENT_TYPE::ITEM_LOCKED:               return "item_locked";
    case MDW_EVENT_TYPE::ITEM_UNLOCKED:             return "item_unlocked";
    case MDW_EVENT_TYPE::REVISION_CHECKED_IN:       return "revision_checked_in";
    case MDW_EVENT_TYPE::REVISION_CHECKED_OUT:      return "revision_checked_out";
    case MDW_EVENT_TYPE::LIFECYCLE_CATEGORY_CHANGED: return "lifecycle_category_changed";
    case MDW_EVENT_TYPE::CUSTOM_STATE_CHANGED:      return "custom_state_changed";
    case MDW_EVENT_TYPE::BACKEND_EVENT:             return "backend_event";
    case MDW_EVENT_TYPE::UNKNOWN:                   return "unknown";
    }

    return "unknown";
}


MDW_EVENT_PUB_BRIDGE::MDW_EVENT_PUB_BRIDGE( MDW_EVENT_BUS* aBus, KINNG_PUB_SERVER* aPub ) :
        m_bus( aBus ),
        m_pub( aPub ),
        m_subscription( 0 ),
        m_seq( 0 )
{
    m_subscription = m_bus->Subscribe( [this]( const MDW_EVENT& aEvent ) { onEvent( aEvent ); } );
}


MDW_EVENT_PUB_BRIDGE::~MDW_EVENT_PUB_BRIDGE()
{
    m_bus->Unsubscribe( m_subscription );
}


std::string MDW_EVENT_PUB_BRIDGE::TopicFor( const MDW_EVENT& aEvent )
{
    return std::string( "workspace/" ) + std::string( aEvent.workspace_id.utf8_string() ) + "/"
           + eventTypeName( aEvent.type );
}


void MDW_EVENT_PUB_BRIDGE::onEvent( const MDW_EVENT& aEvent )
{
    kiapi::workspace::EventEnvelope envelope;
    envelope.set_type( static_cast<kiapi::workspace::EventType>( aEvent.type ) );
    envelope.set_workspace_id( std::string( aEvent.workspace_id.utf8_string() ) );
    envelope.set_item_id( std::string( aEvent.item_id.utf8_string() ) );
    envelope.set_client_identity( std::string( aEvent.client_identity.utf8_string() ) );
    envelope.set_payload( std::string( aEvent.payload.utf8_string() ) );
    envelope.set_backend_event_type( std::string( aEvent.backend_event_type.utf8_string() ) );
    envelope.set_seq( aEvent.seq != 0 ? aEvent.seq : ++m_seq );

    std::string wire;
    envelope.SerializeToString( &wire );

    m_pub->Publish( TopicFor( aEvent ), wire );
}
