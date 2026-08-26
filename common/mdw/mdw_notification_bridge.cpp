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

#include <mdw/mdw_notification_bridge.h>
#include <mdw/mdw_event_bus.h>

#include <wx/app.h>


bool MDW_NOTIFICATION_BRIDGE::Describe( const MDW_EVENT& aEvent, const wxString& aSelfIdentity,
                                        wxString& aKey, wxString& aTitle, wxString& aBody )
{
    if( !aEvent.client_identity.IsEmpty() && aEvent.client_identity == aSelfIdentity )
        return false;

    wxString who = aEvent.client_identity.IsEmpty() ? _( "another user" ) : aEvent.client_identity;

    switch( aEvent.type )
    {
    case MDW_EVENT_TYPE::ITEM_LOCKED:
        aTitle = _( "Item locked" );
        aBody = wxString::Format( _( "%s was locked by %s." ), aEvent.item_id, who );
        break;

    case MDW_EVENT_TYPE::ITEM_UNLOCKED:
        aTitle = _( "Item unlocked" );
        aBody = wxString::Format( _( "%s was unlocked by %s." ), aEvent.item_id, who );
        break;

    case MDW_EVENT_TYPE::REVISION_CHECKED_IN:
        aTitle = _( "New revision" );
        aBody = wxString::Format( _( "%s was checked in by %s." ), aEvent.item_id, who );
        break;

    case MDW_EVENT_TYPE::LIFECYCLE_CATEGORY_CHANGED:
        aTitle = _( "Lifecycle changed" );
        aBody = wxString::Format( _( "The lifecycle state of %s changed." ), aEvent.item_id );
        break;

    default:
        return false;
    }

    aKey = wxString::Format( wxS( "mdw-%d-%s" ), static_cast<int>( aEvent.type ), aEvent.item_id );

    return true;
}


MDW_NOTIFICATION_BRIDGE::MDW_NOTIFICATION_BRIDGE( MDW_EVENT_BUS* aBus, const wxString& aSelfIdentity,
                                                  NOTIFY_FN aNotify ) :
        m_bus( aBus ),
        m_subscription( 0 ),
        m_selfIdentity( aSelfIdentity ),
        m_notify( std::move( aNotify ) ),
        m_alive( std::make_shared<std::atomic<bool>>( true ) )
{
    if( !m_bus )
        return;

    std::weak_ptr<std::atomic<bool>> alive = m_alive;
    MDW_NOTIFICATION_BRIDGE*          self = this;

    m_subscription = m_bus->Subscribe(
            [alive, self]( const MDW_EVENT& aEvent )
            {
                // The bus may dispatch on the transport thread, so nothing here
                // touches the bridge. The event is copied into the main-loop post,
                // which rechecks the token before using the bridge.
                MDW_EVENT copy = aEvent;

                wxTheApp->CallAfter(
                        [alive, self, copy]()
                        {
                            std::shared_ptr<std::atomic<bool>> token = alive.lock();

                            if( !token || !token->load() || !self->m_notify )
                                return;

                            wxString key;
                            wxString title;
                            wxString body;

                            if( Describe( copy, self->m_selfIdentity, key, title, body ) )
                                self->m_notify( key, title, body );
                        } );
            } );
}


MDW_NOTIFICATION_BRIDGE::~MDW_NOTIFICATION_BRIDGE()
{
    m_alive->store( false );
    unsubscribe();
}


void MDW_NOTIFICATION_BRIDGE::unsubscribe()
{
    if( m_bus && m_subscription )
        m_bus->Unsubscribe( m_subscription );

    m_bus = nullptr;
    m_subscription = 0;
}
