/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2024 Jon Evans <jon@craftyjon.com>
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
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <api/api_handler_sch.h>
#include <api/api_sch_utils.h>
#include <api/api_utils.h>
#include <magic_enum.hpp>
#include <sch_commit.h>
#include <sch_edit_frame.h>
#include <wx/filename.h>
#include <sch_sheet.h>
#include <sch_sheet_pin.h>
#include <project.h>
#include <set>

#include <api/common/types/base_types.pb.h>

using namespace kiapi::common::commands;
using kiapi::common::types::CommandStatus;
using kiapi::common::types::DocumentType;
using kiapi::common::types::ItemRequestStatus;


API_HANDLER_SCH::API_HANDLER_SCH( SCH_EDIT_FRAME* aFrame ) :
        API_HANDLER_EDITOR( aFrame )
{
    registerHandler<GetItems, GetItemsResponse>( &API_HANDLER_SCH::handleGetItems );
    registerHandler<GetItemsById, GetItemsResponse>( &API_HANDLER_SCH::handleGetItemsById );
    registerHandler<GetOpenDocuments, GetOpenDocumentsResponse>(
            &API_HANDLER_SCH::handleGetOpenDocuments );
}


std::unique_ptr<COMMIT> API_HANDLER_SCH::createCommit()
{
    return std::make_unique<SCH_COMMIT>( static_cast<SCH_EDIT_FRAME*>( m_frame ) );
}


bool API_HANDLER_SCH::validateDocumentInternal( const DocumentSpecifier& aDocument ) const
{
    return resolveDocument( aDocument ).has_value();
}


std::optional<SCH_SHEET_PATH> API_HANDLER_SCH::resolveDocument(
        const DocumentSpecifier& aDocument ) const
{
    if( aDocument.type() != DocumentType::DOCTYPE_SCHEMATIC )
        return std::nullopt;

    auto* frame = static_cast<SCH_EDIT_FRAME*>( m_frame );
    const PROJECT& project = frame->Schematic().Project();

    if( ( !aDocument.project().name().empty()
          && aDocument.project().name() != project.GetProjectName().ToStdString() )
        || ( !aDocument.project().path().empty()
             && aDocument.project().path() != project.GetProjectPath().ToStdString() ) )
    {
        return std::nullopt;
    }

    if( aDocument.has_sheet_path() )
    {
        for( const SCH_SHEET_PATH& path : frame->Schematic().Hierarchy() )
        {
            const KIID_PATH ids = path.Path();

            if( ids.size() != size_t( aDocument.sheet_path().path_size() ) )
                continue;

            bool match = true;

            for( size_t i = 0; i < ids.size(); ++i )
                match &= ids[i].AsStdString() == aDocument.sheet_path().path( i ).value();

            if( match )
                return path;
        }

        return std::nullopt;
    }

    if( aDocument.has_lib_id() )
        return std::nullopt;

    if( !aDocument.board_filename().empty() )
    {
        wxFileName current( frame->GetCurrentFileName() );

        if( aDocument.board_filename() != current.GetFullName().ToStdString()
            && aDocument.board_filename() != current.GetFullPath().ToStdString() )
        {
            return std::nullopt;
        }
    }

    return frame->GetCurrentSheet();
}


DocumentSpecifier API_HANDLER_SCH::describeDocument( const SCH_SHEET_PATH& aPath ) const
{
    DocumentSpecifier result;
    result.set_type( DocumentType::DOCTYPE_SCHEMATIC );

    for( const KIID& id : aPath.Path() )
        result.mutable_sheet_path()->add_path()->set_value( id.AsStdString() );

    result.mutable_sheet_path()->set_path_human_readable( aPath.PathHumanReadable().ToStdString() );
    const PROJECT& project = static_cast<SCH_EDIT_FRAME*>( m_frame )->Schematic().Project();
    result.mutable_project()->set_name( project.GetProjectName().ToStdString() );
    result.mutable_project()->set_path( project.GetProjectPath().ToStdString() );
    return result;
}


HANDLER_RESULT<GetOpenDocumentsResponse> API_HANDLER_SCH::handleGetOpenDocuments(
        const HANDLER_CONTEXT<GetOpenDocuments>& aCtx )
{
    if( aCtx.Request.type() != DocumentType::DOCTYPE_SCHEMATIC )
    {
        ApiResponseStatus e;

        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    GetOpenDocumentsResponse response;
    auto* frame = static_cast<SCH_EDIT_FRAME*>( m_frame );
    // Put the active page first; every returned document has an unambiguous UUID path.
    *response.add_documents() = describeDocument( frame->GetCurrentSheet() );

    for( const SCH_SHEET_PATH& path : frame->Schematic().Hierarchy() )
    {
        if( path.Path() != frame->GetCurrentSheet().Path() )
            *response.add_documents() = describeDocument( path );
    }

    return response;
}


HANDLER_RESULT<GetItemsResponse> API_HANDLER_SCH::handleGetItems(
        const HANDLER_CONTEXT<GetItems>& aCtx )
{
    auto path = resolveDocument( aCtx.Request.header().document() );

    if( !path )
    {
        ApiResponseStatus error;
        error.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( error );
    }

    if( auto busy = checkForBusy() )
        return tl::unexpected( *busy );

    const auto& header = aCtx.Request.header();
    std::set<int> requested( aCtx.Request.types().begin(), aCtx.Request.types().end() );

    if( requested.empty() )
        requested.insert( types::KOT_SCH_SHEET );

    if( header.field_mask().paths_size() != 0
        || std::any_of( requested.begin(), requested.end(), []( int type )
                       { return type != types::KOT_SCH_SHEET && type != types::KOT_SCH_SHEET_PIN; } ) )
    {
        ApiResponseStatus error;
        error.set_status( ApiStatusCode::AS_BAD_REQUEST );
        error.set_error_message( "Schematic GetItems currently supports sheets and sheet pins, without field masks" );
        return tl::unexpected( error );
    }

    GetItemsResponse response;
    *response.mutable_header() = header;
    *response.mutable_header()->mutable_document() = describeDocument( *path );
    response.set_status( types::IRS_OK );
    bool foundContainer = header.container().value().empty();

    for( SCH_ITEM* item : path->LastScreen()->Items() )
    {
        if( item->Type() != SCH_SHEET_T )
            continue;

        auto* sheet = static_cast<SCH_SHEET*>( item );

        if( !header.container().value().empty() )
        {
            if( header.container().value() != sheet->m_Uuid.AsStdString() )
                continue;

            foundContainer = true;
        }
        else if( requested.count( types::KOT_SCH_SHEET ) )
        {
            response.add_items()->PackFrom( PackSchematicSheet( *sheet ) );
        }

        if( requested.count( types::KOT_SCH_SHEET_PIN ) )
        {
            for( const SCH_SHEET_PIN* pin : sheet->GetPins() )
                response.add_items()->PackFrom( PackSchematicSheetPin( *pin ) );
        }
    }

    if( !foundContainer )
    {
        ApiResponseStatus error;
        error.set_status( ApiStatusCode::AS_BAD_REQUEST );
        error.set_error_message( "The requested sheet container is not on this schematic page" );
        return tl::unexpected( error );
    }

    return response;
}


HANDLER_RESULT<GetItemsResponse> API_HANDLER_SCH::handleGetItemsById(
        const HANDLER_CONTEXT<GetItemsById>& aCtx )
{
    HANDLER_CONTEXT<GetItems> all;
    all.Request.mutable_header()->CopyFrom( aCtx.Request.header() );
    all.Request.add_types( types::KOT_SCH_SHEET );
    all.Request.add_types( types::KOT_SCH_SHEET_PIN );
    auto result = handleGetItems( all );

    if( !result )
        return result;

    GetItemsResponse response;
    *response.mutable_header() = result->header();
    response.set_status( types::IRS_OK );
    std::set<std::string> ids;

    for( const auto& id : aCtx.Request.items() )
        ids.insert( id.value() );

    for( const auto& item : result->items() )
    {
        kiapi::schematic::types::Sheet sheet;
        kiapi::schematic::types::SheetPin pin;

        if( ( item.UnpackTo( &sheet ) && ids.count( sheet.id().value() ) )
            || ( item.UnpackTo( &pin ) && ids.count( pin.id().value() ) ) )
        {
            *response.add_items() = item;
        }
    }

    return response;
}


HANDLER_RESULT<std::unique_ptr<EDA_ITEM>> API_HANDLER_SCH::createItemForType( KICAD_T aType,
        EDA_ITEM* aContainer )
{
    if( !aContainer )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Tried to create an item in a null container" );
        return tl::unexpected( e );
    }

    if( aType == SCH_PIN_T && !dynamic_cast<SCH_SYMBOL*>( aContainer ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "Tried to create a pin in {}, which is not a symbol",
                                          aContainer->GetFriendlyName().ToStdString() ) );
        return tl::unexpected( e );
    }
    else if( aType == SCH_SYMBOL_T && !dynamic_cast<SCHEMATIC*>( aContainer ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "Tried to create a symbol in {}, which is not a "
                                          "schematic",
                                          aContainer->GetFriendlyName().ToStdString() ) );
        return tl::unexpected( e );
    }

    std::unique_ptr<EDA_ITEM> created = CreateItemForType( aType, aContainer );

    if( !created )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "Tried to create an item of type {}, which is unhandled",
                                          magic_enum::enum_name( aType ) ) );
        return tl::unexpected( e );
    }

    return created;
}


HANDLER_RESULT<ItemRequestStatus> API_HANDLER_SCH::handleCreateUpdateItemsInternal( bool aCreate,
        const std::string& aClientName,
        const types::ItemHeader &aHeader,
        const google::protobuf::RepeatedPtrField<google::protobuf::Any>& aItems,
        std::function<void( ItemStatus, google::protobuf::Any )> aItemHandler )
{
    ApiResponseStatus e;

    auto containerResult = validateItemHeaderDocument( aHeader );

    if( !containerResult && containerResult.error().status() == ApiStatusCode::AS_UNHANDLED )
    {
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }
    else if( !containerResult )
    {
        e.CopyFrom( containerResult.error() );
        return tl::unexpected( e );
    }

    SCH_SCREEN* screen = static_cast<SCH_EDIT_FRAME*>( m_frame )->GetScreen();
    EE_RTREE& screenItems = screen->Items();

    std::map<KIID, EDA_ITEM*> itemUuidMap;

    std::for_each( screenItems.begin(), screenItems.end(),
                   [&]( EDA_ITEM* aItem )
                   {
                       itemUuidMap[aItem->m_Uuid] = aItem;
                   } );

    EDA_ITEM* container = nullptr;

    if( containerResult->has_value() )
    {
        const KIID& containerId = **containerResult;

        if( itemUuidMap.count( containerId ) )
        {
            container = itemUuidMap.at( containerId );

            if( !container )
            {
                e.set_status( ApiStatusCode::AS_BAD_REQUEST );
                e.set_error_message( fmt::format(
                        "The requested container {} is not a valid schematic item container",
                        containerId.AsStdString() ) );
                return tl::unexpected( e );
            }
        }
        else
        {
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( fmt::format(
                    "The requested container {} does not exist in this document",
                    containerId.AsStdString() ) );
            return tl::unexpected( e );
        }
    }

    COMMIT* commit = getCurrentCommit( aClientName );

    for( const google::protobuf::Any& anyItem : aItems )
    {
        ItemStatus status;
        std::optional<KICAD_T> type = TypeNameFromAny( anyItem );

        if( !type )
        {
            status.set_code( ItemStatusCode::ISC_INVALID_TYPE );
            status.set_error_message( fmt::format( "Could not decode a valid type from {}",
                                                   anyItem.type_url() ) );
            aItemHandler( status, anyItem );
            continue;
        }

        HANDLER_RESULT<std::unique_ptr<EDA_ITEM>> creationResult =
                createItemForType( *type, container );

        if( !creationResult )
        {
            status.set_code( ItemStatusCode::ISC_INVALID_TYPE );
            status.set_error_message( creationResult.error().error_message() );
            aItemHandler( status, anyItem );
            continue;
        }

        std::unique_ptr<EDA_ITEM> item( std::move( *creationResult ) );

        if( !item->Deserialize( anyItem ) )
        {
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( fmt::format( "could not unpack {} from request",
                                              item->GetClass().ToStdString() ) );
            return tl::unexpected( e );
        }

        if( aCreate && itemUuidMap.count( item->m_Uuid ) )
        {
            status.set_code( ItemStatusCode::ISC_EXISTING );
            status.set_error_message( fmt::format( "an item with UUID {} already exists",
                                                   item->m_Uuid.AsStdString() ) );
            aItemHandler( status, anyItem );
            continue;
        }
        else if( !aCreate && !itemUuidMap.count( item->m_Uuid ) )
        {
            status.set_code( ItemStatusCode::ISC_NONEXISTENT );
            status.set_error_message( fmt::format( "an item with UUID {} does not exist",
                                                   item->m_Uuid.AsStdString() ) );
            aItemHandler( status, anyItem );
            continue;
        }

        status.set_code( ItemStatusCode::ISC_OK );
        google::protobuf::Any newItem;

        if( aCreate )
        {
            item->Serialize( newItem );
            commit->Add( item.release(), screen );

            if( !m_activeClients.count( aClientName ) )
                pushCurrentCommit( aClientName, _( "Added items via API" ) );
        }
        else
        {
            EDA_ITEM* edaItem = itemUuidMap[item->m_Uuid];

            if( SCH_ITEM* schItem = dynamic_cast<SCH_ITEM*>( edaItem ) )
            {
                schItem->SwapItemData( static_cast<SCH_ITEM*>( item.get() ) );
                schItem->Serialize( newItem );
                commit->Modify( schItem, screen );
            }
            else
            {
                wxASSERT( false );
            }

            if( !m_activeClients.count( aClientName ) )
                pushCurrentCommit( aClientName, _( "Created items via API" ) );
        }

        aItemHandler( status, newItem );
    }


    return ItemRequestStatus::IRS_OK;
}


void API_HANDLER_SCH::deleteItemsInternal( std::map<KIID, ItemDeletionStatus>& aItemsToDelete,
                                           const std::string& aClientName )
{
    // TODO
}


std::optional<EDA_ITEM*> API_HANDLER_SCH::getItemFromDocument( const DocumentSpecifier& aDocument,
                                                               const KIID& aId )
{
    if( !validateDocument( aDocument ) )
        return std::nullopt;

    // TODO

    return std::nullopt;
}
