/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2023 Jon Evans <jon@craftyjon.com>
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

#include <wx/filename.h>
#include <wx/stdpaths.h>

#include <qa_utils/wx_utils/unit_test_utils.h>
#include <kinng.h>

#include <import_export.h>
#include <api/common/envelope.pb.h>

#include <nng/nng.h>
#include <nng/protocol/pubsub0/sub.h>

#include <chrono>
#include <cstring>
#include <future>
#include <string>
#include <thread>
#include <vector>

namespace
{
/// A unique ipc:// url per test so cases do not collide on a shared socket path.
std::string tempSocketUrl()
{
    return "ipc://" + wxFileName::CreateTempFileName( "test-kinng" ).ToStdString();
}
}


BOOST_AUTO_TEST_SUITE( KiNNG )

BOOST_AUTO_TEST_CASE( CreateIPCResponder )
{
    KINNG_REQUEST_SERVER server( wxFileName::CreateTempFileName( "test-kinng" ).ToStdString() );
}


BOOST_AUTO_TEST_CASE( ClientEchoAsync )
{
    std::string          url = tempSocketUrl();
    KINNG_REQUEST_SERVER server( url );

    server.SetCallback( [&]( std::string* aMsg ) { server.Reply( "echo:" + *aMsg ); } );
    BOOST_REQUIRE( server.Start() );

    KINNG_REQUEST_CLIENT client( url );
    BOOST_REQUIRE( client.Start() );

    std::promise<KINNG_REQUEST_CLIENT::RESULT> promise;
    std::future<KINNG_REQUEST_CLIENT::RESULT>  future = promise.get_future();

    std::uint64_t id = client.Request( "hello", 3000,
                                       [&]( const KINNG_REQUEST_CLIENT::RESULT& aResult )
                                       { promise.set_value( aResult ); } );
    BOOST_REQUIRE( id != 0 );

    KINNG_REQUEST_CLIENT::RESULT result = future.get();
    BOOST_CHECK( result.received );
    BOOST_CHECK_EQUAL( result.reply, std::string( "echo:hello" ) );
}


BOOST_AUTO_TEST_CASE( ClientRequestSync )
{
    std::string          url = tempSocketUrl();
    KINNG_REQUEST_SERVER server( url );

    server.SetCallback( [&]( std::string* aMsg ) { server.Reply( "sync:" + *aMsg ); } );
    BOOST_REQUIRE( server.Start() );

    KINNG_REQUEST_CLIENT client( url );
    BOOST_REQUIRE( client.Start() );

    KINNG_REQUEST_CLIENT::RESULT result = client.RequestSync( "ping", 3000 );
    BOOST_CHECK( result.received );
    BOOST_CHECK_EQUAL( result.reply, std::string( "sync:ping" ) );
}


BOOST_AUTO_TEST_CASE( ClientTimeout )
{
    std::string          url = tempSocketUrl();
    KINNG_REQUEST_SERVER server( url );

    // The server receives the request but never replies, so the client must
    // report a timeout rather than hang.
    server.SetCallback( []( std::string* ) {} );
    BOOST_REQUIRE( server.Start() );

    KINNG_REQUEST_CLIENT client( url );
    BOOST_REQUIRE( client.Start() );

    KINNG_REQUEST_CLIENT::RESULT result = client.RequestSync( "slow", 300 );
    BOOST_CHECK( !result.received );
    BOOST_CHECK( result.timed_out );
}


BOOST_AUTO_TEST_CASE( ClientCancel )
{
    std::string          url = tempSocketUrl();
    KINNG_REQUEST_SERVER server( url );

    server.SetCallback( []( std::string* ) {} );
    BOOST_REQUIRE( server.Start() );

    KINNG_REQUEST_CLIENT client( url );
    BOOST_REQUIRE( client.Start() );

    std::promise<KINNG_REQUEST_CLIENT::RESULT> promise;
    std::future<KINNG_REQUEST_CLIENT::RESULT>  future = promise.get_future();

    std::uint64_t id = client.Request( "will-cancel", 5000,
                                       [&]( const KINNG_REQUEST_CLIENT::RESULT& aResult )
                                       { promise.set_value( aResult ); } );
    BOOST_REQUIRE( id != 0 );

    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    client.Cancel( id );

    KINNG_REQUEST_CLIENT::RESULT result = future.get();
    BOOST_CHECK( !result.received );
    BOOST_CHECK( result.cancelled );
}


BOOST_AUTO_TEST_CASE( ClientConcurrentRequests )
{
    std::string          url = tempSocketUrl();
    KINNG_REQUEST_SERVER server( url );

    server.SetCallback( [&]( std::string* aMsg ) { server.Reply( "r:" + *aMsg ); } );
    BOOST_REQUIRE( server.Start() );

    KINNG_REQUEST_CLIENT client( url );
    BOOST_REQUIRE( client.Start() );

    constexpr int                                            count = 5;
    std::vector<std::promise<KINNG_REQUEST_CLIENT::RESULT>>   promises( count );
    std::vector<std::future<KINNG_REQUEST_CLIENT::RESULT>>    futures;

    for( int i = 0; i < count; ++i )
        futures.push_back( promises[i].get_future() );

    for( int i = 0; i < count; ++i )
    {
        std::uint64_t id = client.Request( std::to_string( i ), 3000,
                                           [&promises, i]( const KINNG_REQUEST_CLIENT::RESULT& aResult )
                                           { promises[i].set_value( aResult ); } );
        BOOST_REQUIRE( id != 0 );
    }

    for( int i = 0; i < count; ++i )
    {
        KINNG_REQUEST_CLIENT::RESULT result = futures[i].get();
        BOOST_CHECK( result.received );
        BOOST_CHECK_EQUAL( result.reply, "r:" + std::to_string( i ) );
    }
}


BOOST_AUTO_TEST_CASE( PubServerTopicFiltering )
{
    std::string      url = tempSocketUrl();
    KINNG_PUB_SERVER pub( url );
    BOOST_REQUIRE( pub.Start() );

    nng_socket sub;
    BOOST_REQUIRE_EQUAL( nng_sub0_open( &sub ), 0 );

    const char* topic = "workspace/proj";
    BOOST_REQUIRE_EQUAL( nng_socket_set( sub, NNG_OPT_SUB_SUBSCRIBE, topic, std::strlen( topic ) ), 0 );
    nng_socket_set_ms( sub, NNG_OPT_RECVTIMEO, 200 );
    BOOST_REQUIRE_EQUAL( nng_dial( sub, url.c_str(), nullptr, 0 ), 0 );

    // pub/sub drops messages published before the subscription is established,
    // so republish until the subscriber receives one rather than relying on a
    // fixed connect delay.
    std::string expected =
            std::string( "workspace/proj" ) + KINNG_PUB_SERVER::TOPIC_SEPARATOR + "delivered";
    std::string received;

    for( int attempt = 0; attempt < 50 && received.empty(); ++attempt )
    {
        // Filtered by the subscription, so it must never arrive.
        BOOST_CHECK( pub.Publish( "other/proj", "dropped" ) );
        BOOST_CHECK( pub.Publish( "workspace/proj", "delivered" ) );

        char*  buf = nullptr;
        size_t sz = 0;
        int    rv = nng_recv( sub, &buf, &sz, NNG_FLAG_ALLOC );

        if( rv == 0 )
        {
            received.assign( buf, sz );
            nng_free( buf, sz );
        }
    }

    BOOST_CHECK_EQUAL( received, expected );

    nng_close( sub );
}

BOOST_AUTO_TEST_SUITE_END()
