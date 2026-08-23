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
 * The event bus to IPC PUB bridge: an internal event becomes exactly one
 * external notification, framed under a workspace/<id>/<type> topic and carrying
 * a decodable EventEnvelope (proposal tests 8 and 14).
 */

#include <boost/test/unit_test.hpp>

#include <wx/filename.h>

#include <mdw/mdw_event_pub_bridge.h>
#include <mdw/mdw_event_bus.h>
#include <kinng.h>

#include <api/workspace/workspace_types.pb.h>

#include <nng/nng.h>
#include <nng/protocol/pubsub0/sub.h>

#include <chrono>
#include <cstring>
#include <string>
#include <thread>


BOOST_AUTO_TEST_SUITE( MdwEventBridge )


BOOST_AUTO_TEST_CASE( InternalEventBecomesOnePubNotification )
{
    std::string url = "ipc://" + wxFileName::CreateTempFileName( "test-mdw-bridge" ).ToStdString();

    KINNG_PUB_SERVER pub( url );
    BOOST_REQUIRE( pub.Start() );

    MDW_EVENT_BUS        bus;
    MDW_EVENT_PUB_BRIDGE bridge( &bus, &pub );

    nng_socket sub;
    BOOST_REQUIRE_EQUAL( nng_sub0_open( &sub ), 0 );
    const char* topic = "workspace/";
    BOOST_REQUIRE_EQUAL( nng_socket_set( sub, NNG_OPT_SUB_SUBSCRIBE, topic, std::strlen( topic ) ), 0 );
    nng_socket_set_ms( sub, NNG_OPT_RECVTIMEO, 200 );
    BOOST_REQUIRE_EQUAL( nng_dial( sub, url.c_str(), nullptr, 0 ), 0 );

    auto recv = [&]( std::string& aOut ) -> bool
    {
        char*  buf = nullptr;
        size_t sz = 0;

        if( nng_recv( sub, &buf, &sz, NNG_FLAG_ALLOC ) != 0 )
            return false;

        aOut.assign( buf, sz );
        nng_free( buf, sz );
        return true;
    };

    // Publish warmup events until the subscription is established, then drain
    // everything so the counted event below starts from a clean stream.
    std::string drained;

    for( int attempt = 0; attempt < 100; ++attempt )
    {
        MDW_EVENT warmup;
        warmup.type = MDW_EVENT_TYPE::WORKSPACE_BOUND;
        warmup.workspace_id = "warmup";
        bus.PublishLocal( warmup, false );

        if( recv( drained ) )
            break;
    }

    while( recv( drained ) )
        ;

    MDW_EVENT event;
    event.type = MDW_EVENT_TYPE::ITEM_LOCKED;
    event.workspace_id = "ws";
    event.item_id = "board.kicad_pcb";
    bus.PublishLocal( event, false );

    std::string message;
    BOOST_REQUIRE( recv( message ) );

    std::string::size_type sep = message.find( '\0' );
    BOOST_REQUIRE( sep != std::string::npos );

    std::string topicPart = message.substr( 0, sep );
    std::string payload = message.substr( sep + 1 );

    BOOST_CHECK_EQUAL( topicPart, std::string( "workspace/ws/item_locked" ) );

    kiapi::workspace::EventEnvelope envelope;
    BOOST_REQUIRE( envelope.ParseFromString( payload ) );
    BOOST_CHECK_EQUAL( envelope.workspace_id(), std::string( "ws" ) );
    BOOST_CHECK_EQUAL( envelope.item_id(), std::string( "board.kicad_pcb" ) );
    BOOST_CHECK( envelope.type() == kiapi::workspace::ET_ITEM_LOCKED );

    // Exactly one notification: nothing else arrives for the single event.
    std::string extra;
    BOOST_CHECK( !recv( extra ) );

    nng_close( sub );
}


BOOST_AUTO_TEST_SUITE_END()
