/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * The notification bridge's event-to-message mapping is pure, so it is checked
 * here without a running bus or main loop.
 */

#include <boost/test/unit_test.hpp>

#include <mdw/mdw_notification_bridge.h>


BOOST_AUTO_TEST_SUITE( MdwNotificationBridge )


static MDW_EVENT lockEvent( const wxString& aClient )
{
    MDW_EVENT event;
    event.type = MDW_EVENT_TYPE::ITEM_LOCKED;
    event.item_id = wxS( "board.kicad_pcb" );
    event.client_identity = aClient;

    return event;
}


BOOST_AUTO_TEST_CASE( OwnEventsAreSkipped )
{
    wxString key;
    wxString title;
    wxString body;

    BOOST_CHECK( !MDW_NOTIFICATION_BRIDGE::Describe( lockEvent( wxS( "kicad" ) ), wxS( "kicad" ),
                                                     key, title, body ) );
}


BOOST_AUTO_TEST_CASE( OtherClientLockIsDescribed )
{
    wxString key;
    wxString title;
    wxString body;

    BOOST_CHECK( MDW_NOTIFICATION_BRIDGE::Describe( lockEvent( wxS( "alice" ) ), wxS( "kicad" ),
                                                    key, title, body ) );
    BOOST_CHECK( !title.IsEmpty() );
    BOOST_CHECK( body.Contains( wxS( "alice" ) ) );
    BOOST_CHECK( body.Contains( wxS( "board.kicad_pcb" ) ) );
    BOOST_CHECK( key.Contains( wxS( "board.kicad_pcb" ) ) );
}


BOOST_AUTO_TEST_CASE( UninterestingEventsAreSkipped )
{
    MDW_EVENT event;
    event.type = MDW_EVENT_TYPE::WORKSPACE_RECONNECTED;
    event.client_identity = wxS( "alice" );

    wxString key;
    wxString title;
    wxString body;

    BOOST_CHECK( !MDW_NOTIFICATION_BRIDGE::Describe( event, wxS( "kicad" ), key, title, body ) );
}


BOOST_AUTO_TEST_SUITE_END()
