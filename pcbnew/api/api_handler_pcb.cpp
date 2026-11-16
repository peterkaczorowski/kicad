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
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <magic_enum.hpp>
#include <cmath>
#include <algorithm>
#include <google/protobuf/util/field_mask_util.h>
#include <pcb_draw_panel_gal.h>
#include <pcb_screen.h>
#include <view/view.h>
#include <properties/property.h>

#include <common.h>
#include <api/api_handler_pcb.h>
#include <api/api_pcb_utils.h>
#include <api/api_enums.h>
#include <api/api_utils.h>
#include <board_commit.h>
#include <pcb_io/kicad_sexpr/pcb_io_kicad_sexpr.h>
#include <board_connected_item.h>
#include <board_design_settings.h>
#include <footprint.h>
#include <kicad_clipboard.h>
#include <netinfo.h>
#include <pad.h>
#include <pcb_edit_frame.h>
#include <pcb_group.h>
#include <pcb_reference_image.h>
#include <pcb_shape.h>
#include <pcb_text.h>
#include <pcb_textbox.h>
#include <pcb_track.h>
#include <pcbnew_id.h>
#include <pcb_marker.h>
#include <pcb_point.h>
#include <drc/drc_item.h>
#include <layer_ids.h>
#include <project.h>
#include <string_utils.h>
#include <tool/tool_manager.h>
#include <tools/pcb_actions.h>
#include <tools/pcb_selection_tool.h>
#include <zone.h>

#include <api/common/types/base_types.pb.h>
#include <connectivity/connectivity_data.h>
#include <widgets/appearance_controls.h>
#include <widgets/report_severity.h>

using namespace kiapi::common::commands;
using types::CommandStatus;
using types::DocumentType;
using types::ItemRequestStatus;


namespace
{
// API transactions expose staged top-level items immediately, just like GUI edits.
// CHT_DONE lets native BOARD_COMMIT own Push/Revert/Undo without applying them twice.
class PCB_API_COMMIT : public BOARD_COMMIT
{
public:
    explicit PCB_API_COMMIT( PCB_EDIT_FRAME* frame ) : BOARD_COMMIT( frame ), m_frame( frame ),
            m_wasModified( frame->GetScreen()->IsContentModified() ) {}
    void Revert() override
    {
        BOARD_COMMIT::Revert();
        m_frame->GetScreen()->SetContentModified( m_wasModified );
        m_frame->Refresh();
    }
    COMMIT& Stage( EDA_ITEM* item, CHANGE_TYPE change, BASE_SCREEN* screen = nullptr,
                   RECURSE_MODE recurse = RECURSE_MODE::NO_RECURSE ) override
    {
        auto* boardItem = dynamic_cast<BOARD_ITEM*>( item );
        if( boardItem && !boardItem->GetParentFootprint() && !( change & CHT_DONE ) )
        {
            auto* board = m_frame->GetBoard();
            auto* view = m_frame->GetCanvas()->GetView();
            if( ( change & CHT_TYPE ) == CHT_ADD )
            {
                board->Add( boardItem );
                if( boardItem->Type() != PCB_NETINFO_T ) view->Add( boardItem );
                change = change | CHT_DONE;
            }
            else if( ( change & CHT_TYPE ) == CHT_REMOVE )
            {
                if( boardItem->Type() != PCB_NETINFO_T ) view->Remove( boardItem );
                board->Remove( boardItem );
                change = change | CHT_DONE;
            }
        }
        return BOARD_COMMIT::Stage( item, change, screen, recurse );
    }
private:
    PCB_EDIT_FRAME* m_frame;
    bool m_wasModified;
};

// Nets are made available to native Deserialize during this synchronous request.
// Rejected items remove them; accepted items transfer them to the native commit.
class PENDING_API_NETS
{
public:
    PENDING_API_NETS( BOARD& board, const std::set<std::string>& names ) : m_board( board )
    {
        for( const auto& name : names )
        {
            auto* net = new NETINFO_ITEM( &board, wxString::FromUTF8( name ), 0 );
            m_nets.push_back( net ); board.Add( net );
        }
    }
    ~PENDING_API_NETS()
    {
        for( auto* net : m_nets ) { m_board.Remove( net ); delete net; }
    }
    void Accept( BOARD_COMMIT& commit )
    {
        for( auto* net : m_nets ) commit.Added( net );
        m_nets.clear();
    }
private:
    BOARD& m_board;
    std::vector<NETINFO_ITEM*> m_nets;
};
}


API_HANDLER_PCB::API_HANDLER_PCB( PCB_EDIT_FRAME* aFrame ) :
        API_HANDLER_EDITOR( aFrame )
{
    registerIntegrationHandlers();
    registerHandler<GetOpenDocuments, GetOpenDocumentsResponse>(
            &API_HANDLER_PCB::handleGetOpenDocuments );

    registerHandler<GetItems, GetItemsResponse>( &API_HANDLER_PCB::handleGetItems );
    registerHandler<GetItemsById, GetItemsResponse>( &API_HANDLER_PCB::handleGetItemsById );

    registerHandler<GetSelection, SelectionResponse>( &API_HANDLER_PCB::handleGetSelection );
    registerHandler<ClearSelection, Empty>( &API_HANDLER_PCB::handleClearSelection );
    registerHandler<AddToSelection, SelectionResponse>( &API_HANDLER_PCB::handleAddToSelection );
    registerHandler<RemoveFromSelection, SelectionResponse>(
            &API_HANDLER_PCB::handleRemoveFromSelection );

    registerHandler<GetBoardStackup, BoardStackupResponse>( &API_HANDLER_PCB::handleGetStackup );
    registerHandler<GetBoardEnabledLayers, BoardEnabledLayersResponse>(
        &API_HANDLER_PCB::handleGetBoardEnabledLayers );
    registerHandler<SetBoardEnabledLayers, BoardEnabledLayersResponse>(
        &API_HANDLER_PCB::handleSetBoardEnabledLayers );
    registerHandler<GetEmbeddedFiles, common::types::EmbeddedFiles>( &API_HANDLER_PCB::handleGetEmbeddedFiles );
    registerHandler<AddEmbeddedFiles, Empty>( &API_HANDLER_PCB::handleAddEmbeddedFiles );
    registerHandler<SetEmbeddedFiles, Empty>( &API_HANDLER_PCB::handleSetEmbeddedFiles );
    registerHandler<GetGraphicsDefaults, GraphicsDefaultsResponse>(
            &API_HANDLER_PCB::handleGetGraphicsDefaults );
    registerHandler<GetBoundingBox, GetBoundingBoxResponse>(
            &API_HANDLER_PCB::handleGetBoundingBox );
    registerHandler<GetPadShapeAsPolygon, PadShapeAsPolygonResponse>(
            &API_HANDLER_PCB::handleGetPadShapeAsPolygon );
    registerHandler<CheckPadstackPresenceOnLayers, PadstackPresenceResponse>(
            &API_HANDLER_PCB::handleCheckPadstackPresenceOnLayers );
    registerHandler<GetTitleBlockInfo, types::TitleBlockInfo>(
            &API_HANDLER_PCB::handleGetTitleBlockInfo );
    registerHandler<SetTitleBlockInfo, Empty>( &API_HANDLER_PCB::handleSetTitleBlockInfo );
    registerHandler<ExpandTextVariables, ExpandTextVariablesResponse>(
            &API_HANDLER_PCB::handleExpandTextVariables );
    registerHandler<GetBoardOrigin, types::Vector2>( &API_HANDLER_PCB::handleGetBoardOrigin );
    registerHandler<SetBoardOrigin, Empty>( &API_HANDLER_PCB::handleSetBoardOrigin );
    registerHandler<GetBoardLayerName, BoardLayerNameResponse>( &API_HANDLER_PCB::handleGetBoardLayerName );
    registerHandler<GetBoardLayerByName, BoardLayerResponse>( &API_HANDLER_PCB::handleGetBoardLayerByName );

    registerHandler<InteractiveMoveItems, Empty>( &API_HANDLER_PCB::handleInteractiveMoveItems );
    registerHandler<FlipItems, FlipItemsResponse>( &API_HANDLER_PCB::handleFlipItems );
    registerHandler<GetNets, NetsResponse>( &API_HANDLER_PCB::handleGetNets );
    registerHandler<GetConnectedItems, GetItemsResponse>( &API_HANDLER_PCB::handleGetConnectedItems );
    registerHandler<GetItemsByNet, GetItemsResponse>( &API_HANDLER_PCB::handleGetItemsByNet );
    registerHandler<GetItemsByNetClass, GetItemsResponse>( &API_HANDLER_PCB::handleGetItemsByNetClass );
    registerHandler<GetNetClassForNets, NetClassForNetsResponse>(
            &API_HANDLER_PCB::handleGetNetClassForNets );
    registerHandler<RefillZones, Empty>( &API_HANDLER_PCB::handleRefillZones );

    registerHandler<SaveDocumentToString, SavedDocumentResponse>(
            &API_HANDLER_PCB::handleSaveDocumentToString );
    registerHandler<SaveSelectionToString, SavedSelectionResponse>(
            &API_HANDLER_PCB::handleSaveSelectionToString );
    registerHandler<ParseAndCreateItemsFromString, CreateItemsResponse>(
            &API_HANDLER_PCB::handleParseAndCreateItemsFromString );
    registerHandler<GetVisibleLayers, BoardLayers>( &API_HANDLER_PCB::handleGetVisibleLayers );
    registerHandler<SetVisibleLayers, Empty>( &API_HANDLER_PCB::handleSetVisibleLayers );
    registerHandler<GetActiveLayer, BoardLayerResponse>( &API_HANDLER_PCB::handleGetActiveLayer );
    registerHandler<SetActiveLayer, Empty>( &API_HANDLER_PCB::handleSetActiveLayer );
    registerHandler<GetBoardEditorAppearanceSettings, BoardEditorAppearanceSettings>(
            &API_HANDLER_PCB::handleGetBoardEditorAppearanceSettings );
    registerHandler<SetBoardEditorAppearanceSettings, Empty>(
            &API_HANDLER_PCB::handleSetBoardEditorAppearanceSettings );
    registerHandler<InjectDrcError, InjectDrcErrorResponse>(
            &API_HANDLER_PCB::handleInjectDrcError );

    registerHandler<GetVariants, VariantsResponse>( &API_HANDLER_PCB::handleGetVariants );
    registerHandler<AddVariant, Empty>( &API_HANDLER_PCB::handleAddVariant );
    registerHandler<DeleteVariant, Empty>( &API_HANDLER_PCB::handleDeleteVariant );
    registerHandler<RenameVariant, Empty>( &API_HANDLER_PCB::handleRenameVariant );
    registerHandler<CopyVariant, Empty>( &API_HANDLER_PCB::handleCopyVariant );
    registerHandler<SetVariantDescription, Empty>( &API_HANDLER_PCB::handleSetVariantDescription );
    registerHandler<SetCurrentVariant, Empty>( &API_HANDLER_PCB::handleSetCurrentVariant );
    registerHandler<GetCurrentVariant, CurrentVariantResponse>( &API_HANDLER_PCB::handleGetCurrentVariant );
}


PCB_EDIT_FRAME* API_HANDLER_PCB::frame() const
{
    return static_cast<PCB_EDIT_FRAME*>( m_frame );
}


HANDLER_RESULT<GetOpenDocumentsResponse> API_HANDLER_PCB::handleGetOpenDocuments(
        const HANDLER_CONTEXT<GetOpenDocuments>& aCtx )
{
    if( aCtx.Request.type() != DocumentType::DOCTYPE_PCB )
    {
        ApiResponseStatus e;
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    GetOpenDocumentsResponse response;
    common::types::DocumentSpecifier doc;

    wxFileName fn( frame()->GetCurrentFileName() );

    doc.set_type( DocumentType::DOCTYPE_PCB );
    doc.set_board_filename( fn.GetFullName() );

    doc.mutable_project()->set_name( frame()->Prj().GetProjectName().ToStdString() );
    doc.mutable_project()->set_path( frame()->Prj().GetProjectDirectory().ToStdString() );

    response.mutable_documents()->Add( std::move( doc ) );
    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::saveDocumentInternal( const DocumentSpecifier& )
{
    wxFileName path( frame()->Prj().AbsolutePath( frame()->GetBoard()->GetFileName() ) );
    if( !path.IsOk() || path.GetExt() != "kicad_pcb" || !path.IsDirWritable()
        || ( path.FileExists() && !path.IsFileWritable() ) )
        return documentError( "Board needs an existing writable native save path" );
    // The native writer propagates IO_ERROR rather than showing a dialog.
    PCB_IO_KICAD_SEXPR io;
    io.SaveBoard( path.GetFullPath(), frame()->GetBoard() );
    frame()->GetScreen()->SetContentModified( false );
    frame()->UpdateTitle();
    return Empty();
}

HANDLER_RESULT<Empty> API_HANDLER_PCB::saveCopyInternal(
        const DocumentSpecifier&, const wxString& aPath, bool aIncludeProject )
{
    if( wxFileName( aPath ) == wxFileName( frame()->Prj().AbsolutePath( frame()->GetBoard()->GetFileName() ) ) )
        return documentError( "SaveCopy destination must differ from the open document; use SaveDocument" );
    if( !frame()->SavePcbCopy( aPath, aIncludeProject, /* aHeadless */ true ) )
        return documentError( "Could not save board copy" );
    return Empty();
}

HANDLER_RESULT<Empty> API_HANDLER_PCB::revertDocumentInternal( const DocumentSpecifier& )
{
    wxFileName path( frame()->Prj().AbsolutePath( frame()->GetBoard()->GetFileName() ) );
    if( !path.FileExists() || !path.IsFileReadable() )
        return documentError( "Board file cannot be read" );
    // Parse before touching the live model or its dirty flags.
    PCB_IO_KICAD_SEXPR io;
    std::unique_ptr<BOARD> parsed( io.LoadBoard( path.GetFullPath(), nullptr, nullptr, &frame()->Prj() ) );
    if( !parsed )
        return documentError( "Board could not be loaded" );
    frame()->GetScreen()->SetContentModified( false );
    frame()->ReleaseFile();
    if( !frame()->OpenProjectFiles( { path.GetFullPath() }, KICTL_REVERT ) )
        return documentError( "Board could not be reloaded" );
    return Empty();
}


void API_HANDLER_PCB::pushCurrentCommit( const std::string& aClientName, const wxString& aMessage )
{
    API_HANDLER_EDITOR::pushCurrentCommit( aClientName, aMessage );
    frame()->Refresh();
}


std::unique_ptr<COMMIT> API_HANDLER_PCB::createCommit()
{
    return std::make_unique<PCB_API_COMMIT>( frame() );
}


std::optional<BOARD_ITEM*> API_HANDLER_PCB::getItemById( const KIID& aId ) const
{
    BOARD_ITEM* item = frame()->GetBoard()->ResolveItem( aId, true );

    if( !item )
        return std::nullopt;

    return item;
}


bool API_HANDLER_PCB::validateDocumentInternal( const DocumentSpecifier& aDocument ) const
{
    if( aDocument.type() != DocumentType::DOCTYPE_PCB )
        return false;

    wxFileName fn( frame()->GetCurrentFileName() );
    return aDocument.board_filename() == fn.GetFullName().ToStdString()
           && ( aDocument.project().path().empty()
                || wxFileName::DirName( wxString::FromUTF8( aDocument.project().path() ) )
                           == wxFileName::DirName( frame()->Prj().GetProjectDirectory() ) )
           && ( aDocument.project().name().empty()
                || aDocument.project().name() == frame()->Prj().GetProjectName().ToStdString() );
}


HANDLER_RESULT<std::unique_ptr<BOARD_ITEM>> API_HANDLER_PCB::createItemForType( KICAD_T aType,
        BOARD_ITEM_CONTAINER* aContainer )
{
    if( !aContainer )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Tried to create an item in a null container" );
        return tl::unexpected( e );
    }

    if( aType == PCB_PAD_T && !dynamic_cast<FOOTPRINT*>( aContainer ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "Tried to create a pad in {}, which is not a footprint",
                                          aContainer->GetFriendlyName().ToStdString() ) );
        return tl::unexpected( e );
    }
    else if( aType == PCB_FOOTPRINT_T && !dynamic_cast<BOARD*>( aContainer ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "Tried to create a footprint in {}, which is not a board",
                                          aContainer->GetFriendlyName().ToStdString() ) );
        return tl::unexpected( e );
    }

    std::unique_ptr<BOARD_ITEM> created = CreateItemForType( aType, aContainer );

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


HANDLER_RESULT<ItemRequestStatus> API_HANDLER_PCB::handleCreateUpdateItemsInternal( bool aCreate,
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

    BOARD* board = frame()->GetBoard();
    BOARD_ITEM_CONTAINER* container = board;

    if( containerResult->has_value() )
    {
        const KIID& containerId = **containerResult;
        std::optional<BOARD_ITEM*> optItem = getItemById( containerId );

        if( optItem )
        {
            container = dynamic_cast<BOARD_ITEM_CONTAINER*>( *optItem );

            if( !container )
            {
                e.set_status( ApiStatusCode::AS_BAD_REQUEST );
                e.set_error_message( fmt::format(
                        "The requested container {} is not a valid board item container",
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

    BOARD_COMMIT* commit = static_cast<BOARD_COMMIT*>( getCurrentCommit( aClientName ) );
    // An implicit batch must never strand a half-applied commit on a top-level
    // parse/mask error. Explicit transactions remain owned by the caller/Drop.
    struct FailureGuard
    {
        std::function<void()> action;
        ~FailureGuard() { if( action ) action(); }
    } failure{ [&]
    {
        if( !m_activeClients.count( aClientName ) && m_commits.count( aClientName ) )
        { commit->Revert(); m_commits.erase( aClientName ); }
    } };

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

        if( type == PCB_DIMENSION_T )
        {
            board::types::Dimension dimension;
            anyItem.UnpackTo( &dimension );

            switch( dimension.dimension_style_case() )
            {
            case board::types::Dimension::kAligned:    type = PCB_DIM_ALIGNED_T;    break;
            case board::types::Dimension::kOrthogonal: type = PCB_DIM_ORTHOGONAL_T; break;
            case board::types::Dimension::kRadial:     type = PCB_DIM_RADIAL_T;     break;
            case board::types::Dimension::kLeader:     type = PCB_DIM_LEADER_T;     break;
            case board::types::Dimension::kCenter:     type = PCB_DIM_CENTER_T;     break;
            case board::types::Dimension::DIMENSION_STYLE_NOT_SET: break;
            }
        }

        google::protobuf::Any full = anyItem;
        if( !aCreate && aHeader.field_mask().paths_size() )
        {
            const auto* descriptor = google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
                    anyItem.type_url().substr( anyItem.type_url().find_last_of( '/' ) + 1 ) );
            const auto* prototype = descriptor ? google::protobuf::MessageFactory::generated_factory()->GetPrototype( descriptor ) : nullptr;
            if( !prototype ) return documentError( "Unknown PCB protobuf type" );
            std::unique_ptr<google::protobuf::Message> patch( prototype->New() ), merged( prototype->New() );
            if( !anyItem.UnpackTo( patch.get() ) ) return documentError( "Malformed PCB update" );
            const auto* idField = descriptor->FindFieldByName( "id" );
            if( !idField || idField->message_type()->full_name() != "kiapi.common.types.KIID" )
                return documentError( "PCB item requires an immutable UUID" );
            const auto& id = patch->GetReflection()->GetMessage( *patch, idField );
            const std::string uuid = id.GetReflection()->GetString( id, id.GetDescriptor()->FindFieldByName( "value" ) );
            auto original = getItemById( KIID( uuid ) );
            if( !original ) return documentError( "PCB update UUID does not exist" );
            google::protobuf::Any old; ( *original )->Serialize( old );
            if( !old.UnpackTo( merged.get() ) ) return documentError( "PCB update type does not match existing item" );
            for( const auto& path : aHeader.field_mask().paths() )
                if( path == "id" || path.rfind( "id.", 0 ) == 0 || path == "parent" || path.rfind( "parent.", 0 ) == 0 )
                    return documentError( "PCB UUID and parent are immutable" );
            for( const auto& path : aHeader.field_mask().paths() )
                if( !google::protobuf::util::FieldMaskUtil::GetFieldDescriptors( descriptor, path, nullptr ) )
                    return documentError( "Invalid PCB field mask" );
            google::protobuf::util::FieldMaskUtil::MergeOptions options;
            options.set_replace_repeated_fields( true );
            google::protobuf::util::FieldMaskUtil::MergeMessageTo( *patch, aHeader.field_mask(), options, merged.get() );
            full.PackFrom( *merged );
        }
        // Footprint protobuf children use absolute coordinates. A masked transform
        // must move the native footprint, not deserialize its old child positions
        // into a new anchor (which would leave the pads and fields behind).
        if( !aCreate && aHeader.field_mask().paths_size() && full.Is<kiapi::board::types::FootprintInstance>() )
        {
            bool transformOnly = true;
            for( const auto& path : aHeader.field_mask().paths() )
            {
                const auto root = path.substr( 0, path.find( '.' ) );
                if( root != "position" && root != "orientation" && root != "layer" && root != "locked" ) transformOnly = false;
            }
            if( transformOnly )
            {
                kiapi::board::types::FootprintInstance value;
                if( !full.UnpackTo( &value ) ) return documentError( "Malformed footprint transform" );
                auto found = getItemById( KIID( value.id().value() ) );
                auto* footprint = found ? dynamic_cast<FOOTPRINT*>( *found ) : nullptr;
                const auto side = FromProtoEnum<PCB_LAYER_ID, kiapi::board::types::BoardLayer>( value.layer() );
                if( !footprint || container != board || ( side != F_Cu && side != B_Cu )
                    || !board->GetEnabledLayers().test( side ) || !std::isfinite( value.orientation().value_degrees() )
                    || value.position().x_nm() < -INT_MAX / 4 || value.position().x_nm() > INT_MAX / 4
                    || value.position().y_nm() < -INT_MAX / 4 || value.position().y_nm() > INT_MAX / 4 )
                    return documentError( "Invalid footprint transform" );
                commit->Modify( footprint );
                footprint->SetPosition( UnpackVector2( value.position() ) );
                footprint->SetLayerAndFlip( side );
                footprint->SetOrientation( EDA_ANGLE( value.orientation().value_degrees(), DEGREES_T ) );
                footprint->SetLocked( value.locked() == kiapi::common::types::LS_LOCKED );
                board->GetConnectivity()->Update( footprint );
                frame()->GetCanvas()->GetView()->Update( footprint );
                google::protobuf::Any transformed; footprint->Serialize( transformed );
                status.set_code( ItemStatusCode::ISC_OK ); aItemHandler( status, transformed );
                continue;
            }
        }
        std::set<std::string> missing;
        std::string netError;
        if( !kiapi::board::ValidateNetReferences( full, *board, aHeader.create_net_if_missing(), missing, netError ) )
        {
            status.set_code( ItemStatusCode::ISC_INVALID_DATA ); status.set_error_message( netError );
            aItemHandler( status, anyItem ); continue;
        }
        PENDING_API_NETS pendingNets( *board, missing );
        HANDLER_RESULT<std::unique_ptr<BOARD_ITEM>> creationResult =
                createItemForType( *type, container );

        if( !creationResult )
        {
            status.set_code( ItemStatusCode::ISC_INVALID_TYPE );
            status.set_error_message( creationResult.error().error_message() );
            aItemHandler( status, anyItem );
            continue;
        }

        std::unique_ptr<BOARD_ITEM> item( std::move( *creationResult ) );

        if( !item->Deserialize( full ) )
        {
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( fmt::format( "could not unpack {} from request",
                                              item->GetClass().ToStdString() ) );
            return tl::unexpected( e );
        }

        std::optional<BOARD_ITEM*> optItem = getItemById( item->m_Uuid );

        if( aCreate && optItem )
        {
            status.set_code( ItemStatusCode::ISC_EXISTING );
            status.set_error_message( fmt::format( "an item with UUID {} already exists",
                                                   item->m_Uuid.AsStdString() ) );
            aItemHandler( status, anyItem );
            continue;
        }
        else if( !aCreate && !optItem )
        {
            status.set_code( ItemStatusCode::ISC_NONEXISTENT );
            status.set_error_message( fmt::format( "an item with UUID {} does not exist",
                                                   item->m_Uuid.AsStdString() ) );
            aItemHandler( status, anyItem );
            continue;
        }

        const bool layerless = item->Type() == PCB_GROUP_T || item->Type() == PCB_GENERATOR_T;
        if( ( !layerless && ( item->GetLayer() < 0 || item->GetLayer() >= PCB_LAYER_ID_COUNT ) )
            || ( ( item->Type() == PCB_TRACE_T || item->Type() == PCB_ARC_T || item->Type() == PCB_VIA_T )
                 && !IsCopperLayer( item->GetLayer() ) )
            || ( item->Type() == PCB_FOOTPRINT_T && item->GetLayer() != F_Cu && item->GetLayer() != B_Cu ) )
        {
            status.set_code( ItemStatusCode::ISC_INVALID_DATA );
            status.set_error_message( "Invalid PCB layer; copper items require copper and footprints require an outer side" );
            aItemHandler( status, anyItem ); continue;
        }
        if( auto* track = dynamic_cast<PCB_TRACK*>( item.get() ); track && track->GetWidth() <= 0 )
        {
            status.set_code( ItemStatusCode::ISC_INVALID_DATA ); status.set_error_message( "Copper width must be positive" );
            aItemHandler( status, anyItem ); continue;
        }
        if( aCreate && !( board->GetEnabledLayers() & item->GetLayerSet() ).any() )
        {
            status.set_code( ItemStatusCode::ISC_INVALID_DATA );
            status.set_error_message(
                "attempted to add item with no overlapping layers with the board" );
            aItemHandler( status, anyItem );
            continue;
        }

        if( !aCreate && ( *optItem )->Type() != item->Type() )
            return documentError( "Cannot change PCB item type" );
        if( !aCreate && !aHeader.allow_net_mismatch() )
        {
            bool changedNet = false;
            if( auto* oldPad = dynamic_cast<PAD*>( *optItem ) )
                changedNet = oldPad->GetNetname() != static_cast<PAD*>( item.get() )->GetNetname();
            if( auto* oldFootprint = dynamic_cast<FOOTPRINT*>( *optItem ) )
            {
                for( PAD* pad : static_cast<FOOTPRINT*>( item.get() )->Pads() )
                    for( PAD* oldPad : oldFootprint->Pads() )
                        if( oldPad->GetNumber() == pad->GetNumber() && oldPad->GetNetname() != pad->GetNetname() )
                            changedNet = true;
            }
            if( changedNet )
            {
                status.set_code( ItemStatusCode::ISC_INVALID_DATA );
                status.set_error_message( "Pad net reassignment is protected; update from schematic or explicitly set allow_net_mismatch" );
                aItemHandler( status, anyItem ); continue;
            }
        }
        pendingNets.Accept( *commit );
        status.set_code( ItemStatusCode::ISC_OK );
        google::protobuf::Any newItem;

        if( aCreate )
        {
            if( item->Type() == PCB_FOOTPRINT_T )
            {
                // Ensure children have unique identifiers; in case the API client created this new
                // footprint by cloning an existing one and only changing the parent UUID.
                item->RunOnChildren(
                        []( BOARD_ITEM* aChild )
                        {
                            aChild->ResetUuid();
                        },
                        RECURSE );
            }

            item->Serialize( newItem );
            commit->Add( item.release() );
        }
        else
        {
            BOARD_ITEM* boardItem = *optItem;

            // Footprints can't be modified by CopyFrom at the moment because the commit system
            // doesn't currently know what to do with a footprint that has had its children
            // replaced with other children; which results in things like the view not having its
            // cached geometry for footprint children updated when you move a footprint around.
            // And also, groups are special because they can contain any item type, so we
            // can't use CopyFrom on them either.
            if( boardItem->Type() == PCB_FOOTPRINT_T  || boardItem->Type() == PCB_GROUP_T )
            {
                // Save group membership before removal, since Remove() severs the relationship
                PCB_GROUP* parentGroup = dynamic_cast<PCB_GROUP*>( boardItem->GetParentGroup() );

                commit->Remove( boardItem );
                item->Serialize( newItem );

                BOARD_ITEM* newBoardItem = item.release();
                commit->Add( newBoardItem );

                // Restore group membership for the newly added item
                if( parentGroup )
                    parentGroup->AddItem( newBoardItem );
            }
            else
            {
                commit->Modify( boardItem );
                boardItem->CopyFrom( item.get() );
                board->GetConnectivity()->Update( boardItem );
                frame()->GetCanvas()->GetView()->Update( boardItem );
                boardItem->Serialize( newItem );
            }
        }

        aItemHandler( status, newItem );
    }

    if( !m_activeClients.count( aClientName ) )
    {
        pushCurrentCommit( aClientName, aCreate ? _( "Created items via API" )
                                                : _( "Modified items via API" ) );
    }


    failure.action = {};
    return ItemRequestStatus::IRS_OK;
}


HANDLER_RESULT<GetItemsResponse> API_HANDLER_PCB::handleGetItems( const HANDLER_CONTEXT<GetItems>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    GetItemsResponse response;

    BOARD* board = frame()->GetBoard();
    std::vector<BOARD_ITEM*> items;
    std::set<KICAD_T> typesRequested, typesInserted;
    bool handledAnything = false;

    for( KICAD_T type : parseRequestedItemTypes( aCtx.Request.types() ) )
    {
        typesRequested.emplace( type );

        if( typesInserted.count( type ) )
            continue;

        switch( type )
        {
        case PCB_TRACE_T:
        case PCB_ARC_T:
        case PCB_VIA_T:
            handledAnything = true;
            std::copy( board->Tracks().begin(), board->Tracks().end(),
                       std::back_inserter( items ) );
            typesInserted.insert( { PCB_TRACE_T, PCB_ARC_T, PCB_VIA_T } );
            break;

        case PCB_PAD_T:
        {
            handledAnything = true;

            for( FOOTPRINT* fp : board->Footprints() )
            {
                std::copy( fp->Pads().begin(), fp->Pads().end(),
                           std::back_inserter( items ) );
            }

            typesInserted.insert( PCB_PAD_T );
            break;
        }

        case PCB_FOOTPRINT_T:
        {
            handledAnything = true;

            std::copy( board->Footprints().begin(), board->Footprints().end(),
                       std::back_inserter( items ) );

            typesInserted.insert( PCB_FOOTPRINT_T );
            break;
        }

        case PCB_SHAPE_T:
        case PCB_TABLE_T:
        case PCB_TEXT_T:
        case PCB_TEXTBOX_T:
        case PCB_BARCODE_T:
        case PCB_REFERENCE_IMAGE_T:
        {
            handledAnything = true;
            bool inserted = false;

            for( BOARD_ITEM* item : board->Drawings() )
            {
                if( item->Type() == type )
                {
                    items.emplace_back( item );
                    inserted = true;
                }
            }

            if( inserted )
                typesInserted.insert( type );

            break;
        }

        case PCB_DIMENSION_T:
        {
            handledAnything = true;
            bool inserted = false;

            for( BOARD_ITEM* item : board->Drawings() )
            {
                switch (item->Type()) {
                    case PCB_DIM_ALIGNED_T:
                    case PCB_DIM_CENTER_T:
                    case PCB_DIM_RADIAL_T:
                    case PCB_DIM_ORTHOGONAL_T:
                    case PCB_DIM_LEADER_T:
                        items.emplace_back( item );
                        inserted = true;
                        break;
                    default:
                        break;
                }
            }
            // we have to add the dimension subtypes to the requested to get them out
            typesRequested.insert( {PCB_DIM_ALIGNED_T, PCB_DIM_CENTER_T, PCB_DIM_RADIAL_T, PCB_DIM_ORTHOGONAL_T, PCB_DIM_LEADER_T } );

            if( inserted )
                typesInserted.insert( {PCB_DIM_ALIGNED_T, PCB_DIM_CENTER_T, PCB_DIM_RADIAL_T, PCB_DIM_ORTHOGONAL_T, PCB_DIM_LEADER_T } );

            break;
        }

        case PCB_ZONE_T:
        {
            handledAnything = true;

            std::copy( board->Zones().begin(), board->Zones().end(),
                       std::back_inserter( items ) );

            typesInserted.insert( PCB_ZONE_T );
            break;
        }

        case PCB_GROUP_T:
        {
            handledAnything = true;

            std::copy( board->Groups().begin(), board->Groups().end(),
                       std::back_inserter( items ) );

            typesInserted.insert( PCB_GROUP_T );
            break;
        }

        case PCB_POINT_T:
        {
            handledAnything = true;
            std::copy( board->Points().begin(), board->Points().end(), std::back_inserter( items ) );
            typesInserted.insert( PCB_POINT_T );
            break;
        }

        default:
            break;
        }
    }

    if( !handledAnything )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "none of the requested types are valid for a Board object" );
        return tl::unexpected( e );
    }

    for( const BOARD_ITEM* item : items )
    {
        if( !typesRequested.count( item->Type() ) )
            continue;

        google::protobuf::Any itemBuf;
        item->Serialize( itemBuf );
        response.mutable_items()->Add( std::move( itemBuf ) );
    }

    response.set_status( ItemRequestStatus::IRS_OK );
    return response;
}


HANDLER_RESULT<GetItemsResponse> API_HANDLER_PCB::handleGetItemsById(
        const HANDLER_CONTEXT<GetItemsById>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    GetItemsResponse response;

    std::vector<BOARD_ITEM*> items;

    for( const kiapi::common::types::KIID& id : aCtx.Request.items() )
    {
        if( std::optional<BOARD_ITEM*> item = getItemById( KIID( id.value() ) ) )
            items.emplace_back( *item );
    }

    if( items.empty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "none of the requested IDs were found or valid" );
        return tl::unexpected( e );
    }

    for( const BOARD_ITEM* item : items )
    {
        google::protobuf::Any itemBuf;
        item->Serialize( itemBuf );
        response.mutable_items()->Add( std::move( itemBuf ) );
    }

    response.set_status( ItemRequestStatus::IRS_OK );
    return response;
}

void API_HANDLER_PCB::deleteItemsInternal( const DocumentSpecifier& aDocument,
                              std::map<KIID, ItemDeletionStatus>& aItemsToDelete,
                                           const std::string& aClientName )
{
    BOARD* board = frame()->GetBoard();
    std::vector<BOARD_ITEM*> validatedItems;

    for( std::pair<const KIID, ItemDeletionStatus> pair : aItemsToDelete )
    {
        if( BOARD_ITEM* item = board->ResolveItem( pair.first, true ) )
        {
            validatedItems.push_back( item );
            aItemsToDelete[pair.first] = ItemDeletionStatus::IDS_OK;
        }

        // Note: we don't currently support locking items from API modification, but here is where
        // to add it in the future (and return IDS_IMMUTABLE)
    }

    COMMIT* commit = getCurrentCommit( aClientName );

    for( BOARD_ITEM* item : validatedItems )
        commit->Remove( item );

    if( !m_activeClients.count( aClientName ) )
        pushCurrentCommit( aClientName, _( "Deleted items via API" ) );
}


std::optional<EDA_ITEM*> API_HANDLER_PCB::getItemFromDocument( const DocumentSpecifier& aDocument,
                                                               const KIID& aId )
{
    if( !validateDocument( aDocument ) )
        return std::nullopt;

    return getItemById( aId );
}


HANDLER_RESULT<SelectionResponse> API_HANDLER_PCB::handleGetSelection(
            const HANDLER_CONTEXT<GetSelection>& aCtx )
{
    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    std::set<KICAD_T> filter;

    for( KICAD_T type : parseRequestedItemTypes( aCtx.Request.types() ) )
        filter.insert( type );

    TOOL_MANAGER* mgr = frame()->GetToolManager();
    PCB_SELECTION_TOOL* selectionTool = mgr->GetTool<PCB_SELECTION_TOOL>();

    SelectionResponse response;

    for( EDA_ITEM* item : selectionTool->GetSelection() )
    {
        if( filter.empty() || filter.contains( item->Type() ) )
            item->Serialize( *response.add_items() );
    }

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleClearSelection(
        const HANDLER_CONTEXT<ClearSelection>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    TOOL_MANAGER* mgr = frame()->GetToolManager();
    mgr->RunAction( ACTIONS::selectionClear );
    frame()->Refresh();

    return Empty();
}


HANDLER_RESULT<SelectionResponse> API_HANDLER_PCB::handleAddToSelection(
        const HANDLER_CONTEXT<AddToSelection>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    TOOL_MANAGER* mgr = frame()->GetToolManager();
    PCB_SELECTION_TOOL* selectionTool = mgr->GetTool<PCB_SELECTION_TOOL>();

    std::vector<EDA_ITEM*> toAdd;

    for( const types::KIID& id : aCtx.Request.items() )
    {
        if( std::optional<BOARD_ITEM*> item = getItemById( KIID( id.value() ) ) )
            toAdd.emplace_back( *item );
    }

    selectionTool->AddItemsToSel( &toAdd );
    frame()->Refresh();

    SelectionResponse response;

    for( EDA_ITEM* item : selectionTool->GetSelection() )
        item->Serialize( *response.add_items() );

    return response;
}


HANDLER_RESULT<SelectionResponse> API_HANDLER_PCB::handleRemoveFromSelection(
        const HANDLER_CONTEXT<RemoveFromSelection>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    TOOL_MANAGER* mgr = frame()->GetToolManager();
    PCB_SELECTION_TOOL* selectionTool = mgr->GetTool<PCB_SELECTION_TOOL>();

    std::vector<EDA_ITEM*> toRemove;

    for( const types::KIID& id : aCtx.Request.items() )
    {
        if( std::optional<BOARD_ITEM*> item = getItemById( KIID( id.value() ) ) )
            toRemove.emplace_back( *item );
    }

    selectionTool->RemoveItemsFromSel( &toRemove );
    frame()->Refresh();

    SelectionResponse response;

    for( EDA_ITEM* item : selectionTool->GetSelection() )
        item->Serialize( *response.add_items() );

    return response;
}


HANDLER_RESULT<BoardStackupResponse> API_HANDLER_PCB::handleGetStackup(
        const HANDLER_CONTEXT<GetBoardStackup>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BoardStackupResponse  response;

    frame()->GetBoard()->GetStackupOrDefault().Serialize( *response.mutable_stackup() );

    // User-settable layer names are not stored in BOARD_STACKUP at the moment
    for( board::BoardStackupLayer& layer : *response.mutable_stackup()->mutable_layers() )
    {
        if( layer.type() == board::BoardStackupLayerType::BSLT_DIELECTRIC )
            continue;

        PCB_LAYER_ID id = FromProtoEnum<PCB_LAYER_ID>( layer.layer() );
        wxCHECK2( id != UNDEFINED_LAYER, continue );

        layer.set_user_name( frame()->GetBoard()->GetLayerName( id ) );
    }

    return response;
}


HANDLER_RESULT<BoardEnabledLayersResponse> API_HANDLER_PCB::handleGetBoardEnabledLayers(
        const HANDLER_CONTEXT<GetBoardEnabledLayers>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BoardEnabledLayersResponse response;

    BOARD* board = frame()->GetBoard();
    int copperLayerCount = board->GetCopperLayerCount();

    response.set_copper_layer_count( copperLayerCount );

    LSET enabled = board->GetEnabledLayers();

    // The Rescue layer is an internal detail and should be hidden from the API
    enabled.reset( Rescue );

    // Just in case this is out of sync; the API should always return the expected copper layers
    enabled |= LSET::AllCuMask( copperLayerCount );

    board::PackLayerSet( *response.mutable_layers(), enabled );

    return response;
}


HANDLER_RESULT<BoardEnabledLayersResponse> API_HANDLER_PCB::handleSetBoardEnabledLayers(
        const HANDLER_CONTEXT<SetBoardEnabledLayers>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( aCtx.Request.copper_layer_count() % 2 != 0 )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "copper_layer_count must be an even number" );
        return tl::unexpected( e );
    }

    if( aCtx.Request.copper_layer_count() > MAX_CU_LAYERS )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "copper_layer_count must be below %d", MAX_CU_LAYERS ) );
        return tl::unexpected( e );
    }

    int copperLayerCount = static_cast<int>( aCtx.Request.copper_layer_count() );
    LSET enabled = board::UnpackLayerSet( aCtx.Request.layers() );

    // Sanitize the input
    enabled |= LSET( { Edge_Cuts, Margin, F_CrtYd, B_CrtYd } );
    enabled &= ~LSET::AllCuMask();
    enabled |= LSET::AllCuMask( copperLayerCount );

    BOARD* board = frame()->GetBoard();

    LSET previousEnabled = board->GetEnabledLayers();
    LSET changedLayers = enabled ^ previousEnabled;

    board->SetEnabledLayers( enabled );
    board->SetVisibleLayers( board->GetVisibleLayers() | changedLayers );

    LSEQ removedLayers;

    for( PCB_LAYER_ID layer_id : previousEnabled )
    {
        if( !enabled[layer_id] && board->HasItemsOnLayer( layer_id ) )
            removedLayers.push_back( layer_id );
    }

    bool modified = false;

    if( !removedLayers.empty() )
    {
        m_frame->GetToolManager()->RunAction( PCB_ACTIONS::selectionClear );

        for( PCB_LAYER_ID layer_id : removedLayers )
            modified |= board->RemoveAllItemsOnLayer( layer_id );
    }

    if( enabled != previousEnabled )
        frame()->UpdateUserInterface();

    if( modified )
        frame()->OnModify();

    BoardEnabledLayersResponse response;

    response.set_copper_layer_count( copperLayerCount );
    board::PackLayerSet( *response.mutable_layers(), enabled );

    return response;
}


HANDLER_RESULT<common::types::EmbeddedFiles>
API_HANDLER_PCB::handleGetEmbeddedFiles( const HANDLER_CONTEXT<GetEmbeddedFiles>& aCtx )
{
    if( HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() ); !documentValidation )
        return tl::unexpected( documentValidation.error() );

    common::types::EmbeddedFiles response;
    board::PackEmbeddedFiles( response, *frame()->GetBoard() );
    return response;
}


HANDLER_RESULT<Empty> unpackEmbeddedFiles( EMBEDDED_FILES& aOutput, const common::types::EmbeddedFiles& aProto )
{
    if( !board::UnpackEmbeddedFiles( aOutput, aProto ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "embedded file validation failed" );
        return tl::unexpected( e );
    }

    return Empty();
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleAddEmbeddedFiles( const HANDLER_CONTEXT<AddEmbeddedFiles>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() ); !documentValidation )
        return tl::unexpected( documentValidation.error() );

    EMBEDDED_FILES files;
    HANDLER_RESULT<Empty> result = unpackEmbeddedFiles( files, aCtx.Request.files() );

    if( !result.has_value() )
        return result;

    EMBEDDED_FILES* boardFiles = frame()->GetBoard()->GetEmbeddedFiles();

    for( const auto& fileEntry : files.EmbeddedFileMap() )
    {
        const std::shared_ptr<EMBEDDED_FILES::EMBEDDED_FILE>& file = fileEntry.second;
        auto copy = std::make_shared<EMBEDDED_FILES::EMBEDDED_FILE>( *file );
        boardFiles->AddFile( copy );
    }

    frame()->OnModify();
    return result;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleSetEmbeddedFiles( const HANDLER_CONTEXT<SetEmbeddedFiles>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() ); !documentValidation )
        return tl::unexpected( documentValidation.error() );

    HANDLER_RESULT<Empty> result = unpackEmbeddedFiles( *frame()->GetBoard()->GetEmbeddedFiles(),
                                                        aCtx.Request.files() );

    if( !result.has_value() )
        return result;

    frame()->OnModify();
    return result;
}


HANDLER_RESULT<GraphicsDefaultsResponse> API_HANDLER_PCB::handleGetGraphicsDefaults(
        const HANDLER_CONTEXT<GetGraphicsDefaults>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    const BOARD_DESIGN_SETTINGS& bds = frame()->GetBoard()->GetDesignSettings();
    GraphicsDefaultsResponse response;

    // TODO: This should change to be an enum class
    constexpr std::array<kiapi::board::BoardLayerClass, LAYER_CLASS_COUNT> classOrder = {
        kiapi::board::BLC_SILKSCREEN,
        kiapi::board::BLC_COPPER,
        kiapi::board::BLC_EDGES,
        kiapi::board::BLC_COURTYARD,
        kiapi::board::BLC_FABRICATION,
        kiapi::board::BLC_OTHER
    };

    for( int i = 0; i < LAYER_CLASS_COUNT; ++i )
    {
        kiapi::board::BoardLayerGraphicsDefaults* l = response.mutable_defaults()->add_layers();

        l->set_layer( classOrder[i] );
        l->mutable_line_thickness()->set_value_nm( bds.m_LineThickness[i] );

        kiapi::common::types::TextAttributes* text = l->mutable_text();
        text->mutable_size()->set_x_nm( bds.m_TextSize[i].x );
        text->mutable_size()->set_y_nm( bds.m_TextSize[i].y );
        text->mutable_stroke_width()->set_value_nm( bds.m_TextThickness[i] );
        text->set_italic( bds.m_TextItalic[i] );
        text->set_keep_upright( bds.m_TextUpright[i] );
    }

    return response;
}


HANDLER_RESULT<types::Vector2> API_HANDLER_PCB::handleGetBoardOrigin(
        const HANDLER_CONTEXT<GetBoardOrigin>& aCtx )
{
    if( HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );
        !documentValidation )
    {
        return tl::unexpected( documentValidation.error() );
    }

    VECTOR2I origin;
    const BOARD_DESIGN_SETTINGS& settings = frame()->GetBoard()->GetDesignSettings();

    switch( aCtx.Request.type() )
    {
    case BOT_GRID:
        origin = settings.GetGridOrigin();
        break;

    case BOT_DRILL:
        origin = settings.GetAuxOrigin();
        break;

    default:
    case BOT_UNKNOWN:
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Unexpected origin type" );
        return tl::unexpected( e );
    }
    }

    types::Vector2 reply;
    PackVector2( reply, origin );
    return reply;
}

HANDLER_RESULT<Empty> API_HANDLER_PCB::handleSetBoardOrigin(
        const HANDLER_CONTEXT<SetBoardOrigin>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );
        !documentValidation )
    {
        return tl::unexpected( documentValidation.error() );
    }

    VECTOR2I origin = UnpackVector2( aCtx.Request.origin() );

    switch( aCtx.Request.type() )
    {
    case BOT_GRID:
    {
        PCB_EDIT_FRAME* f = frame();

        frame()->CallAfter( [f, origin]()
                            {
                                // gridSetOrigin takes ownership and frees this
                                VECTOR2D* dorigin = new VECTOR2D( origin );
                                TOOL_MANAGER* mgr = f->GetToolManager();
                                mgr->RunAction( PCB_ACTIONS::gridSetOrigin, dorigin );
                                f->Refresh();
                            } );
        break;
    }

    case BOT_DRILL:
    {
        PCB_EDIT_FRAME* f = frame();

        frame()->CallAfter( [f, origin]()
                            {
                                TOOL_MANAGER* mgr = f->GetToolManager();
                                mgr->RunAction( PCB_ACTIONS::drillSetOrigin, origin );
                                f->Refresh();
                            } );
        break;
    }

    default:
    case BOT_UNKNOWN:
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Unexpected origin type" );
        return tl::unexpected( e );
    }
    }

    return Empty();
}


HANDLER_RESULT<BoardLayerNameResponse> API_HANDLER_PCB::handleGetBoardLayerName(
            const HANDLER_CONTEXT<GetBoardLayerName>& aCtx )
{
    if( HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );
        !documentValidation )
    {
        return tl::unexpected( documentValidation.error() );
    }

    BoardLayerNameResponse response;

    PCB_LAYER_ID id = FromProtoEnum<PCB_LAYER_ID>( aCtx.Request.layer() );

    response.set_name( frame()->GetBoard()->GetLayerName( id ) );

    return response;
}


HANDLER_RESULT<BoardLayerResponse> API_HANDLER_PCB::handleGetBoardLayerByName(
            const HANDLER_CONTEXT<GetBoardLayerByName>& aCtx )
{
    if( HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );
        !documentValidation )
    {
        return tl::unexpected( documentValidation.error() );
    }

    BoardLayerResponse response;

    PCB_LAYER_ID id = frame()->GetBoard()->GetLayerID( wxString::FromUTF8( aCtx.Request.name() ) );
    response.set_layer( ToProtoEnum<PCB_LAYER_ID, board::types::BoardLayer>( id ) );

    return response;
}


HANDLER_RESULT<GetBoundingBoxResponse> API_HANDLER_PCB::handleGetBoundingBox(
        const HANDLER_CONTEXT<GetBoundingBox>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    GetBoundingBoxResponse response;
    bool includeText = aCtx.Request.mode() == BoundingBoxMode::BBM_ITEM_AND_CHILD_TEXT;

    for( const types::KIID& idMsg : aCtx.Request.items() )
    {
        KIID id( idMsg.value() );
        std::optional<BOARD_ITEM*> optItem = getItemById( id );

        if( !optItem )
            continue;

        BOARD_ITEM* item = *optItem;
        BOX2I bbox;

        if( item->Type() == PCB_FOOTPRINT_T )
            bbox = static_cast<FOOTPRINT*>( item )->GetBoundingBox( includeText );
        else
            bbox = item->GetBoundingBox();

        response.add_items()->set_value( idMsg.value() );
        PackBox2( *response.add_boxes(), bbox );
    }

    return response;
}


HANDLER_RESULT<PadShapeAsPolygonResponse> API_HANDLER_PCB::handleGetPadShapeAsPolygon(
        const HANDLER_CONTEXT<GetPadShapeAsPolygon>& aCtx )
{
    if( HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );
        !documentValidation )
    {
        return tl::unexpected( documentValidation.error() );
    }

    PadShapeAsPolygonResponse response;
    PCB_LAYER_ID layer = FromProtoEnum<PCB_LAYER_ID, board::types::BoardLayer>( aCtx.Request.layer() );

    for( const types::KIID& padRequest : aCtx.Request.pads() )
    {
        KIID id( padRequest.value() );
        std::optional<BOARD_ITEM*> optPad = getItemById( id );

        if( !optPad || ( *optPad )->Type() != PCB_PAD_T )
            continue;

        response.add_pads()->set_value( padRequest.value() );

        PAD* pad = static_cast<PAD*>( *optPad );
        SHAPE_POLY_SET poly;
        pad->TransformShapeToPolygon( poly, pad->Padstack().EffectiveLayerFor( layer ), 0,
                                      pad->GetMaxError(), ERROR_INSIDE );

        types::PolygonWithHoles* polyMsg = response.mutable_polygons()->Add();
        PackPolyLine( *polyMsg->mutable_outline(), poly.COutline( 0 ) );
    }

    return response;
}


HANDLER_RESULT<PadstackPresenceResponse> API_HANDLER_PCB::handleCheckPadstackPresenceOnLayers(
        const HANDLER_CONTEXT<CheckPadstackPresenceOnLayers>& aCtx )
{
    using board::types::BoardLayer;

    if( HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );
        !documentValidation )
    {
        return tl::unexpected( documentValidation.error() );
    }

    PadstackPresenceResponse response;

    LSET layers;

    for( const int layer : aCtx.Request.layers() )
        layers.set( FromProtoEnum<PCB_LAYER_ID, BoardLayer>( static_cast<BoardLayer>( layer ) ) );

    for( const types::KIID& padRequest : aCtx.Request.items() )
    {
        KIID id( padRequest.value() );
        std::optional<BOARD_ITEM*> optItem = getItemById( id );

        if( !optItem )
            continue;

        switch( ( *optItem )->Type() )
        {
        case PCB_PAD_T:
        {
            PAD* pad = static_cast<PAD*>( *optItem );

            for( PCB_LAYER_ID layer : layers )
            {
                PadstackPresenceEntry* entry = response.add_entries();
                entry->mutable_item()->set_value( pad->m_Uuid.AsStdString() );
                entry->set_layer( ToProtoEnum<PCB_LAYER_ID, BoardLayer>( layer ) );
                entry->set_presence( pad->FlashLayer( layer ) ? PSP_PRESENT : PSP_NOT_PRESENT );
            }

            break;
        }

        case PCB_VIA_T:
        {
            PCB_VIA* via = static_cast<PCB_VIA*>( *optItem );

            for( PCB_LAYER_ID layer : layers )
            {
                PadstackPresenceEntry* entry = response.add_entries();
                entry->mutable_item()->set_value( via->m_Uuid.AsStdString() );
                entry->set_layer( ToProtoEnum<PCB_LAYER_ID, BoardLayer>( layer ) );
                entry->set_presence( via->FlashLayer( layer ) ? PSP_PRESENT : PSP_NOT_PRESENT );
            }

            break;
        }

        default:
            break;
        }
    }

    return response;
}


HANDLER_RESULT<types::TitleBlockInfo> API_HANDLER_PCB::handleGetTitleBlockInfo(
        const HANDLER_CONTEXT<GetTitleBlockInfo>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();
    const TITLE_BLOCK& block = board->GetTitleBlock();

    types::TitleBlockInfo response;

    response.set_title( block.GetTitle().ToUTF8() );
    response.set_date( block.GetDate().ToUTF8() );
    response.set_revision( block.GetRevision().ToUTF8() );
    response.set_company( block.GetCompany().ToUTF8() );
    response.set_comment1( block.GetComment( 0 ).ToUTF8() );
    response.set_comment2( block.GetComment( 1 ).ToUTF8() );
    response.set_comment3( block.GetComment( 2 ).ToUTF8() );
    response.set_comment4( block.GetComment( 3 ).ToUTF8() );
    response.set_comment5( block.GetComment( 4 ).ToUTF8() );
    response.set_comment6( block.GetComment( 5 ).ToUTF8() );
    response.set_comment7( block.GetComment( 6 ).ToUTF8() );
    response.set_comment8( block.GetComment( 7 ).ToUTF8() );
    response.set_comment9( block.GetComment( 8 ).ToUTF8() );

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleSetTitleBlockInfo( const HANDLER_CONTEXT<SetTitleBlockInfo>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( !aCtx.Request.has_title_block() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "SetTitleBlockInfo requires title_block" );
        return tl::unexpected( e );
    }

    TITLE_BLOCK& block = frame()->GetBoard()->GetTitleBlock();

    const types::TitleBlockInfo& request = aCtx.Request.title_block();

    block.SetTitle( wxString::FromUTF8( request.title() ) );
    block.SetDate( wxString::FromUTF8( request.date() ) );
    block.SetRevision( wxString::FromUTF8( request.revision() ) );
    block.SetCompany( wxString::FromUTF8( request.company() ) );
    block.SetComment( 0, wxString::FromUTF8( request.comment1() ) );
    block.SetComment( 1, wxString::FromUTF8( request.comment2() ) );
    block.SetComment( 2, wxString::FromUTF8( request.comment3() ) );
    block.SetComment( 3, wxString::FromUTF8( request.comment4() ) );
    block.SetComment( 4, wxString::FromUTF8( request.comment5() ) );
    block.SetComment( 5, wxString::FromUTF8( request.comment6() ) );
    block.SetComment( 6, wxString::FromUTF8( request.comment7() ) );
    block.SetComment( 7, wxString::FromUTF8( request.comment8() ) );
    block.SetComment( 8, wxString::FromUTF8( request.comment9() ) );

    if( frame() )
    {
        frame()->OnModify();
        frame()->UpdateUserInterface();
    }

    return Empty();
}


HANDLER_RESULT<ExpandTextVariablesResponse> API_HANDLER_PCB::handleExpandTextVariables(
    const HANDLER_CONTEXT<ExpandTextVariables>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    ExpandTextVariablesResponse reply;
    BOARD* board = frame()->GetBoard();

    std::function<bool( wxString* )> textResolver =
            [&]( wxString* token ) -> bool
            {
                // Handles m_board->GetTitleBlock() *and* m_board->GetProject()
                return board->ResolveTextVar( token, 0 );
            };

    for( const std::string& textMsg : aCtx.Request.text() )
    {
        wxString text = ExpandTextVars( wxString::FromUTF8( textMsg ), &textResolver );

        if( aCtx.Request.expand_env_vars() )
            text = ExpandEnvVarSubstitutions( text, board->GetProject() );

        reply.add_text( text.ToUTF8() );
    }

    return reply;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleInteractiveMoveItems(
        const HANDLER_CONTEXT<InteractiveMoveItems>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    TOOL_MANAGER* mgr = frame()->GetToolManager();
    std::vector<EDA_ITEM*> toSelect;

    for( const kiapi::common::types::KIID& id : aCtx.Request.items() )
    {
        if( std::optional<BOARD_ITEM*> item = getItemById( KIID( id.value() ) ) )
            toSelect.emplace_back( static_cast<EDA_ITEM*>( *item ) );
    }

    if( toSelect.empty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "None of the given items exist on the board",
                                          aCtx.Request.board().board_filename() ) );
        return tl::unexpected( e );
    }

    PCB_SELECTION_TOOL* selectionTool = mgr->GetTool<PCB_SELECTION_TOOL>();
    selectionTool->GetSelection().SetReferencePoint( toSelect[0]->GetPosition() );

    mgr->RunAction( ACTIONS::selectionClear );
    mgr->RunAction<EDA_ITEMS*>( ACTIONS::selectItems, &toSelect );

    COMMIT* commit = getCurrentCommit( aCtx.ClientName );
    mgr->PostAPIAction( PCB_ACTIONS::move, commit );

    return Empty();
}


HANDLER_RESULT<FlipItemsResponse> API_HANDLER_PCB::handleFlipItems( const HANDLER_CONTEXT<FlipItems>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    auto containerResult = validateItemHeaderDocument( aCtx.Request.header() );

    if( !containerResult && containerResult.error().status() == ApiStatusCode::AS_UNHANDLED )
    {
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }
    else if( !containerResult )
    {
        return tl::unexpected( containerResult.error() );
    }

    FLIP_DIRECTION flipDirection = FromProtoEnum<FLIP_DIRECTION, BoardFlipDirection>(
            aCtx.Request.direction() );

    FlipItemsResponse response;
    response.mutable_header()->CopyFrom( aCtx.Request.header() );

    BOARD_COMMIT* commit = static_cast<BOARD_COMMIT*>( getCurrentCommit( aCtx.ClientName ) );

    bool anyModified = false;

    for( const types::KIID& id : aCtx.Request.items() )
    {
        ItemFlipResult* result = response.add_flipped_items();

        std::optional<BOARD_ITEM*> optItem = getItemById( KIID( id.value() ) );

        if( !optItem )
        {
            result->mutable_status()->set_code( ItemStatusCode::ISC_NONEXISTENT );
            result->mutable_status()->set_error_message(
                    fmt::format( "an item with UUID {} does not exist", id.value() ) );
            continue;
        }

        BOARD_ITEM* boardItem = *optItem;

        static const std::set<KICAD_T> flippableTypes = {
            PCB_FOOTPRINT_T,
            PCB_PAD_T,
            PCB_SHAPE_T,
            PCB_REFERENCE_IMAGE_T,
            PCB_FIELD_T,
            PCB_GENERATOR_T,
            PCB_TEXT_T,
            PCB_TEXTBOX_T,
            PCB_TABLE_T,
            PCB_TABLECELL_T,
            PCB_TRACE_T,
            PCB_VIA_T,
            PCB_ARC_T,
            PCB_ZONE_T,
            PCB_GROUP_T,
            PCB_BARCODE_T,
            PCB_MARKER_T,
            PCB_POINT_T,
            PCB_TARGET_T,
            PCB_DIM_ALIGNED_T,
            PCB_DIM_LEADER_T,
            PCB_DIM_CENTER_T,
            PCB_DIM_RADIAL_T,
            PCB_DIM_ORTHOGONAL_T,
        };

        KICAD_T itemType = boardItem->Type();

        if( !flippableTypes.contains( itemType ) )
        {
            result->mutable_status()->set_code( ItemStatusCode::ISC_INVALID_TYPE );
            result->mutable_status()->set_error_message(
                    fmt::format( "items of type {} cannot be flipped",
                                 magic_enum::enum_name( itemType ) ) );
            continue;
        }

        commit->Modify( boardItem, nullptr, RECURSE_MODE::RECURSE );
        boardItem->Flip( boardItem->GetPosition(), flipDirection );
        boardItem->Normalize();

        // Maybe this should be in FOOTPRINT::Normalize?
        if( boardItem->Type() == PCB_FOOTPRINT_T )
            static_cast<FOOTPRINT*>( boardItem )->InvalidateComponentClassCache();

        anyModified = true;

        google::protobuf::Any itemBuf;
        boardItem->Serialize( itemBuf );
        *result->mutable_item() = std::move( itemBuf );

        result->mutable_status()->set_code( ItemStatusCode::ISC_OK );
    }

    response.set_status( ItemRequestStatus::IRS_OK );

    if( anyModified && !m_activeClients.count( aCtx.ClientName ) )
        pushCurrentCommit( aCtx.ClientName, _( "Flipped items via API" ) );

    return response;
}


HANDLER_RESULT<NetsResponse> API_HANDLER_PCB::handleGetNets( const HANDLER_CONTEXT<GetNets>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    NetsResponse response;
    BOARD* board = frame()->GetBoard();

    std::set<wxString> netclassFilter;

    for( const std::string& nc : aCtx.Request.netclass_filter() )
        netclassFilter.insert( wxString( nc.c_str(), wxConvUTF8 ) );

    for( NETINFO_ITEM* net : board->GetNetInfo() )
    {
        NETCLASS* nc = net->GetNetClass();

        if( !netclassFilter.empty() && nc )
        {
            bool inClass = false;

            for( const wxString& filter : netclassFilter )
            {
                if( nc->ContainsNetclassWithName( filter ) )
                {
                    inClass = true;
                    break;
                }
            }

            if( !inClass )
                continue;
        }

        board::types::Net* netProto = response.add_nets();
        netProto->set_name( net->GetNetname() );
        netProto->mutable_code()->set_value( net->GetNetCode() );
    }

    return response;
}


HANDLER_RESULT<GetItemsResponse> API_HANDLER_PCB::handleGetConnectedItems(
        const HANDLER_CONTEXT<GetConnectedItems>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    std::vector<KICAD_T> types = parseRequestedItemTypes( aCtx.Request.types() );
    const bool filterByType = aCtx.Request.types_size() > 0;

    if( filterByType && types.empty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "none of the requested types are valid for a Board object" );
        return tl::unexpected( e );
    }

    std::set<KICAD_T> typeFilter( types.begin(), types.end() );
    std::vector<BOARD_CONNECTED_ITEM*> sourceItems;

    for( const types::KIID& id : aCtx.Request.items() )
    {
        if( std::optional<BOARD_ITEM*> item = getItemById( KIID( id.value() ) ) )
        {
            if( BOARD_CONNECTED_ITEM* connected = dynamic_cast<BOARD_CONNECTED_ITEM*>( *item ) )
                sourceItems.emplace_back( connected );
        }
    }

    if( sourceItems.empty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "none of the requested IDs were found or valid connected items" );
        return tl::unexpected( e );
    }

    GetItemsResponse response;
    std::shared_ptr<CONNECTIVITY_DATA> conn = frame()->GetBoard()->GetConnectivity();
    std::set<KIID> insertedItems;

    for( BOARD_CONNECTED_ITEM* source : sourceItems )
    {
        for( BOARD_CONNECTED_ITEM* connected : conn->GetConnectedItems( source ) )
        {
            if( filterByType && !typeFilter.contains( connected->Type() ) )
                continue;

            if( !insertedItems.insert( connected->m_Uuid ).second )
                continue;

            connected->Serialize( *response.add_items() );
        }
    }

    response.set_status( ItemRequestStatus::IRS_OK );
    return response;
}


std::vector<KICAD_T> API_HANDLER_PCB::parseRequestedItemTypes( const google::protobuf::RepeatedField<int>& aTypes )
{
    std::vector<KICAD_T> types;

    for( int typeRaw : aTypes )
    {
        auto typeMessage = static_cast<common::types::KiCadObjectType>( typeRaw );
        KICAD_T type = FromProtoEnum<KICAD_T>( typeMessage );

        if( type != TYPE_NOT_INIT )
            types.emplace_back( type );
    }

    return types;
}


HANDLER_RESULT<GetItemsResponse> API_HANDLER_PCB::handleGetItemsByNet(
        const HANDLER_CONTEXT<GetItemsByNet>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    std::vector<KICAD_T> types = parseRequestedItemTypes( aCtx.Request.types() );
    const bool filterByType = aCtx.Request.types_size() > 0;

    if( filterByType && types.empty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "none of the requested types are valid for a Board object" );
        return tl::unexpected( e );
    }

    if( !filterByType )
        types.assign( { PCB_PAD_T, PCB_VIA_T, PCB_TRACE_T, PCB_ARC_T, PCB_SHAPE_T, PCB_ZONE_T } );

    GetItemsResponse response;
    BOARD* board = frame()->GetBoard();
    std::shared_ptr<CONNECTIVITY_DATA> conn = board->GetConnectivity();
    std::set<KIID> insertedItems;

    const NETINFO_LIST& nets = board->GetNetInfo();

    for( const board::types::Net& net : aCtx.Request.nets() )
    {
        NETINFO_ITEM* netInfo = nets.GetNetItem( wxString::FromUTF8( net.name() ) );

        if( !netInfo )
            continue;

        for( BOARD_CONNECTED_ITEM* item : conn->GetNetItems( netInfo->GetNetCode(), types ) )
        {
            if( !insertedItems.insert( item->m_Uuid ).second )
                continue;

            item->Serialize( *response.add_items() );
        }
    }

    response.set_status( ItemRequestStatus::IRS_OK );
    return response;
}


HANDLER_RESULT<GetItemsResponse> API_HANDLER_PCB::handleGetItemsByNetClass(
        const HANDLER_CONTEXT<GetItemsByNetClass>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    std::vector<KICAD_T> types = parseRequestedItemTypes( aCtx.Request.types() );
    const bool filterByType = aCtx.Request.types_size() > 0;

    if( filterByType && types.empty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "none of the requested types are valid for a Board object" );
        return tl::unexpected( e );
    }

    if( !filterByType )
        types.assign( { PCB_PAD_T, PCB_VIA_T, PCB_TRACE_T, PCB_ARC_T, PCB_SHAPE_T, PCB_ZONE_T } );

    std::set<wxString> requestedClasses;

    for( const std::string& netClass : aCtx.Request.net_classes() )
        requestedClasses.insert( wxString( netClass.c_str(), wxConvUTF8 ) );

    GetItemsResponse response;
    BOARD* board = frame()->GetBoard();
    std::shared_ptr<CONNECTIVITY_DATA> conn = board->GetConnectivity();
    std::set<KIID> insertedItems;

    for( NETINFO_ITEM* net : board->GetNetInfo() )
    {
        if( !net )
            continue;

        NETCLASS* nc = net->GetNetClass();

        if( !requestedClasses.empty() )
        {
            if( !nc )
                continue;

            bool inClass = false;

            for( const wxString& filter : requestedClasses )
            {
                if( nc->ContainsNetclassWithName( filter ) )
                {
                    inClass = true;
                    break;
                }
            }

            if( !inClass )
                continue;
        }

        for( BOARD_CONNECTED_ITEM* item : conn->GetNetItems( net->GetNetCode(), types ) )
        {
            if( !insertedItems.insert( item->m_Uuid ).second )
                continue;

            item->Serialize( *response.add_items() );
        }
    }

    response.set_status( ItemRequestStatus::IRS_OK );
    return response;
}


HANDLER_RESULT<NetClassForNetsResponse> API_HANDLER_PCB::handleGetNetClassForNets(
            const HANDLER_CONTEXT<GetNetClassForNets>& aCtx )
{
    NetClassForNetsResponse response;

    BOARD* board = frame()->GetBoard();
    const NETINFO_LIST& nets = board->GetNetInfo();

    for( const board::types::Net& net : aCtx.Request.net() )
    {
        NETINFO_ITEM* netInfo = nets.GetNetItem( wxString::FromUTF8( net.name() ) );

        if( !netInfo )
            continue;

        auto [pair, rc] = response.mutable_classes()->insert( { net.name(), {} } );
        netInfo->GetNetClass()->Serialize( pair->second );
    }

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleRefillZones( const HANDLER_CONTEXT<RefillZones>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( aCtx.Request.zones().empty() )
    {
        TOOL_MANAGER* mgr = frame()->GetToolManager();
        frame()->CallAfter( [mgr]()
                            {
                                mgr->RunAction( PCB_ACTIONS::zoneFillAll );
                            } );
    }
    else
    {
        // TODO
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNIMPLEMENTED );
        return tl::unexpected( e );
    }

    return Empty();
}


HANDLER_RESULT<SavedDocumentResponse> API_HANDLER_PCB::handleSaveDocumentToString(
        const HANDLER_CONTEXT<SaveDocumentToString>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    SavedDocumentResponse response;
    response.mutable_document()->CopyFrom( aCtx.Request.document() );

    CLIPBOARD_IO io;
    io.SetWriter(
        [&]( const wxString& aData )
        {
            response.set_contents( aData.ToUTF8() );
        } );

    io.SaveBoard( wxEmptyString, frame()->GetBoard(), nullptr );

    return response;
}


HANDLER_RESULT<SavedSelectionResponse> API_HANDLER_PCB::handleSaveSelectionToString(
        const HANDLER_CONTEXT<SaveSelectionToString>& aCtx )
{
    SavedSelectionResponse response;

    TOOL_MANAGER* mgr = frame()->GetToolManager();
    PCB_SELECTION_TOOL* selectionTool = mgr->GetTool<PCB_SELECTION_TOOL>();
    PCB_SELECTION& selection = selectionTool->GetSelection();

    CLIPBOARD_IO io;
    io.SetWriter(
        [&]( const wxString& aData )
        {
            response.set_contents( aData.ToUTF8() );
        } );

    io.SetBoard( frame()->GetBoard() );
    io.SaveSelection( selection, false );

    return response;
}


HANDLER_RESULT<CreateItemsResponse> API_HANDLER_PCB::handleParseAndCreateItemsFromString(
        const HANDLER_CONTEXT<ParseAndCreateItemsFromString>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    CreateItemsResponse response;
    return response;
}


HANDLER_RESULT<BoardLayers> API_HANDLER_PCB::handleGetVisibleLayers(
        const HANDLER_CONTEXT<GetVisibleLayers>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BoardLayers response;

    for( PCB_LAYER_ID layer : frame()->GetBoard()->GetVisibleLayers() )
        response.add_layers( ToProtoEnum<PCB_LAYER_ID, board::types::BoardLayer>( layer ) );

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleSetVisibleLayers(
        const HANDLER_CONTEXT<SetVisibleLayers>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    LSET visible;
    LSET enabled = frame()->GetBoard()->GetEnabledLayers();

    for( int layerIdx : aCtx.Request.layers() )
    {
        PCB_LAYER_ID layer =
                FromProtoEnum<PCB_LAYER_ID>( static_cast<board::types::BoardLayer>( layerIdx ) );

        if( enabled.Contains( layer ) )
            visible.set( layer );
    }

    frame()->GetBoard()->SetVisibleLayers( visible );
    frame()->GetAppearancePanel()->OnBoardChanged();
    frame()->GetCanvas()->SyncLayersVisibility( frame()->GetBoard() );
    frame()->Refresh();
    return Empty();
}


HANDLER_RESULT<BoardLayerResponse> API_HANDLER_PCB::handleGetActiveLayer(
        const HANDLER_CONTEXT<GetActiveLayer>& aCtx )
{
    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BoardLayerResponse response;
    response.set_layer(
            ToProtoEnum<PCB_LAYER_ID, board::types::BoardLayer>( frame()->GetActiveLayer() ) );

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleSetActiveLayer(
        const HANDLER_CONTEXT<SetActiveLayer>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    PCB_LAYER_ID layer = FromProtoEnum<PCB_LAYER_ID>( aCtx.Request.layer() );

    if( !frame()->GetBoard()->GetEnabledLayers().Contains( layer ) )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( fmt::format( "Layer {} is not a valid layer for the given board",
                                            magic_enum::enum_name( layer ) ) );
        return tl::unexpected( err );
    }

    frame()->SetActiveLayer( layer );
    return Empty();
}


HANDLER_RESULT<BoardEditorAppearanceSettings> API_HANDLER_PCB::handleGetBoardEditorAppearanceSettings(
        const HANDLER_CONTEXT<GetBoardEditorAppearanceSettings>& aCtx )
{
    BoardEditorAppearanceSettings reply;

    // TODO: might be nice to put all these things in one place and have it derive SERIALIZABLE

    const PCB_DISPLAY_OPTIONS& displayOptions = frame()->GetDisplayOptions();

    reply.set_inactive_layer_display( ToProtoEnum<HIGH_CONTRAST_MODE, InactiveLayerDisplayMode>(
            displayOptions.m_ContrastModeDisplay ) );
    reply.set_net_color_display(
            ToProtoEnum<NET_COLOR_MODE, NetColorDisplayMode>( displayOptions.m_NetColorMode ) );

    reply.set_board_flip( frame()->GetCanvas()->GetView()->IsMirroredX()
                                  ? BoardFlipMode::BFM_FLIPPED_X
                                  : BoardFlipMode::BFM_NORMAL );

    PCBNEW_SETTINGS* editorSettings = frame()->GetPcbNewSettings();

    reply.set_ratsnest_display( ToProtoEnum<RATSNEST_MODE, RatsnestDisplayMode>(
            editorSettings->m_Display.m_RatsnestMode ) );

    return reply;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleSetBoardEditorAppearanceSettings(
        const HANDLER_CONTEXT<SetBoardEditorAppearanceSettings>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    PCB_DISPLAY_OPTIONS options = frame()->GetDisplayOptions();
    PCBNEW_SETTINGS* editorSettings = frame()->GetPcbNewSettings();
    const BoardEditorAppearanceSettings& newSettings = aCtx.Request.settings();

    options.m_ContrastModeDisplay =
            FromProtoEnum<HIGH_CONTRAST_MODE>( newSettings.inactive_layer_display() );
    options.m_NetColorMode =
            FromProtoEnum<NET_COLOR_MODE>( newSettings.net_color_display() );
    options.m_FlipBoardView = newSettings.board_flip() == BoardFlipMode::BFM_FLIPPED_X;

    editorSettings->m_Display.m_RatsnestMode =
            FromProtoEnum<RATSNEST_MODE>( newSettings.ratsnest_display() );

    frame()->SetDisplayOptions( options );
    frame()->GetCanvas()->GetView()->UpdateAllLayersColor();
    frame()->GetCanvas()->Refresh();

    return Empty();
}


HANDLER_RESULT<InjectDrcErrorResponse> API_HANDLER_PCB::handleInjectDrcError(
        const HANDLER_CONTEXT<InjectDrcError>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.board() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    SEVERITY severity = FromProtoEnum<SEVERITY>( aCtx.Request.severity() );
    int      layer = severity == RPT_SEVERITY_WARNING ? LAYER_DRC_WARNING : LAYER_DRC_ERROR;
    int      code = severity == RPT_SEVERITY_WARNING ? DRCE_GENERIC_WARNING : DRCE_GENERIC_ERROR;

    std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( code );

    drcItem->SetErrorMessage( wxString::FromUTF8( aCtx.Request.message() ) );

    RC_ITEM::KIIDS ids;

    for( const auto& id : aCtx.Request.items() )
        ids.emplace_back( KIID( id.value() ) );

    if( !ids.empty() )
        drcItem->SetItems( ids );

    const auto& pos = aCtx.Request.position();
    VECTOR2I    position( static_cast<int>( pos.x_nm() ), static_cast<int>( pos.y_nm() ) );

    PCB_MARKER* marker = new PCB_MARKER( drcItem, position, layer );

    COMMIT* commit = getCurrentCommit( aCtx.ClientName );
    commit->Add( marker );
    commit->Push( wxS( "API injected DRC marker" ) );

    InjectDrcErrorResponse response;
    response.mutable_marker()->set_value( marker->GetUUID().AsStdString() );

    return response;
}


HANDLER_RESULT<VariantsResponse> API_HANDLER_PCB::handleGetVariants( const HANDLER_CONTEXT<GetVariants>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() ); !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();
    VariantsResponse response;

    response.mutable_document()->CopyFrom( aCtx.Request.document() );

    for( const wxString& name : board->GetVariantNames() )
    {
        types::DesignVariant* var = response.add_variants();
        var->set_name( name.ToUTF8() );
        var->set_description( board->GetVariantDescription( name ).ToUTF8() );
    }

    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleAddVariant( const HANDLER_CONTEXT<AddVariant>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() ); !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    wxString name = wxString::FromUTF8( aCtx.Request.name() );

    if( name.IsEmpty() || name.CmpNoCase( GetDefaultVariantName() ) == 0 )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "'{}' is not a valid variant name", aCtx.Request.name() ) );
        return tl::unexpected( e );
    }

    if( board->HasVariant( name ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "a variant named '{}' already exists", aCtx.Request.name() ) );
        return tl::unexpected( e );
    }

    board->AddVariant( name );

    if( aCtx.Request.has_description() )
        board->SetVariantDescription( name, wxString::FromUTF8( aCtx.Request.description() ) );

    frame()->UpdateVariantSelectionCtrl();

    return Empty();
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleDeleteVariant( const HANDLER_CONTEXT<DeleteVariant>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() ); !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    wxString name = wxString::FromUTF8( aCtx.Request.name() );

    if( name.IsEmpty() || name.CmpNoCase( GetDefaultVariantName() ) == 0 )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "'{}' is not a valid variant name", aCtx.Request.name() ) );
        return tl::unexpected( e );
    }

    if( !board->HasVariant( name ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "no variant named '{}' exists", aCtx.Request.name() ) );
        return tl::unexpected( e );
    }

    board->DeleteVariant( name );
    frame()->UpdateVariantSelectionCtrl();

    return Empty();
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleRenameVariant( const HANDLER_CONTEXT<RenameVariant>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() ); !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    wxString oldName = wxString::FromUTF8( aCtx.Request.old_name() );
    wxString newName = wxString::FromUTF8( aCtx.Request.new_name() );

    if( oldName.IsEmpty() || oldName.CmpNoCase( GetDefaultVariantName() ) == 0 )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "'{}' is not a valid variant name", aCtx.Request.old_name() ) );
        return tl::unexpected( e );
    }

    if( newName.IsEmpty() || newName.CmpNoCase( GetDefaultVariantName() ) == 0 )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "'{}' is not a valid variant name", aCtx.Request.new_name() ) );
        return tl::unexpected( e );
    }

    if( !board->HasVariant( oldName ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "no variant named '{}' exists", aCtx.Request.old_name() ) );
        return tl::unexpected( e );
    }

    if( board->HasVariant( newName ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "a variant named '{}' already exists", aCtx.Request.new_name() ) );
        return tl::unexpected( e );
    }

    board->RenameVariant( oldName, newName );
    frame()->UpdateVariantSelectionCtrl();

    return Empty();
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleCopyVariant( const HANDLER_CONTEXT<CopyVariant>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() ); !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    wxString oldName = wxString::FromUTF8( aCtx.Request.old_name() );
    wxString newName = wxString::FromUTF8( aCtx.Request.new_name() );

    if( oldName.IsEmpty() || oldName.CmpNoCase( GetDefaultVariantName() ) == 0 )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "'{}' is not a valid variant name", aCtx.Request.old_name() ) );
        return tl::unexpected( e );
    }

    if( newName.IsEmpty() || newName.CmpNoCase( GetDefaultVariantName() ) == 0 )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "'{}' is not a valid variant name", aCtx.Request.new_name() ) );
        return tl::unexpected( e );
    }

    if( !board->HasVariant( oldName ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "no variant named '{}' exists", aCtx.Request.old_name() ) );
        return tl::unexpected( e );
    }

    if( board->HasVariant( newName ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "a variant named '{}' already exists", aCtx.Request.new_name() ) );
        return tl::unexpected( e );
    }

    board->CopyVariant( oldName, newName,
                        aCtx.Request.has_new_description() ? wxString::FromUTF8( aCtx.Request.new_description() )
                                                           : wxString() );
    frame()->UpdateVariantSelectionCtrl();

    return Empty();
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleSetVariantDescription( const HANDLER_CONTEXT<SetVariantDescription>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() ); !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    wxString name = wxString::FromUTF8( aCtx.Request.name() );

    if( name.IsEmpty() || name.CmpNoCase( GetDefaultVariantName() ) == 0 || !board->HasVariant( name ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "no variant named '{}' exists", aCtx.Request.name() ) );
        return tl::unexpected( e );
    }

    board->SetVariantDescription( name, wxString::FromUTF8( aCtx.Request.description() ) );

    return Empty();
}


HANDLER_RESULT<Empty> API_HANDLER_PCB::handleSetCurrentVariant( const HANDLER_CONTEXT<SetCurrentVariant>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() ); !documentValidation )
        return tl::unexpected( documentValidation.error() );

    BOARD* board = frame()->GetBoard();

    if( aCtx.Request.has_name() && !aCtx.Request.name().empty() )
    {
        if( wxString name = wxString::FromUTF8( aCtx.Request.name() ); !board->HasVariant( name ) )
        {
            ApiResponseStatus e;
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( fmt::format( "no variant named '{}' exists", aCtx.Request.name() ) );
            return tl::unexpected( e );
        }
    }

    frame()->SetCurrentVariant( aCtx.Request.has_name() ? wxString::FromUTF8( aCtx.Request.name() ) : wxString() );

    return Empty();
}


HANDLER_RESULT<CurrentVariantResponse>
API_HANDLER_PCB::handleGetCurrentVariant( const HANDLER_CONTEXT<GetCurrentVariant>& aCtx )
{
    if( HANDLER_RESULT<bool> documentValidation = validateDocument( aCtx.Request.document() ); !documentValidation )
        return tl::unexpected( documentValidation.error() );

    CurrentVariantResponse response;

    if( wxString current = frame()->GetBoard()->GetCurrentVariant(); !current.IsEmpty() )
        response.set_name( current.ToUTF8() );

    return response;
}
