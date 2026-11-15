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

#include <ranges>

#include <api/api_handler_editor.h>
#include <api/api_server.h>
#include <api/api_utils.h>
#include <eda_base_frame.h>
#include <eda_draw_frame.h>
#include <dialog_shim.h>
#include <project.h>
#include <tool/tool_manager.h>
#include <tool/action_manager.h>
#include <view/view.h>
#include <wx/filename.h>
#include <eda_item.h>
#include <ki_exception.h>
#include <pgm_base.h>
#include <wx/wx.h>

using namespace kiapi::common::commands;


API_HANDLER_EDITOR::API_HANDLER_EDITOR( EDA_BASE_FRAME* aFrame ) :
        API_HANDLER(),
        m_frame( aFrame )
{
    registerHandler<BeginCommit, BeginCommitResponse>( &API_HANDLER_EDITOR::handleBeginCommit );
    registerHandler<EndCommit, EndCommitResponse>( &API_HANDLER_EDITOR::handleEndCommit );
    registerHandler<CreateItems, CreateItemsResponse>( &API_HANDLER_EDITOR::handleCreateItems );
    registerHandler<UpdateItems, UpdateItemsResponse>( &API_HANDLER_EDITOR::handleUpdateItems );
    registerHandler<DeleteItems, DeleteItemsResponse>( &API_HANDLER_EDITOR::handleDeleteItems );
    registerHandler<HitTest, HitTestResponse>( &API_HANDLER_EDITOR::handleHitTest );
    registerHandler<SaveDocument, Empty>( &API_HANDLER_EDITOR::handleSaveDocument );
    registerHandler<SaveCopyOfDocument, Empty>( &API_HANDLER_EDITOR::handleSaveCopyOfDocument );
    registerHandler<RevertDocument, Empty>( &API_HANDLER_EDITOR::handleRevertDocument );
    registerHandler<RunAction, RunActionResponse>( &API_HANDLER_EDITOR::handleRunAction );
}


static std::optional<ApiResponseStatus> rejectIfMultipleEditors( const std::string& aFuncName )
{
    // Handle legacy clients that don't know about the document field, when it's safe to do so
    const auto& handlers = Pgm().GetApiServer().Handlers();
    if( std::count_if( handlers.begin(), handlers.end(),
                               []( const API_HANDLER* aHandler )
                               {
                                   return dynamic_cast<const API_HANDLER_EDITOR*>( aHandler );
                               } )
        > 1 )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format(
                "{} without a document specified is not allowed when multiple editors are open", aFuncName ) );
        return e;
    }

    return std::nullopt;
}


HANDLER_RESULT<BeginCommitResponse> API_HANDLER_EDITOR::handleBeginCommit(
        const HANDLER_CONTEXT<BeginCommit>& aCtx )
{
    // Handle legacy clients that don't know about the document field, when it's safe to do so
    if( !aCtx.Request.has_header() )
    {
        if( auto error = rejectIfMultipleEditors( "BeginCommit" ) )
            return tl::unexpected( *error );
    }

    // TODO Mark BeginCommit.header explicitly required in V12 and remove this optionality along with the above
    if( aCtx.Request.has_header() )
    {
        if( HANDLER_RESULT<std::optional<KIID>> valid = validateItemHeaderDocument( aCtx.Request.header() ); !valid )
            return tl::unexpected( valid.error() );
    }

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( m_commits.count( aCtx.ClientName ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "the client {} already has a commit in progress",
                                          aCtx.ClientName ) );
        return tl::unexpected( e );
    }

    wxASSERT( !m_activeClients.count( aCtx.ClientName ) );

    BeginCommitResponse response;

    KIID id;
    m_commits[aCtx.ClientName] = std::make_pair( id, createCommit() );
    response.mutable_id()->set_value( id.AsStdString() );

    m_activeClients.insert( aCtx.ClientName );

    return response;
}


HANDLER_RESULT<EndCommitResponse> API_HANDLER_EDITOR::handleEndCommit(
        const HANDLER_CONTEXT<EndCommit>& aCtx )
{
    // Handle legacy clients that don't know about the document field, when it's safe to do so
    if( !aCtx.Request.has_header() )
    {
        if( auto error = rejectIfMultipleEditors( "EndCommit" ) )
            return tl::unexpected( *error );
    }

    // TODO Mark EndCommit.header explicitly required in V12 and remove this optionality along with the above
    if( aCtx.Request.has_header() )
    {
        if( HANDLER_RESULT<std::optional<KIID>> valid = validateItemHeaderDocument( aCtx.Request.header() ); !valid )
            return tl::unexpected( valid.error() );
    }

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !m_commits.count( aCtx.ClientName ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "the client {} does not has a commit in progress",
                                          aCtx.ClientName ) );
        return tl::unexpected( e );
    }

    wxASSERT( m_activeClients.count( aCtx.ClientName ) );

    const std::pair<KIID, std::unique_ptr<COMMIT>>& pair = m_commits.at( aCtx.ClientName );
    const KIID& id = pair.first;
    const std::unique_ptr<COMMIT>& commit = pair.second;

    EndCommitResponse response;

    // Do not check IDs with drop; it is a safety net in case the id was lost on the client side
    switch( aCtx.Request.action() )
    {
    case kiapi::common::commands::CMA_DROP:
    {
        commit->Revert();
        m_commits.erase( aCtx.ClientName );
        m_activeClients.erase( aCtx.ClientName );
        break;
    }

    case kiapi::common::commands::CMA_COMMIT:
    {
        if( aCtx.Request.id().value().compare( id.AsStdString() ) != 0 )
        {
            ApiResponseStatus e;
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( fmt::format( "the id {} does not match the commit in progress",
                                              aCtx.Request.id().value() ) );
            return tl::unexpected( e );
        }

        pushCurrentCommit( aCtx.ClientName, wxString( aCtx.Request.message().c_str(), wxConvUTF8 ) );
        break;
    }

    default:
        break;
    }

    return response;
}


COMMIT* API_HANDLER_EDITOR::getCurrentCommit( const std::string& aClientName )
{
    if( !m_commits.count( aClientName ) )
    {
        KIID id;
        m_commits[aClientName] = std::make_pair( id, createCommit() );
    }

    return m_commits.at( aClientName ).second.get();
}


void API_HANDLER_EDITOR::pushCurrentCommit( const std::string& aClientName,
                                            const wxString& aMessage )
{
    auto it = m_commits.find( aClientName );

    if( it == m_commits.end() )
        return;

    it->second.second->Push( aMessage.IsEmpty() ? m_defaultCommitMessage : aMessage );
    m_commits.erase( it );
    m_activeClients.erase( aClientName );
}


HANDLER_RESULT<bool> API_HANDLER_EDITOR::validateDocument( const DocumentSpecifier& aDocument )
{
    if( aDocument.type() != thisDocumentType() )
        return documentError( "", ApiStatusCode::AS_UNHANDLED );

    if( !validateDocumentInternal( aDocument ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "the requested document {} is not open",
                                          aDocument.board_filename() ) );
        return tl::unexpected( e );
    }

    return true;
}


HANDLER_RESULT<std::optional<KIID>> API_HANDLER_EDITOR::validateItemHeaderDocument(
        const types::ItemHeader& aHeader )
{
    if( !aHeader.has_document() || aHeader.document().type() != thisDocumentType() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        // No error message, this is a flag that the server should try a different handler
        return tl::unexpected( e );
    }

    HANDLER_RESULT<bool> documentValidation = validateDocument( aHeader.document() );

    if( !documentValidation )
        return tl::unexpected( documentValidation.error() );

    if( !validateDocumentInternal( aHeader.document() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "the requested document {} is not open",
                                          aHeader.document().board_filename() ) );
        return tl::unexpected( e );
    }

    if( aHeader.has_container() )
    {
        return KIID( aHeader.container().value() );
    }

    // Valid header, but no container provided
    return std::nullopt;
}


std::optional<ApiResponseStatus> API_HANDLER_EDITOR::checkForBusy()
{
    // On macOS a quasi-modal library dialog need not make IsEnabled() false.
    // Check actual dialog modality as well as the frame's interactive-operation guard.
    bool modal = false;
    for( wxWindow* window : wxTopLevelWindows )
    {
        auto* dialog = dynamic_cast<wxDialog*>( window );
        auto* shim = dynamic_cast<DIALOG_SHIM*>( window );
        if( dialog && dialog->IsShown()
            && ( dialog->IsModal() || ( shim && shim->IsQuasiModal() ) ) )
        {
            modal = true;
            break;
        }
    }
    if( modal || !m_frame->CanAcceptApiCommands() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BUSY );
        e.set_error_message( "KiCad is busy and cannot respond to API requests right now" );
        return e;
    }

    return std::nullopt;
}


HANDLER_RESULT<CreateItemsResponse> API_HANDLER_EDITOR::handleCreateItems(
        const HANDLER_CONTEXT<CreateItems>& aCtx )
{
    HANDLER_RESULT<std::optional<KIID>> document =
            validateItemHeaderDocument( aCtx.Request.header() );

    if( !document )
        return tl::unexpected( document.error() );

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    CreateItemsResponse response;

    HANDLER_RESULT<ItemRequestStatus> result = handleCreateUpdateItemsInternal( true,
            aCtx.ClientName,
            aCtx.Request.header(), aCtx.Request.items(),
            [&]( const ItemStatus& aStatus, const google::protobuf::Any& aItem )
            {
                ItemCreationResult itemResult;
                itemResult.mutable_status()->CopyFrom( aStatus );
                itemResult.mutable_item()->CopyFrom( aItem );
                response.mutable_created_items()->Add( std::move( itemResult ) );
            } );

    if( !result.has_value() )
        return tl::unexpected( result.error() );

    response.set_status( *result );
    return response;
}


HANDLER_RESULT<UpdateItemsResponse> API_HANDLER_EDITOR::handleUpdateItems(
        const HANDLER_CONTEXT<UpdateItems>& aCtx )
{
    HANDLER_RESULT<std::optional<KIID>> document =
            validateItemHeaderDocument( aCtx.Request.header() );

    if( !document )
        return tl::unexpected( document.error() );

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    UpdateItemsResponse response;

    HANDLER_RESULT<ItemRequestStatus> result = handleCreateUpdateItemsInternal( false,
            aCtx.ClientName,
            aCtx.Request.header(), aCtx.Request.items(),
            [&]( const ItemStatus& aStatus, const google::protobuf::Any& aItem )
            {
                ItemUpdateResult itemResult;
                itemResult.mutable_status()->CopyFrom( aStatus );
                itemResult.mutable_item()->CopyFrom( aItem );
                response.mutable_updated_items()->Add( std::move( itemResult ) );
            } );

    if( !result.has_value() )
        return tl::unexpected( result.error() );

    response.set_status( *result );
    return response;
}


HANDLER_RESULT<DeleteItemsResponse> API_HANDLER_EDITOR::handleDeleteItems(
        const HANDLER_CONTEXT<DeleteItems>& aCtx )
{
    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    std::map<KIID, ItemDeletionStatus> itemsToDelete;

    for( const kiapi::common::types::KIID& kiidBuf : aCtx.Request.item_ids() )
    {
        if( !kiidBuf.value().empty() )
        {
            KIID kiid( kiidBuf.value() );
            itemsToDelete[kiid] = ItemDeletionStatus::IDS_NONEXISTENT;
        }
    }

    if( itemsToDelete.empty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "no valid items to delete were given" );
        return tl::unexpected( e );
    }

    deleteItemsInternal( itemsToDelete, aCtx.ClientName );

    DeleteItemsResponse response;

    for( const auto& [id, status] : itemsToDelete )
    {
        ItemDeletionResult* result = response.add_deleted_items();
        result->mutable_id()->set_value( id.AsStdString() );
        result->set_status( status );
    }

    response.set_status( kiapi::common::types::ItemRequestStatus::IRS_OK );
    return response;
}


HANDLER_RESULT<HitTestResponse> API_HANDLER_EDITOR::handleHitTest(
        const HANDLER_CONTEXT<HitTest>& aCtx )
{
    if( !validateItemHeaderDocument( aCtx.Request.header() ) )
    {
        ApiResponseStatus e;
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    HitTestResponse response;

    std::optional<EDA_ITEM*> item = getItemFromDocument( aCtx.Request.header().document(),
                                                         KIID( aCtx.Request.id().value() ) );

    if( !item )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "the requested item ID is not present in the given document" );
        return tl::unexpected( e );
    }

    if( ( *item )->HitTest( UnpackVector2( aCtx.Request.position() ), aCtx.Request.tolerance() ) )
        response.set_result( HitTestResult::HTR_HIT );
    else
        response.set_result( HitTestResult::HTR_NO_HIT );

    return response;
}


// Document-only editors can participate in transaction exclusion without exposing item editing.
namespace
{
class DOCUMENT_API_COMMIT : public COMMIT
{
public:
    void Push( const wxString&, int ) override { clear(); }
    void Revert() override { clear(); }
protected:
    EDA_ITEM* undoLevelItem( EDA_ITEM* aItem ) const override { return aItem; }
    EDA_ITEM* makeImage( EDA_ITEM* aItem ) const override { return aItem->Clone(); }
};
}

std::unique_ptr<COMMIT> API_HANDLER_EDITOR::createCommit()
{
    return std::make_unique<DOCUMENT_API_COMMIT>();
}

HANDLER_RESULT<ItemRequestStatus> API_HANDLER_EDITOR::handleCreateUpdateItemsInternal(
        bool, const std::string&, const types::ItemHeader&,
        const google::protobuf::RepeatedPtrField<google::protobuf::Any>&,
        std::function<void( ItemStatus, google::protobuf::Any )> )
{
    return documentError( "Item editing is not supported in this editor", ApiStatusCode::AS_UNIMPLEMENTED );
}

void API_HANDLER_EDITOR::deleteItemsInternal( std::map<KIID, ItemDeletionStatus>& aItems,
                                              const std::string& )
{
    for( auto& [id, status] : aItems )
        status = ItemDeletionStatus::IDS_IMMUTABLE;
}

std::optional<EDA_ITEM*> API_HANDLER_EDITOR::getItemFromDocument( const DocumentSpecifier&, const KIID& )
{
    return std::nullopt;
}

tl::unexpected<ApiResponseStatus> API_HANDLER_EDITOR::documentError(
        const std::string& aMessage, ApiStatusCode aStatus )
{
    ApiResponseStatus error;
    error.set_status( aStatus );
    error.set_error_message( aMessage );
    return tl::unexpected( error );
}

HANDLER_RESULT<bool> API_HANDLER_EDITOR::validateDocumentCommand( const DocumentSpecifier& aDocument )
{
    auto valid = validateDocument( aDocument );
    if( !valid )
        return tl::unexpected( valid.error() );
    if( auto busy = checkForBusy() )
        return tl::unexpected( *busy );
    if( !m_commits.empty() )
        return documentError( "Finish or drop the open API commit before running document commands" );
    if( m_frame && !m_frame->ToolStackIsEmpty() )
        return documentError( "An interactive tool is active", ApiStatusCode::AS_BUSY );
    return true;
}

bool API_HANDLER_EDITOR::documentModified() const
{
    return m_frame && m_frame->IsContentModified();
}

HANDLER_RESULT<Empty> API_HANDLER_EDITOR::saveDocumentInternal( const DocumentSpecifier& )
{
    return documentError( "Saving is not supported", ApiStatusCode::AS_UNIMPLEMENTED );
}

HANDLER_RESULT<Empty> API_HANDLER_EDITOR::saveCopyInternal( const DocumentSpecifier&, const wxString&, bool )
{
    return documentError( "Saving a copy is not supported", ApiStatusCode::AS_UNIMPLEMENTED );
}

HANDLER_RESULT<Empty> API_HANDLER_EDITOR::revertDocumentInternal( const DocumentSpecifier& )
{
    return documentError( "Reloading is not supported", ApiStatusCode::AS_UNIMPLEMENTED );
}

HANDLER_RESULT<Empty> API_HANDLER_EDITOR::handleSaveDocument( const HANDLER_CONTEXT<SaveDocument>& aCtx )
{
    auto valid = validateDocumentCommand( aCtx.Request.document() );
    if( !valid )
        return tl::unexpected( valid.error() );
    try
    {
        return saveDocumentInternal( aCtx.Request.document() );
    }
    catch( const IO_ERROR& error )
    {
        return documentError( error.What().ToStdString() );
    }
}

HANDLER_RESULT<Empty> API_HANDLER_EDITOR::handleSaveCopyOfDocument(
        const HANDLER_CONTEXT<SaveCopyOfDocument>& aCtx )
{
    auto valid = validateDocumentCommand( aCtx.Request.document() );
    if( !valid )
        return tl::unexpected( valid.error() );
    if( aCtx.Request.path().empty() )
        return documentError( "A destination path is required" );

    wxFileName path( wxString::FromUTF8( aCtx.Request.path() ) );
    if( path.IsRelative() )
    {
        auto* frame = dynamic_cast<EDA_DRAW_FRAME*>( m_frame );
        if( !frame )
            return documentError( "An absolute destination path is required" );
        path = frame->Prj().AbsolutePath( path.GetFullPath() );
    }
    const std::map<types::DocumentType, wxString> extensions = {
        { types::DOCTYPE_PCB, "kicad_pcb" }, { types::DOCTYPE_SCHEMATIC, "kicad_sch" },
        { types::DOCTYPE_SYMBOL, "kicad_sym" }, { types::DOCTYPE_FOOTPRINT, "kicad_mod" },
        { types::DOCTYPE_DRAWING_SHEET, "kicad_wks" }
    };
    if( !path.IsOk() || !path.IsDirWritable() || path.GetExt() != extensions.at( thisDocumentType() ) )
        return documentError( "Destination directory must be writable and extension must match the document type" );
    if( path.FileExists() && ( !aCtx.Request.options().overwrite() || !path.IsFileWritable() ) )
        return documentError( "Destination exists; overwrite permission and a writable file are required" );
    try
    {
        return saveCopyInternal( aCtx.Request.document(), path.GetFullPath(),
                                 aCtx.Request.options().include_project() );
    }
    catch( const IO_ERROR& error )
    {
        return documentError( error.What().ToStdString() );
    }
}

HANDLER_RESULT<Empty> API_HANDLER_EDITOR::handleRevertDocument( const HANDLER_CONTEXT<RevertDocument>& aCtx )
{
    auto valid = validateDocumentCommand( aCtx.Request.document() );
    if( !valid )
        return tl::unexpected( valid.error() );
    if( documentModified() && !aCtx.Request.discard_unsaved() )
        return documentError( "Document has unsaved changes; set discard_unsaved=true to discard them" );

    auto* frame = dynamic_cast<EDA_DRAW_FRAME*>( m_frame );
    const double scale = frame ? frame->GetCanvas()->GetView()->GetScale() : 1.0;
    const VECTOR2D center = frame ? frame->GetCanvas()->GetView()->GetCenter() : VECTOR2D();
    try
    {
        auto result = revertDocumentInternal( aCtx.Request.document() );
        if( frame )
        {
            frame->GetCanvas()->GetView()->SetScale( scale );
            frame->GetCanvas()->GetView()->SetCenter( center );
            frame->UpdateStatusBar();
            frame->GetCanvas()->Refresh();
        }
        return result;
    }
    catch( const IO_ERROR& error )
    {
        return documentError( error.What().ToStdString() );
    }
}

HANDLER_RESULT<RunActionResponse> API_HANDLER_EDITOR::handleRunAction(
        const HANDLER_CONTEXT<RunAction>& aCtx )
{
    if( !aCtx.Request.has_document() )
    {
        if( auto error = rejectIfMultipleEditors( "RunAction" ) )
            return tl::unexpected( *error );
        if( auto busy = checkForBusy() )
            return tl::unexpected( *busy );
        if( !m_commits.empty() )
            return documentError( "Finish or drop the open API commit before RunAction" );
        if( m_frame && !m_frame->ToolStackIsEmpty() )
            return documentError( "An interactive tool is active", ApiStatusCode::AS_BUSY );
    }
    else
    {
        auto valid = validateDocumentCommand( aCtx.Request.document() );
        if( !valid )
            return tl::unexpected( valid.error() );
    }
    // Never bypass the explicit dirty-revert policy through the GUI action.
    if( aCtx.Request.action() == "common.Control.revert" )
    {
        if( !aCtx.Request.has_document() )
            return documentError( "Revert actions require an explicit document" );
        HANDLER_CONTEXT<RevertDocument> revert;
        revert.Request.mutable_document()->CopyFrom( aCtx.Request.document() );
        auto result = handleRevertDocument( revert );
        if( !result )
            return tl::unexpected( result.error() );
        RunActionResponse response;
        response.set_status( RAS_OK );
        return response;
    }
    if( aCtx.Request.action() == "common.Control.save" )
    {
        if( !aCtx.Request.has_document() )
            return documentError( "Save actions require an explicit document" );
        HANDLER_CONTEXT<SaveDocument> save;
        save.Request.mutable_document()->CopyFrom( aCtx.Request.document() );
        auto result = handleSaveDocument( save );
        if( !result )
            return tl::unexpected( result.error() );
        RunActionResponse response;
        response.set_status( RAS_OK );
        return response;
    }
    RunActionResponse response;
    // Reply before running actions which may enter a modal event loop. Otherwise the
    // request/reply socket cannot receive further requests (including busy checks).
    // Use the GUI event queue, not TOOL_MANAGER::PostAction: the latter needs an
    // active tool dispatch to drain its queue and can wait indefinitely during IPC.
    auto* manager = m_frame ? m_frame->GetToolManager() : nullptr;
    if( !manager || !manager->GetActionManager()->FindAction( aCtx.Request.action() ) )
    {
        response.set_status( RAS_INVALID );
        return response;
    }
    m_frame->CallAfter( [frame = m_frame, action = aCtx.Request.action()]()
                       { frame->GetToolManager()->RunAction( action ); } );
    response.set_status( RAS_OK );
    return response;
}
