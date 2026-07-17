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
 * Exercises the MDW_BACKEND proxy against a loopback transport: a successful
 * handshake that caches identity, capabilities and schema; a version-mismatch
 * rejection; and an asynchronous call that is cancelled mid-flight.
 */

#include <boost/test/unit_test.hpp>

#include <mdw/mdw_backend.h>
#include <mdw/mdw_loopback_transport.h>

#include <api/mdw/backend_base.pb.h>

#include <future>
#include <memory>
#include <string>

namespace
{
std::string typeName( const kiapi::mdw::MdwRequest& aRequest )
{
    const std::string&     url = aRequest.payload().type_url();
    std::string::size_type slash = url.find_last_of( '/' );

    return slash == std::string::npos ? url : url.substr( slash + 1 );
}


/// A dispatcher that answers Hello, GetSchema and GetItemInfo. When aMajorRequired
/// does not match the caller's Hello major, Hello is rejected with WE_PROTOCOL.
MDW_LOOPBACK_TRANSPORT::DISPATCH makeDispatch( unsigned aMajorRequired )
{
    return [aMajorRequired]( const kiapi::mdw::MdwRequest& aRequest, kiapi::mdw::MdwResponse& aResponse )
    {
        aResponse.set_channel_token( aRequest.channel_token() );
        aResponse.set_request_id( aRequest.request_id() );
        aResponse.mutable_error()->set_code( kiapi::workspace::WE_OK );

        std::string name = typeName( aRequest );

        if( name == "kiapi.mdw.Hello" )
        {
            kiapi::mdw::Hello hello;
            aRequest.payload().UnpackTo( &hello );

            if( hello.protocol_version().major() != aMajorRequired )
            {
                aResponse.mutable_error()->set_code( kiapi::workspace::WE_PROTOCOL );
                return;
            }

            kiapi::mdw::HelloReply reply;
            reply.mutable_backend_info()->set_backend_id( "org.test" );
            reply.mutable_capabilities()->add_groups( kiapi::workspace::CG_LOCKING );
            reply.mutable_capabilities()->add_groups( kiapi::workspace::CG_REVISIONS );
            aResponse.mutable_payload()->PackFrom( reply );
        }
        else if( name == "kiapi.mdw.GetSchemaRequest" )
        {
            kiapi::mdw::GetSchemaReply reply;
            reply.mutable_schema()->set_schema_version( "1" );
            reply.mutable_schema()->add_attributes()->set_attr_id( "owner" );
            aResponse.mutable_payload()->PackFrom( reply );
        }
        else if( name == "kiapi.mdw.GetItemInfoRequest" )
        {
            kiapi::mdw::GetItemInfoRequest request;
            aRequest.payload().UnpackTo( &request );

            kiapi::mdw::GetItemInfoReply reply;
            reply.mutable_item()->set_item_id( request.item_id() );
            aResponse.mutable_payload()->PackFrom( reply );
        }
        else
        {
            aResponse.mutable_error()->set_code( kiapi::workspace::WE_NOT_SUPPORTED );
        }
    };
}


std::unique_ptr<MDW_BACKEND> makeBackend( MDW_LOOPBACK_TRANSPORT::DISPATCH aDispatch, int aLatencyMs )
{
    auto transport = std::make_unique<MDW_LOOPBACK_TRANSPORT>( std::move( aDispatch ), aLatencyMs );
    auto backend = std::make_unique<MDW_BACKEND>( std::move( transport ), wxS( "tok" ) );
    backend->Start();

    return backend;
}
}


BOOST_AUTO_TEST_SUITE( MdwBackendProxy )


BOOST_AUTO_TEST_CASE( HandshakeCachesAdvertisement )
{
    std::unique_ptr<MDW_BACKEND> backend = makeBackend( makeDispatch( 1 ), 0 );

    BOOST_REQUIRE( backend->Handshake( wxS( "10.0" ), wxEmptyString, 3000 ) );
    BOOST_CHECK( backend->Handshaked() );
    BOOST_CHECK_EQUAL( backend->Info().backend_id, wxString( "org.test" ) );
    BOOST_CHECK( backend->Has( MDW_CAPABILITY_GROUP::LOCKING ) );
    BOOST_CHECK( backend->Has( MDW_CAPABILITY_GROUP::REVISIONS ) );
    BOOST_CHECK( !backend->Has( MDW_CAPABILITY_GROUP::ATTRIBUTES ) );
    BOOST_CHECK_EQUAL( backend->Schema().attributes.size(), 1u );
}


BOOST_AUTO_TEST_CASE( HandshakeRejectsVersionMismatch )
{
    // The backend requires major 2 but the proxy speaks major 1.
    std::unique_ptr<MDW_BACKEND> backend = makeBackend( makeDispatch( 2 ), 0 );

    BOOST_CHECK( !backend->Handshake( wxS( "10.0" ), wxEmptyString, 3000 ) );
    BOOST_CHECK( !backend->Handshaked() );
}


BOOST_AUTO_TEST_CASE( AsyncCallCancelled )
{
    std::unique_ptr<MDW_BACKEND> backend = makeBackend( makeDispatch( 1 ), 400 );

    std::promise<MDW_ERROR_CODE> promise;
    std::future<MDW_ERROR_CODE>  future = promise.get_future();

    kiapi::mdw::GetItemInfoRequest request;
    request.set_item_id( "board" );

    std::uint64_t handle = backend->CallAsync<kiapi::mdw::GetItemInfoRequest, kiapi::mdw::GetItemInfoReply>(
            request, 5000,
            [&promise]( MDW_ERROR_CODE aCode, const kiapi::mdw::GetItemInfoReply& )
            { promise.set_value( aCode ); } );

    BOOST_REQUIRE( handle != 0 );
    backend->Cancel( handle );

    BOOST_CHECK( future.get() == MDW_ERROR_CODE::CANCELLED );
}


BOOST_AUTO_TEST_SUITE_END()
