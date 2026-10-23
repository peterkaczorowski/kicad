/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2026 KiCad Developers, see AUTHORS.txt for contributors.
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
 * kicad-mdw-odoo
 *
 * A minimal reference MDW backend that maps an Odoo model onto the workspace
 * model over JSON-RPC. Items are records of the model named by
 * KICAD_MDW_ODOO_MODEL (default "mdw.item"), folders are the distinct values of
 * their folder field, and item attributes are carried in a JSON field. It
 * advertises CG_ATTRIBUTES and nothing else, so the SDK auto-answers
 * WE_NOT_SUPPORTED for every group it does not serve. Like the SDK it links
 * kiapi, nng and curl only; no wxWidgets and no kicommon.
 */

#include "odoo_rpc_client.h"
#include "odoo_error_mapper.h"

#include <mdw_sdk/mdw_backend_server.h>
#include <mdw_sdk/mdw_folder_tree.h>
#include <mdw_sdk/mdw_run_backend.h>

#include <api/mdw/backend_base.pb.h>
#include <api/mdw/backend_attributes.pb.h>
#include <api/workspace/workspace_types.pb.h>

#include <curl/curl.h>

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <optional>
#include <set>
#include <string>

namespace mdw = kiapi::mdw;
namespace ws = kiapi::workspace;

namespace
{

/// The record fields every handler that returns an ItemInfo has to read.
const nlohmann::json ITEM_FIELDS = nlohmann::json::array( { "id", "name", "folder", "state" } );


/// The shared Odoo session. Handlers run on worker threads, so the client and
/// its request counter are serialized behind one mutex.
struct ODOO_STATE
{
    std::mutex      mutex;
    ODOO_RPC_CLIENT client;
    std::string     model;
};


std::string envValue( const char* aName, const std::string& aFallback )
{
    const char* value = std::getenv( aName );

    return ( value && value[0] != '\0' ) ? std::string( value ) : aFallback;
}


ODOO_STATE& odooState()
{
    static ODOO_STATE state;
    static const bool init = []()
    {
        state.client.SetServer( envValue( "KICAD_MDW_ODOO_URL", "http://127.0.0.1:8069" ),
                                envValue( "KICAD_MDW_ODOO_DB", "kicad" ) );
        state.model = envValue( "KICAD_MDW_ODOO_MODEL", "mdw.item" );
        return true;
    }();

    (void) init;
    return state;
}


bool isAllDigits( const std::string& aText )
{
    return !aText.empty()
           && std::all_of( aText.begin(), aText.end(),
                           []( char c ) { return c >= '0' && c <= '9'; } );
}


std::string fieldString( const nlohmann::json& aRecord, const char* aKey )
{
    if( !aRecord.contains( aKey ) )
        return std::string();

    const nlohmann::json& value = aRecord[aKey];

    if( value.is_string() )
        return value.get<std::string>();

    if( value.is_number_integer() )
        return std::to_string( value.get<long long>() );

    // Odoo returns JSON false for an unset char or text field.
    if( value.is_boolean() )
        return value.get<bool>() ? "1" : std::string();

    if( value.is_null() )
        return std::string();

    return value.dump();
}


/// Build an ItemInfo from a search_read/read record. item_id is the numeric id
/// when present, else the record name.
ws::ItemInfo itemInfo( const nlohmann::json& aRecord )
{
    ws::ItemInfo item;

    std::string id = fieldString( aRecord, "id" );
    std::string name = fieldString( aRecord, "name" );

    item.set_item_id( id.empty() ? name : id );
    item.set_display_name( name );
    item.set_folder_id( fieldString( aRecord, "folder" ) );
    item.set_is_folder( false );
    item.set_category( ws::LC_EDITABLE );
    return item;
}


/**
 * Resolve a request item_id into aId. A numeric id is taken verbatim; any other
 * value is looked up by name. The caller must already hold the state mutex.
 *
 * Returns the error to answer the request with when the id does not resolve, so
 * a handler can forward it unchanged and otherwise proceed with aId.
 */
std::optional<ws::WorkspaceError> resolveOrFail( ODOO_STATE& aState, const std::string& aItemId,
                                                 long long& aId )
{
    if( aItemId.empty() )
        return MdwFail( ws::WE_ITEM_NOT_FOUND, "empty item id" );

    if( isAllDigits( aItemId ) )
    {
        aId = std::strtoll( aItemId.c_str(), nullptr, 10 );
        return std::nullopt;
    }

    nlohmann::json domain = nlohmann::json::array();
    domain.push_back( nlohmann::json::array( { "name", "=", aItemId } ) );

    ODOO_RPC_RESULT lookup =
            aState.client.SearchRead( aState.model, domain, nlohmann::json::array( { "id" } ), 1 );

    if( !lookup.success )
        return OdooMapError( lookup, ws::WE_ITEM_NOT_FOUND );

    if( lookup.result.is_array() && !lookup.result.empty() && lookup.result[0].contains( "id" ) )
    {
        aId = lookup.result[0]["id"].get<long long>();
        return std::nullopt;
    }

    return MdwFail( ws::WE_ITEM_NOT_FOUND, "no such item" );
}


void registerBaseHandlers( MDW_BACKEND_SERVER& aServer )
{
    aServer.RegisterHandler<mdw::AuthenticateRequest, mdw::AuthResult>(
            []( const mdw::AuthenticateRequest& aRequest,
                mdw::AuthResult& aReply ) -> ws::WorkspaceError
            {
                const mdw::Credentials& creds = aRequest.credentials();
                const auto&             fields = creds.fields();

                std::string login = envValue( "KICAD_MDW_ODOO_USER", std::string() );

                if( fields.count( "username" ) )
                    login = fields.at( "username" );

                std::string password;

                if( fields.count( "password" ) )
                    password = fields.at( "password" );
                else if( fields.count( "api_key" ) )
                    password = fields.at( "api_key" );

                if( login.empty() || password.empty() )
                {
                    aReply.set_authenticated( false );
                    return MdwFail( ws::WE_AUTH_FAILED, "missing username or password" );
                }

                ODOO_STATE&                 state = odooState();
                std::lock_guard<std::mutex> lock( state.mutex );

                ODOO_RPC_RESULT result = state.client.Authenticate( login, password );

                if( !result.success )
                {
                    aReply.set_authenticated( false );
                    return OdooMapError( result, ws::WE_AUTH_FAILED );
                }

                aReply.set_authenticated( true );
                aReply.set_user_id( std::to_string( state.client.Uid() ) );
                aReply.set_display_name( login );
                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::LogoutRequest, mdw::LogoutReply>(
            []( const mdw::LogoutRequest&, mdw::LogoutReply& aReply ) -> ws::WorkspaceError
            {
                ODOO_STATE&                 state = odooState();
                std::lock_guard<std::mutex> lock( state.mutex );

                state.client.Reset();
                aReply.set_ok( true );
                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::IsAuthenticatedRequest, mdw::IsAuthenticatedReply>(
            []( const mdw::IsAuthenticatedRequest&,
                mdw::IsAuthenticatedReply& aReply ) -> ws::WorkspaceError
            {
                ODOO_STATE&                 state = odooState();
                std::lock_guard<std::mutex> lock( state.mutex );

                aReply.set_authenticated( state.client.IsAuthenticated() );
                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::GetRootFoldersRequest, mdw::GetRootFoldersReply>(
            []( const mdw::GetRootFoldersRequest&,
                mdw::GetRootFoldersReply& aReply ) -> ws::WorkspaceError
            {
                ws::Folder* folder = aReply.add_folders();
                folder->set_folder_id( "" );
                folder->set_display_name( "Root" );
                folder->set_has_children( true );
                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::GetChildFoldersRequest, mdw::GetChildFoldersReply>(
            []( const mdw::GetChildFoldersRequest& aRequest,
                mdw::GetChildFoldersReply& aReply ) -> ws::WorkspaceError
            {
                ODOO_STATE&                 state = odooState();
                std::lock_guard<std::mutex> lock( state.mutex );

                ODOO_RPC_RESULT result = state.client.SearchRead(
                        state.model, nlohmann::json::array(),
                        nlohmann::json::array( { "folder" } ) );

                if( !result.success )
                    return OdooMapError( result );

                std::set<std::string> folders;

                for( const nlohmann::json& record : result.result )
                {
                    std::string folder = fieldString( record, "folder" );

                    if( !folder.empty() )
                        folders.insert( folder );
                }

                MdwAddChildFolders( folders, aRequest.folder_id(), aReply );

                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::ListItemsRequest, mdw::ListItemsReply>(
            []( const mdw::ListItemsRequest& aRequest,
                mdw::ListItemsReply& aReply ) -> ws::WorkspaceError
            {
                ODOO_STATE&                 state = odooState();
                std::lock_guard<std::mutex> lock( state.mutex );

                nlohmann::json domain = nlohmann::json::array();
                domain.push_back(
                        nlohmann::json::array( { "folder", "=", aRequest.folder_id() } ) );

                ODOO_RPC_RESULT result =
                        state.client.SearchRead( state.model, domain, ITEM_FIELDS );

                if( !result.success )
                    return OdooMapError( result );

                for( const nlohmann::json& record : result.result )
                    *aReply.add_items() = itemInfo( record );

                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::GetItemInfoRequest, mdw::GetItemInfoReply>(
            []( const mdw::GetItemInfoRequest& aRequest,
                mdw::GetItemInfoReply& aReply ) -> ws::WorkspaceError
            {
                ODOO_STATE&                 state = odooState();
                std::lock_guard<std::mutex> lock( state.mutex );

                long long id = 0;

                if( std::optional<ws::WorkspaceError> failure =
                            resolveOrFail( state, aRequest.item_id(), id ) )
                {
                    return *failure;
                }

                ODOO_RPC_RESULT result =
                        state.client.Read( state.model, nlohmann::json::array( { id } ),
                                           ITEM_FIELDS );

                if( !result.success )
                    return OdooMapError( result, ws::WE_ITEM_NOT_FOUND );

                if( !result.result.is_array() || result.result.empty() )
                    return MdwFail( ws::WE_ITEM_NOT_FOUND, "no such item" );

                *aReply.mutable_item() = itemInfo( result.result[0] );
                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::CheckoutRequest, mdw::CheckoutReply>(
            []( const mdw::CheckoutRequest& aRequest,
                mdw::CheckoutReply& aReply ) -> ws::WorkspaceError
            {
                ODOO_STATE&                 state = odooState();
                std::lock_guard<std::mutex> lock( state.mutex );

                long long id = 0;

                if( std::optional<ws::WorkspaceError> failure =
                            resolveOrFail( state, aRequest.item_id(), id ) )
                {
                    return *failure;
                }

                ODOO_RPC_RESULT result =
                        state.client.Read( state.model, nlohmann::json::array( { id } ),
                                           ITEM_FIELDS );

                if( !result.success || !result.result.is_array() || result.result.empty() )
                    return MdwFail( ws::WE_ITEM_NOT_FOUND, "no such item" );

                *aReply.mutable_item() = itemInfo( result.result[0] );
                aReply.set_local_path( aRequest.local_path() );
                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::CheckinRequest, mdw::CheckinReply>(
            []( const mdw::CheckinRequest& aRequest,
                mdw::CheckinReply& aReply ) -> ws::WorkspaceError
            {
                ODOO_STATE&                 state = odooState();
                std::lock_guard<std::mutex> lock( state.mutex );

                long long id = 0;

                if( std::optional<ws::WorkspaceError> failure =
                            resolveOrFail( state, aRequest.item_id(), id ) )
                {
                    return *failure;
                }

                nlohmann::json values;
                values["last_comment"] = aRequest.comment();

                ODOO_RPC_RESULT result =
                        state.client.Write( state.model, nlohmann::json::array( { id } ), values );

                if( !result.success )
                    return OdooMapError( result );

                std::time_t   now = std::time( nullptr );
                ws::Revision* rev = aReply.mutable_revision();
                rev->set_revision_id( std::to_string( static_cast<long long>( now ) ) );
                rev->set_comment( aRequest.comment() );
                rev->set_timestamp_unix( static_cast<long long>( now ) );
                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::CancelCheckoutRequest, mdw::CancelCheckoutReply>(
            []( const mdw::CancelCheckoutRequest&,
                mdw::CancelCheckoutReply& ) -> ws::WorkspaceError
            {
                return MdwOk();
            } );
}


void registerAttributeHandlers( MDW_BACKEND_SERVER& aServer )
{
    aServer.RegisterHandler<mdw::GetAttributesRequest, mdw::GetAttributesReply>(
            []( const mdw::GetAttributesRequest& aRequest,
                mdw::GetAttributesReply& aReply ) -> ws::WorkspaceError
            {
                ODOO_STATE&                 state = odooState();
                std::lock_guard<std::mutex> lock( state.mutex );

                long long id = 0;

                if( std::optional<ws::WorkspaceError> failure =
                            resolveOrFail( state, aRequest.item_id(), id ) )
                {
                    return *failure;
                }

                ODOO_RPC_RESULT result = state.client.Read(
                        state.model, nlohmann::json::array( { id } ),
                        nlohmann::json::array( { "attributes" } ) );

                if( !result.success || !result.result.is_array() || result.result.empty() )
                    return OdooMapError( result, ws::WE_ITEM_NOT_FOUND );

                std::string raw = fieldString( result.result[0], "attributes" );

                if( raw.empty() )
                    return MdwOk();

                nlohmann::json parsed = nlohmann::json::parse( raw, nullptr, false );

                if( parsed.is_discarded() || !parsed.is_object() )
                    return MdwOk();

                ws::AttributeSet* set = aReply.mutable_attributes();

                for( auto it = parsed.begin(); it != parsed.end(); ++it )
                {
                    ws::AttributeValue* value = set->add_values();
                    value->set_attr_id( it.key() );
                    value->set_value( it.value().is_string() ? it.value().get<std::string>()
                                                             : it.value().dump() );
                }

                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::SetAttributesRequest, mdw::SetAttributesReply>(
            []( const mdw::SetAttributesRequest& aRequest,
                mdw::SetAttributesReply& ) -> ws::WorkspaceError
            {
                ODOO_STATE&                 state = odooState();
                std::lock_guard<std::mutex> lock( state.mutex );

                long long id = 0;

                if( std::optional<ws::WorkspaceError> failure =
                            resolveOrFail( state, aRequest.item_id(), id ) )
                {
                    return *failure;
                }

                nlohmann::json attributes = nlohmann::json::object();

                for( const ws::AttributeValue& value : aRequest.attributes().values() )
                    attributes[value.attr_id()] = value.value();

                nlohmann::json values;
                values["attributes"] = attributes.dump();

                ODOO_RPC_RESULT result =
                        state.client.Write( state.model, nlohmann::json::array( { id } ), values );

                if( !result.success )
                    return OdooMapError( result );

                return MdwOk();
            } );
}


void buildDescriptor( MDW_BACKEND_DESCRIPTOR& aDescriptor )
{
    aDescriptor.info.set_backend_id( "org.kicad.mdw.odoo" );
    aDescriptor.info.set_display_name( "Odoo Workspace" );
    aDescriptor.info.mutable_version()->set_major( 1 );
    aDescriptor.info.mutable_version()->set_minor( 0 );

    aDescriptor.capabilities.add_groups( ws::CG_ATTRIBUTES );

    aDescriptor.schema.set_schema_version( "1" );

    ws::AttributeSchema* owner = aDescriptor.schema.add_attributes();
    owner->set_attr_id( "owner" );
    owner->set_display_name( "Owner" );
    owner->set_type( ws::AT_STRING );

    ws::AttributeSchema* status = aDescriptor.schema.add_attributes();
    status->set_attr_id( "status" );
    status->set_display_name( "Status" );
    status->set_type( ws::AT_STRING );
}

} // namespace


int main( int argc, char** argv )
{
    curl_global_init( CURL_GLOBAL_DEFAULT );

    MDW_BACKEND_LAUNCH launch = MDW_ParseLaunch( argc, argv );

    MDW_BACKEND_DESCRIPTOR descriptor;
    buildDescriptor( descriptor );

    MDW_BACKEND_SERVER server;
    registerBaseHandlers( server );
    registerAttributeHandlers( server );

    int code = MDW_RunBackend( launch, descriptor, server, nullptr );

    curl_global_cleanup();
    return code;
}
