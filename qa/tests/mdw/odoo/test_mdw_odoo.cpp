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
 * The Odoo backend end-to-end test. It stands up an in-process mock HTTP JSON-RPC
 * server that impersonates Odoo, points the reference kicad-mdw-odoo backend at
 * it through the supervisor, and drives the protocol as a black box: the
 * handshake advertises ATTRIBUTES, authentication succeeds, the fake records
 * list and resolve, the attribute round-trip works, and an unadvertised lock
 * request is answered honestly with NOT_SUPPORTED. No real Odoo is involved.
 */

#include <boost/test/unit_test.hpp>

#include "mock_odoo_server.h"

#include <mdw/mdw_backend_supervisor.h>
#include <mdw/mdw_backend.h>

#include <api/mdw/backend_base.pb.h>
#include <api/mdw/backend_attributes.pb.h>
#include <api/mdw/backend_locking.pb.h>
#include <api/workspace/workspace_types.pb.h>

#include <wx/utils.h>

#include <string>


BOOST_AUTO_TEST_SUITE( MdwOdoo )


BOOST_AUTO_TEST_CASE( OdooBackendEndToEnd )
{
    MOCK_ODOO_SERVER server;
    BOOST_REQUIRE( server.Start() );

    const wxString url =
            wxString::Format( wxS( "http://127.0.0.1:%d" ), server.GetPort() );

    wxSetEnv( wxS( "KICAD_MDW_ODOO_URL" ), url );
    wxSetEnv( wxS( "KICAD_MDW_ODOO_DB" ), wxS( "kicad" ) );
    wxSetEnv( wxS( "KICAD_MDW_ODOO_USER" ), wxS( "tester@kicad.org" ) );

    MDW_BACKEND_MANIFEST manifest;
    manifest.backend_id = wxS( "org.kicad.mdw.odoo" );
    manifest.executable_path = wxString::FromUTF8( MDW_ODOO_EXECUTABLE );
    manifest.valid = true;

    MDW_BACKEND_SUPERVISOR supervisor( manifest );
    BOOST_REQUIRE( supervisor.Launch( wxS( "10.0" ), 8000 ) );

    MDW_BACKEND* backend = supervisor.Backend();
    BOOST_REQUIRE( backend != nullptr );
    BOOST_CHECK( backend->Handshaked() );
    BOOST_CHECK( backend->Has( MDW_CAPABILITY_GROUP::ATTRIBUTES ) );

    // Authentication drives the mock login and must succeed.
    kiapi::mdw::AuthenticateRequest authRequest;
    authRequest.mutable_credentials()->set_method( "password" );
    ( *authRequest.mutable_credentials()->mutable_fields() )["username"] = "tester@kicad.org";
    ( *authRequest.mutable_credentials()->mutable_fields() )["password"] = "secret";
    kiapi::mdw::AuthResult authResult;
    BOOST_CHECK( backend->CallSync( authRequest, authResult, 5000 ) == MDW_ERROR_CODE::SUCCESS );
    BOOST_CHECK( authResult.authenticated() );

    // The fake records list, and one of them resolves.
    kiapi::mdw::ListItemsRequest listRequest;
    listRequest.set_folder_id( "" );
    kiapi::mdw::ListItemsReply listReply;
    BOOST_CHECK( backend->CallSync( listRequest, listReply, 5000 ) == MDW_ERROR_CODE::SUCCESS );
    BOOST_CHECK( listReply.items_size() >= 1 );

    std::string firstId;

    for( const kiapi::workspace::ItemInfo& item : listReply.items() )
    {
        if( item.item_id() == "101" )
            firstId = item.item_id();
    }

    BOOST_REQUIRE( !firstId.empty() );

    kiapi::mdw::GetItemInfoRequest infoRequest;
    infoRequest.set_item_id( firstId );
    kiapi::mdw::GetItemInfoReply infoReply;
    BOOST_CHECK( backend->CallSync( infoRequest, infoReply, 5000 ) == MDW_ERROR_CODE::SUCCESS );
    BOOST_CHECK_EQUAL( infoReply.item().item_id(), firstId );

    // The attribute read parses the mock's JSON field into typed values.
    kiapi::mdw::GetAttributesRequest getAttrRequest;
    getAttrRequest.set_item_id( firstId );
    kiapi::mdw::GetAttributesReply getAttrReply;
    BOOST_CHECK( backend->CallSync( getAttrRequest, getAttrReply, 5000 )
                 == MDW_ERROR_CODE::SUCCESS );
    BOOST_CHECK( getAttrReply.attributes().values_size() >= 1 );

    // The attribute write serializes back through the model write.
    kiapi::mdw::SetAttributesRequest setAttrRequest;
    setAttrRequest.set_item_id( firstId );
    kiapi::workspace::AttributeValue* value = setAttrRequest.mutable_attributes()->add_values();
    value->set_attr_id( "owner" );
    value->set_value( "carol" );
    kiapi::mdw::SetAttributesReply setAttrReply;
    BOOST_CHECK( backend->CallSync( setAttrRequest, setAttrReply, 5000 )
                 == MDW_ERROR_CODE::SUCCESS );

    // Locking is not advertised, so the SDK must answer NOT_SUPPORTED.
    kiapi::mdw::LockRequest lockRequest;
    lockRequest.set_item_id( firstId );
    kiapi::mdw::LockReply lockReply;
    BOOST_CHECK( backend->CallSync( lockRequest, lockReply, 5000 )
                 == MDW_ERROR_CODE::NOT_SUPPORTED );

    supervisor.Shutdown();
    BOOST_CHECK( !supervisor.Online() );

    server.Stop();
}


BOOST_AUTO_TEST_SUITE_END()
