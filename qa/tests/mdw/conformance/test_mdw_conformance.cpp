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
 * The conformance suite is the behavioural contract every backend must meet. It
 * spawns the reference mock backend through the real supervisor and drives it as
 * a black box: handshake, mandatory-method coverage, capability honesty in both
 * directions, schema stability, request timeout, clean shutdown, and recovery
 * from a backend that dies mid-session.
 */

#include <boost/test/unit_test.hpp>

#include <mdw/mdw_backend_supervisor.h>
#include <mdw/mdw_backend.h>

#include <api/mdw/backend_base.pb.h>
#include <api/mdw/backend_locking.pb.h>
#include <api/mdw/backend_forge.pb.h>

#include <wx/process.h>
#include <wx/utils.h>

#include <memory>

namespace
{
std::unique_ptr<MDW_BACKEND_SUPERVISOR> launchMock( const wxString& aCapabilities, long aLatencyMs )
{
    wxSetEnv( wxS( "MDW_MOCK_CAPABILITIES" ), aCapabilities );

    if( aLatencyMs > 0 )
        wxSetEnv( wxS( "MDW_MOCK_LATENCY_MS" ), wxString::Format( wxS( "%ld" ), aLatencyMs ) );
    else
        wxUnsetEnv( wxS( "MDW_MOCK_LATENCY_MS" ) );

    MDW_BACKEND_MANIFEST manifest;
    manifest.backend_id = wxS( "org.kicad.mdw.mock" );
    manifest.executable_path = wxString::FromUTF8( MDW_MOCK_EXECUTABLE );
    manifest.valid = true;

    auto supervisor = std::make_unique<MDW_BACKEND_SUPERVISOR>( manifest );

    if( !supervisor->Launch( wxS( "10.0" ), 8000 ) )
        return nullptr;

    return supervisor;
}
}


BOOST_AUTO_TEST_SUITE( MdwConformance )


BOOST_AUTO_TEST_CASE( HandshakeAndIdentity )
{
    auto supervisor = launchMock( wxS( "LOCKING,REVISIONS,ATTRIBUTES,LIFECYCLE,EVENTS" ), 0 );
    BOOST_REQUIRE( supervisor );

    MDW_BACKEND* backend = supervisor->Backend();
    BOOST_REQUIRE( backend != nullptr );
    BOOST_CHECK( backend->Handshaked() );
    BOOST_CHECK_EQUAL( backend->Info().backend_id, wxString( "org.kicad.mdw.mock" ) );
    BOOST_CHECK( backend->Has( MDW_CAPABILITY_GROUP::LOCKING ) );
}


BOOST_AUTO_TEST_CASE( MandatoryMethodCoverage )
{
    auto supervisor = launchMock( wxS( "LOCKING,REVISIONS,ATTRIBUTES,LIFECYCLE,EVENTS" ), 0 );
    BOOST_REQUIRE( supervisor );

    MDW_BACKEND* backend = supervisor->Backend();

    kiapi::mdw::AuthenticateRequest authRequest;
    authRequest.mutable_credentials()->set_method( "password" );
    kiapi::mdw::AuthResult authResult;
    BOOST_CHECK( backend->CallSync( authRequest, authResult, 5000 ) == MDW_ERROR_CODE::SUCCESS );
    BOOST_CHECK( authResult.authenticated() );

    kiapi::mdw::GetRootFoldersRequest rootRequest;
    kiapi::mdw::GetRootFoldersReply   rootReply;
    BOOST_CHECK( backend->CallSync( rootRequest, rootReply, 5000 ) == MDW_ERROR_CODE::SUCCESS );
    BOOST_CHECK( rootReply.folders_size() >= 1 );

    kiapi::mdw::ListItemsRequest listRequest;
    listRequest.set_folder_id( "root" );
    kiapi::mdw::ListItemsReply listReply;
    BOOST_CHECK( backend->CallSync( listRequest, listReply, 5000 ) == MDW_ERROR_CODE::SUCCESS );
    BOOST_CHECK( listReply.items_size() >= 1 );

    kiapi::mdw::GetItemInfoRequest infoRequest;
    infoRequest.set_item_id( "board.kicad_pcb" );
    kiapi::mdw::GetItemInfoReply infoReply;
    BOOST_CHECK( backend->CallSync( infoRequest, infoReply, 5000 ) == MDW_ERROR_CODE::SUCCESS );

    kiapi::mdw::CheckoutRequest checkoutRequest;
    checkoutRequest.set_item_id( "board.kicad_pcb" );
    checkoutRequest.set_local_path( "/tmp/board.kicad_pcb" );
    kiapi::mdw::CheckoutReply checkoutReply;
    BOOST_CHECK( backend->CallSync( checkoutRequest, checkoutReply, 5000 ) == MDW_ERROR_CODE::SUCCESS );
}


BOOST_AUTO_TEST_CASE( CapabilityHonestyBothDirections )
{
    auto supervisor = launchMock( wxS( "LOCKING" ), 0 );
    BOOST_REQUIRE( supervisor );

    MDW_BACKEND* backend = supervisor->Backend();

    // Advertised: locking answers normally.
    BOOST_CHECK( backend->Has( MDW_CAPABILITY_GROUP::LOCKING ) );

    kiapi::mdw::LockRequest lockRequest;
    lockRequest.set_item_id( "board.kicad_pcb" );
    kiapi::mdw::LockReply lockReply;
    BOOST_CHECK( backend->CallSync( lockRequest, lockReply, 5000 ) == MDW_ERROR_CODE::SUCCESS );

    // Unadvertised: forge is not advertised, so it must answer NOT_SUPPORTED.
    BOOST_CHECK( !backend->Has( MDW_CAPABILITY_GROUP::FORGE ) );

    kiapi::mdw::GetForgeInfoRequest forgeRequest;
    kiapi::mdw::GetForgeInfoReply   forgeReply;
    BOOST_CHECK( backend->CallSync( forgeRequest, forgeReply, 5000 )
                 == MDW_ERROR_CODE::NOT_SUPPORTED );
}


BOOST_AUTO_TEST_CASE( SchemaIsStable )
{
    auto supervisor = launchMock( wxS( "ATTRIBUTES" ), 0 );
    BOOST_REQUIRE( supervisor );

    MDW_BACKEND* backend = supervisor->Backend();

    kiapi::mdw::GetSchemaRequest schemaRequest;
    kiapi::mdw::GetSchemaReply   first;
    kiapi::mdw::GetSchemaReply   second;
    BOOST_CHECK( backend->CallSync( schemaRequest, first, 5000 ) == MDW_ERROR_CODE::SUCCESS );
    BOOST_CHECK( backend->CallSync( schemaRequest, second, 5000 ) == MDW_ERROR_CODE::SUCCESS );

    BOOST_CHECK_EQUAL( first.schema().schema_version(), second.schema().schema_version() );
    BOOST_CHECK_EQUAL( first.schema().attributes_size(), second.schema().attributes_size() );
}


BOOST_AUTO_TEST_CASE( RequestTimesOut )
{
    // The mock delays every data handler; a short per-request timeout must fire.
    auto supervisor = launchMock( wxS( "LOCKING" ), 1500 );
    BOOST_REQUIRE( supervisor );

    MDW_BACKEND* backend = supervisor->Backend();

    kiapi::mdw::GetItemInfoRequest request;
    request.set_item_id( "board.kicad_pcb" );
    kiapi::mdw::GetItemInfoReply reply;

    BOOST_CHECK( backend->CallSync( request, reply, 300 ) == MDW_ERROR_CODE::TIMEOUT );
}


BOOST_AUTO_TEST_CASE( RecoversFromBackendDeath )
{
    auto supervisor = launchMock( wxS( "LOCKING" ), 0 );
    BOOST_REQUIRE( supervisor );

    MDW_BACKEND* backend = supervisor->Backend();

    // Kill the backend out from under the proxy, then a call must fail cleanly as
    // a lost backend rather than hang.
    wxKill( supervisor->ProcessId(), wxSIGKILL );

    kiapi::mdw::GetItemInfoRequest request;
    request.set_item_id( "board.kicad_pcb" );
    kiapi::mdw::GetItemInfoReply reply;

    MDW_ERROR_CODE code = backend->CallSync( request, reply, 3000 );
    BOOST_CHECK( code == MDW_ERROR_CODE::BACKEND_LOST || code == MDW_ERROR_CODE::TIMEOUT );
}


BOOST_AUTO_TEST_CASE( CleanShutdown )
{
    auto supervisor = launchMock( wxS( "LOCKING" ), 0 );
    BOOST_REQUIRE( supervisor );

    long pid = supervisor->ProcessId();
    BOOST_REQUIRE( pid > 0 );

    supervisor->Shutdown();
    BOOST_CHECK( !supervisor->Online() );

    // The process must be gone after shutdown.
    BOOST_CHECK( !wxProcess::Exists( pid ) );
}


BOOST_AUTO_TEST_SUITE_END()
