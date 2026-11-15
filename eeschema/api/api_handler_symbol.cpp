/* Copyright The KiCad Developers, see AUTHORS.txt.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <api/api_handler_symbol.h>
#include <api/api_utils.h>
#include <symbol_editor/symbol_edit_frame.h>
#include <lib_symbol.h>
#include <project.h>
#include <project_sch.h>
#include <libraries/symbol_library_adapter.h>
#include <symbol_editor/lib_symbol_library_manager.h>
#include <sch_io/kicad_sexpr/sch_io_kicad_sexpr.h>
#include <wx/filename.h>

using namespace kiapi::common;
using namespace kiapi::common::commands;

API_HANDLER_SYMBOL::API_HANDLER_SYMBOL( SYMBOL_EDIT_FRAME* aFrame ) : API_HANDLER_EDITOR( aFrame )
{
    registerHandler<GetOpenDocuments, GetOpenDocumentsResponse>(
            &API_HANDLER_SYMBOL::handleGetOpenDocuments );
}

HANDLER_RESULT<GetOpenDocumentsResponse> API_HANDLER_SYMBOL::handleGetOpenDocuments(
        const HANDLER_CONTEXT<GetOpenDocuments>& aCtx )
{
    if( aCtx.Request.type() != types::DOCTYPE_SYMBOL )
    {
        ApiResponseStatus error;
        error.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( error );
    }
    GetOpenDocumentsResponse response;
    if( LIB_SYMBOL* symbol = frame()->GetCurSymbol() )
    {
        auto* doc = response.add_documents();
        doc->set_type( types::DOCTYPE_SYMBOL );
        *doc->mutable_lib_id() = LibIdToProto( symbol->GetLibId() );
        if( !frame()->Prj().IsNullProject() )
        {
            doc->mutable_project()->set_name( frame()->Prj().GetProjectName().ToStdString() );
            doc->mutable_project()->set_path( frame()->Prj().GetProjectDirectory().ToStdString() );
        }
    }
    return response;
}


SYMBOL_EDIT_FRAME* API_HANDLER_SYMBOL::frame() const
{
    return static_cast<SYMBOL_EDIT_FRAME*>( m_frame );
}

bool API_HANDLER_SYMBOL::validateDocumentInternal( const DocumentSpecifier& aDocument ) const
{
    LIB_SYMBOL* symbol = frame()->GetCurSymbol();
    return symbol && aDocument.type() == types::DOCTYPE_SYMBOL
           && aDocument.lib_id().SerializeAsString() == LibIdToProto( symbol->GetLibId() ).SerializeAsString()
           && ( aDocument.project().path().empty()
                || wxFileName::DirName( wxString::FromUTF8( aDocument.project().path() ) )
                           == wxFileName::DirName( frame()->Prj().GetProjectDirectory() ) )
           && ( aDocument.project().name().empty()
                || aDocument.project().name() == frame()->Prj().GetProjectName().ToStdString() );
}

bool API_HANDLER_SYMBOL::documentModified() const
{
    auto* symbol = frame()->GetCurSymbol();
    return frame()->GetScreen()->IsContentModified()
           || ( symbol && frame()->GetLibManager().IsSymbolModified(
                                 symbol->GetName(), symbol->GetLibId().GetLibNickname() ) );
}

HANDLER_RESULT<Empty> API_HANDLER_SYMBOL::saveDocumentInternal( const DocumentSpecifier& )
{
    auto* symbol = frame()->GetCurSymbol();
    const auto& id = symbol->GetLibId();
    if( frame()->IsSymbolFromSchematic() || id.GetLibNickname().empty() )
        return documentError( "SaveDocument requires a library symbol; use SaveCopy for standalone symbols" );
    auto* adapter = PROJECT_SCH::SymbolLibAdapter( &frame()->Prj() );
    if( !adapter->IsSymbolLibWritable( id.GetLibNickname() ) )
        return documentError( "Symbol library is read-only" );
    if( adapter->SaveSymbol( id.GetLibNickname(), new LIB_SYMBOL( *symbol ), true ) != SYMBOL_LIBRARY_ADAPTER::SAVE_OK )
        return documentError( "Could not save symbol" );
    auto& manager = frame()->GetLibManager();
    manager.UpdateSymbol( symbol, id.GetLibNickname() );
    manager.AcceptSymbolBaseline( *symbol, id.GetLibNickname() );
    frame()->GetScreen()->SetContentModified( false );
    frame()->UpdateTitle();
    return Empty();
}

HANDLER_RESULT<Empty> API_HANDLER_SYMBOL::saveCopyInternal(
        const DocumentSpecifier&, const wxString& aPath, bool aIncludeProject )
{
    if( aIncludeProject )
        return documentError( "A standalone symbol library copy does not include a project" );
    auto* adapter = PROJECT_SCH::SymbolLibAdapter( &frame()->Prj() );
    if( auto row = adapter->GetRow( frame()->GetCurSymbol()->GetLibId().GetLibNickname() ) )
    {
        if( wxFileName( LIBRARY_MANAGER::GetFullURI( *row, true ) ) == wxFileName( aPath ) )
            return documentError( "SaveCopy cannot overwrite the open library; use SaveDocument" );
    }
    // A copy is a self-contained single-symbol library, even for inherited symbols.
    const wxString temporary = wxFileName( wxFileName( aPath ).GetPath(),
                                            ".ipc-" + KIID().AsString() + ".kicad_sym" ).GetFullPath();
    try
    {
        SCH_IO_KICAD_SEXPR io;
        io.CreateLibrary( temporary );
        auto flattened = frame()->GetCurSymbol()->Flatten();
        io.SaveSymbol( temporary, flattened.release() );
        if( !wxRenameFile( temporary, aPath, true ) )
        {
            wxRemoveFile( temporary );
            return documentError( "Could not replace destination symbol library" );
        }
    }
    catch( ... )
    {
        if( wxFileExists( temporary ) )
            wxRemoveFile( temporary );
        throw;
    }
    return Empty();
}

HANDLER_RESULT<Empty> API_HANDLER_SYMBOL::revertDocumentInternal( const DocumentSpecifier& )
{
    auto* symbol = frame()->GetCurSymbol();
    const LIB_ID id = symbol->GetLibId();
    if( frame()->IsSymbolFromSchematic() || id.GetLibNickname().empty() )
        return documentError( "RevertDocument requires a symbol backed by a library" );
    auto* adapter = PROJECT_SCH::SymbolLibAdapter( &frame()->Prj() );
    // Revert is an explicit disk read, even when the file's timestamp did not change.
    adapter->ReloadLibraryEntry( id.GetLibNickname() );
    auto status = adapter->LoadLibraryEntry( id.GetLibNickname() );
    if( !status || status->load_status != LOAD_STATUS::LOADED )
        return documentError( "Could not reload symbol library" );
    auto* loaded = adapter->LoadSymbol( id );
    if( !loaded )
        return documentError( "Symbol no longer exists in its library" );
    // Keep other modified symbols and their Undo histories untouched.
    auto replacement = std::make_unique<LIB_SYMBOL>( *loaded );
    auto& manager = frame()->GetLibManager();
    frame()->ClearUndoRedoList();
    manager.AcceptSymbolBaseline( *replacement, id.GetLibNickname() );
    frame()->SetCurSymbol( new LIB_SYMBOL( *manager.GetBufferedSymbol(
                                  id.GetLibItemName(), id.GetLibNickname() ) ), false );
    frame()->GetScreen()->SetContentModified( false );
    frame()->RebuildSymbolUnitAndBodyStyleLists();
    frame()->UpdateMsgPanel();
    frame()->UpdateTitle();
    return Empty();
}
