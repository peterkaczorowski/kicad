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
 * Drives the SDK dispatch server in process over a loopback ipc socket using
 * the kinng request client, exercising typed handler dispatch, capability
 * honesty for unregistered messages, channel-token validation, and handler
 * error propagation through the envelope.
 */

#include <boost/test/unit_test.hpp>

#include <wx/filename.h>

#include <mdw_sdk/mdw_backend_server.h>
#include <kinng.h>

#include <api/mdw/mdw_envelope.pb.h>
#include <api/mdw/backend_base.pb.h>
#include <api/mdw/backend_locking.pb.h>

#include <string>

namespace
{
std::string tempSocketUrl()
{
    return "ipc://" + wxFileName::CreateTempFileName( "test-mdw-sdk" ).ToStdString();
}


kiapi::workspace::WorkspaceError makeError( kiapi::workspace::WorkspaceErrorCode aCode )
{
    kiapi::workspace::WorkspaceError error;
    error.set_code( aCode );
    return error;
}


kiapi::mdw::MdwResponse roundTrip( KINNG_REQUEST_CLIENT& aClient, const kiapi::mdw::MdwRequest& aRequest )
{
    std::string wire;
    aRequest.SerializeToString( &wire );

    KINNG_REQUEST_CLIENT::RESULT result = aClient.RequestSync( wire, 3000 );
    BOOST_REQUIRE( result.received );

    kiapi::mdw::MdwResponse response;
    BOOST_REQUIRE( response.ParseFromString( result.reply ) );

    return response;
}
}


BOOST_AUTO_TEST_SUITE( MdwSdkDispatch )


BOOST_AUTO_TEST_CASE( TypedHandlerDispatch )
{
    std::string        url = tempSocketUrl();
    MDW_BACKEND_SERVER server;
    server.SetChannelToken( "tok" );

    server.RegisterHandler<kiapi::mdw::GetItemInfoRequest, kiapi::mdw::GetItemInfoReply>(
            []( const kiapi::mdw::GetItemInfoRequest& aReq, kiapi::mdw::GetItemInfoReply& aReply )
            {
                aReply.mutable_item()->set_item_id( aReq.item_id() );
                aReply.mutable_item()->set_display_name( "Board" );
                return makeError( kiapi::workspace::WE_OK );
            } );

    BOOST_REQUIRE( server.Start( url, 2 ) );

    KINNG_REQUEST_CLIENT client( url );
    BOOST_REQUIRE( client.Start() );

    kiapi::mdw::MdwRequest request;
    request.set_channel_token( "tok" );
    request.set_request_id( "1" );

    kiapi::mdw::GetItemInfoRequest inner;
    inner.set_item_id( "board.kicad_pcb" );
    request.mutable_payload()->PackFrom( inner );

    kiapi::mdw::MdwResponse response = roundTrip( client, request );

    BOOST_CHECK_EQUAL( response.error().code(), kiapi::workspace::WE_OK );
    BOOST_CHECK_EQUAL( response.request_id(), std::string( "1" ) );

    kiapi::mdw::GetItemInfoReply reply;
    BOOST_REQUIRE( response.payload().UnpackTo( &reply ) );
    BOOST_CHECK_EQUAL( reply.item().item_id(), std::string( "board.kicad_pcb" ) );
}


BOOST_AUTO_TEST_CASE( UnregisteredMessageIsNotSupported )
{
    std::string        url = tempSocketUrl();
    MDW_BACKEND_SERVER server;
    server.SetChannelToken( "tok" );

    // Register only GetItemInfo; a locking request must answer WE_NOT_SUPPORTED.
    server.RegisterHandler<kiapi::mdw::GetItemInfoRequest, kiapi::mdw::GetItemInfoReply>(
            []( const kiapi::mdw::GetItemInfoRequest&, kiapi::mdw::GetItemInfoReply& )
            { return makeError( kiapi::workspace::WE_OK ); } );

    BOOST_REQUIRE( server.Start( url, 2 ) );

    KINNG_REQUEST_CLIENT client( url );
    BOOST_REQUIRE( client.Start() );

    kiapi::mdw::MdwRequest request;
    request.set_channel_token( "tok" );

    kiapi::mdw::LockRequest inner;
    inner.set_item_id( "board.kicad_pcb" );
    request.mutable_payload()->PackFrom( inner );

    kiapi::mdw::MdwResponse response = roundTrip( client, request );
    BOOST_CHECK_EQUAL( response.error().code(), kiapi::workspace::WE_NOT_SUPPORTED );
}


BOOST_AUTO_TEST_CASE( ChannelTokenMismatchRejected )
{
    std::string        url = tempSocketUrl();
    MDW_BACKEND_SERVER server;
    server.SetChannelToken( "right" );

    server.RegisterHandler<kiapi::mdw::GetItemInfoRequest, kiapi::mdw::GetItemInfoReply>(
            []( const kiapi::mdw::GetItemInfoRequest&, kiapi::mdw::GetItemInfoReply& )
            { return makeError( kiapi::workspace::WE_OK ); } );

    BOOST_REQUIRE( server.Start( url, 2 ) );

    KINNG_REQUEST_CLIENT client( url );
    BOOST_REQUIRE( client.Start() );

    kiapi::mdw::MdwRequest request;
    request.set_channel_token( "wrong" );

    kiapi::mdw::GetItemInfoRequest inner;
    inner.set_item_id( "x" );
    request.mutable_payload()->PackFrom( inner );

    kiapi::mdw::MdwResponse response = roundTrip( client, request );
    BOOST_CHECK_EQUAL( response.error().code(), kiapi::workspace::WE_PROTOCOL );
}


BOOST_AUTO_TEST_CASE( HandlerErrorPropagates )
{
    std::string        url = tempSocketUrl();
    MDW_BACKEND_SERVER server;
    server.SetChannelToken( "tok" );

    server.RegisterHandler<kiapi::mdw::GetItemInfoRequest, kiapi::mdw::GetItemInfoReply>(
            []( const kiapi::mdw::GetItemInfoRequest&, kiapi::mdw::GetItemInfoReply& )
            { return makeError( kiapi::workspace::WE_ITEM_NOT_FOUND ); } );

    BOOST_REQUIRE( server.Start( url, 2 ) );

    KINNG_REQUEST_CLIENT client( url );
    BOOST_REQUIRE( client.Start() );

    kiapi::mdw::MdwRequest request;
    request.set_channel_token( "tok" );

    kiapi::mdw::GetItemInfoRequest inner;
    inner.set_item_id( "missing" );
    request.mutable_payload()->PackFrom( inner );

    kiapi::mdw::MdwResponse response = roundTrip( client, request );
    BOOST_CHECK_EQUAL( response.error().code(), kiapi::workspace::WE_ITEM_NOT_FOUND );
    BOOST_CHECK( !response.has_payload() || response.payload().type_url().empty() );
}


BOOST_AUTO_TEST_SUITE_END()
