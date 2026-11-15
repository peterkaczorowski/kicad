/* Copyright The KiCad Developers, see AUTHORS.txt.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <api/api_handler_sch.h>
#include <api/api_handler_symbol.h>
#include <api/api_sch_utils.h>
#include <api/api_utils.h>
#include <sch_edit_frame.h>
#include <sch_sheet.h>
#include <sch_symbol.h>
#include <lib_symbol.h>
#include <project.h>
#include <project_sch.h>
#include <libraries/symbol_library_adapter.h>
#include <symbol_editor/symbol_edit_frame.h>
#include <symbol_editor/lib_symbol_library_manager.h>
#include <sch_io/kicad_sexpr/sch_io_kicad_sexpr.h>
#include <kiway.h>
#include <wx/filename.h>
#include <wx/filefn.h>
#include <set>

using namespace kiapi::schematic::commands;
using namespace kiapi::common;

namespace
{
SYMBOL_EDIT_FRAME* symbolEditor( SCH_EDIT_FRAME* frame, bool create = false )
{
    return dynamic_cast<SYMBOL_EDIT_FRAME*>( frame->Kiway().Player( FRAME_SCH_SYMBOL_EDITOR, create ) );
}
std::string editorBlocked( SYMBOL_EDIT_FRAME* editor, const LIB_ID* destination = nullptr )
{
    if( !editor ) return {};
    if( editor->GetSymbolApiHandler() && editor->GetSymbolApiHandler()->HasPendingCommit() )
        return "Finish the symbol editor API commit first";
    if( editor->IsSyncLibrariesInProgress() || !editor->ToolStackIsEmpty() ) return "Symbol editor is busy";
    if( destination && editor->GetLibManager().IsSymbolModified( destination->GetLibItemName(), destination->GetLibNickname() ) )
        return "The destination symbol has unsaved editor changes";
    if( !destination && editor->IsSymbolFromSchematic() && editor->GetScreen()->IsContentModified() )
        return "Finish the linked schematic-symbol edit before opening a library copy";
    return {};
}
// Update only the persisted symbol. In particular, never rebuild an entire
// LIB_BUFFER: that would discard dirty siblings and break derived-parent links.
void acceptLibraryItem( SYMBOL_EDIT_FRAME* editor, const LIB_SYMBOL& symbol )
{
    if( !editor ) return;
    const LIB_ID id = symbol.GetLibId();
    const bool current = editor->GetCurSymbol() && !editor->IsSymbolFromSchematic()
                         && editor->GetCurSymbol()->GetLibId() == id;
    const int unit = std::min( editor->GetUnit(), symbol.GetUnitCount() );
    const int body = std::min( editor->GetBodyStyle(), symbol.GetBodyStyleCount() );
    auto& manager = editor->GetLibManager();
    manager.UpdateSymbol( const_cast<LIB_SYMBOL*>( &symbol ), id.GetLibNickname() );
    manager.AcceptSymbolBaseline( symbol, id.GetLibNickname() );
    if( current )
    {
        editor->SetCurSymbol( nullptr, false );
        editor->LoadSymbol( id, std::max( 1, unit ), std::max( 1, body ) );
    }
}
LibraryInfo libraryInfo( SYMBOL_LIBRARY_ADAPTER& adapter, const LIBRARY_TABLE_ROW& row )
{
    LibraryInfo out; out.set_nickname( row.Nickname().ToStdString() ); out.set_uri( row.URI().ToStdString() );
    out.set_scope( row.Scope() == LIBRARY_TABLE_SCOPE::PROJECT ? "project" : "global" );
    try
    {
        auto status = adapter.LoadLibraryEntry( row.Nickname() );
        out.set_writable( status && status->load_status == LOAD_STATUS::LOADED
                          && adapter.IsSymbolLibWritable( row.Nickname() ) );
    }
    catch( const IO_ERROR& ) {}
    return out;
}
std::string libraryError( SYMBOL_LIBRARY_ADAPTER& adapter, const wxString& nickname )
{
    auto status = adapter.LoadLibraryEntry( nickname );
    if( !status ) return "Library does not exist in the resolved tables";
    if( status->load_status != LOAD_STATUS::LOADED )
        return status->error ? status->error->message.ToStdString() : "Library could not be loaded";
    return {};
}
SCH_SYMBOL* findSymbol( const SCH_SHEET_PATH& path, const std::string& uuid )
{
    for( SCH_ITEM* item : path.LastScreen()->Items() )
        if( item->m_Uuid.AsStdString() == uuid ) return dynamic_cast<SCH_SYMBOL*>( item );
    return nullptr;
}
}

void API_HANDLER_SCH::registerLibraryHandlers()
{
    registerHandler<SearchSymbolLibraries, SearchSymbolLibrariesResponse>( &API_HANDLER_SCH::handleSearchSymbolLibraries );
    registerHandler<GetSymbolDefinition, SymbolDefinitionResponse>( &API_HANDLER_SCH::handleGetSymbolDefinition );
    registerHandler<CreateSymbolLibrary, CreateSymbolLibraryResponse>( &API_HANDLER_SCH::handleCreateSymbolLibrary );
    registerHandler<SaveSymbolDefinition, SymbolDefinitionResponse>( &API_HANDLER_SCH::handleSaveSymbolDefinition );
    registerHandler<RecoverSchematicSymbol, SymbolDefinitionResponse>( &API_HANDLER_SCH::handleRecoverSchematicSymbol );
    registerHandler<OpenLibrarySymbol, OpenLibrarySymbolResponse>( &API_HANDLER_SCH::handleOpenLibrarySymbol );
}

HANDLER_RESULT<bool> API_HANDLER_SCH::validateLibraryContext( const DocumentSpecifier& document, bool write )
{
    if( document.project().path().empty() || !document.has_sheet_path() || !resolveDocument( document ) )
        return documentError( "Symbol library commands require an explicit open project and sheet UUID path" );
    if( write ) return validateDocumentCommand( document );
    if( auto busy = checkForBusy() ) return tl::unexpected( *busy );
    return true;
}

HANDLER_RESULT<SearchSymbolLibrariesResponse> API_HANDLER_SCH::handleSearchSymbolLibraries(
        const HANDLER_CONTEXT<SearchSymbolLibraries>& context )
{
    const auto& request = context.Request;
    if( auto valid = validateLibraryContext( request.document(), false ); !valid ) return tl::unexpected( valid.error() );
    auto* frame = static_cast<SCH_EDIT_FRAME*>( m_frame );
    auto* adapter = PROJECT_SCH::SymbolLibAdapter( &frame->Prj() );
    SearchSymbolLibrariesResponse response;
    const wxString query = wxString::FromUTF8( request.query() ).Lower();
    std::set<wxString> visited;
    const uint32_t limit = request.limit() ? std::min( request.limit(), 1000u ) : 100;
    try
    {
        for( const auto* row : adapter->Rows( LIBRARY_TABLE_SCOPE::BOTH, true ) )
        {
            const wxString nickname = row->Nickname();
            if( ( !request.library_nickname().empty() && nickname != wxString::FromUTF8( request.library_nickname() ) )
                || !visited.insert( nickname ).second ) continue;
            // Resolve project precedence through the same adapter used for insertion.
            auto resolved = adapter->GetRow( nickname );
            const LIBRARY_TABLE_ROW* actual = resolved ? *resolved : row;
            std::string error = actual->Disabled() ? "Library is disabled" : libraryError( *adapter, nickname );
            if( !error.empty() )
            { auto* failure = response.add_errors(); failure->set_nickname( nickname.ToStdString() ); failure->set_message( error ); continue; }
            const LibraryInfo info = libraryInfo( *adapter, *actual );
            wxString enumerationError;
            auto symbols = adapter->GetSymbols( nickname, SYMBOL_LIBRARY_ADAPTER::SYMBOL_TYPE::ALL_SYMBOLS, &enumerationError );
            if( !enumerationError.empty() )
            { auto* failure = response.add_errors(); failure->set_nickname( nickname.ToStdString() ); failure->set_message( enumerationError.ToStdString() ); continue; }
            for( LIB_SYMBOL* symbol : symbols )
            {
                if( !symbol ) continue;
                if( !query.empty() && !symbol->GetName().Lower().Contains( query )
                    && !symbol->GetKeyWords().Lower().Contains( query ) ) continue;
                uint32_t index = response.total_matches(); response.set_total_matches( index + 1 );
                if( index < request.offset() || response.matches_size() >= int( limit ) ) continue;
                auto* match = response.add_matches(); *match->mutable_lib_id() = LibIdToProto( symbol->GetLibId() );
                *match->mutable_library() = info;
                match->set_description( symbol->GetDescription().ToStdString() ); match->set_keywords( symbol->GetKeyWords().ToStdString() );
            }
        }
    }
    catch( const IO_ERROR& error ) { return documentError( error.What().ToStdString() ); }
    if( !request.library_nickname().empty() && visited.empty() ) return documentError( "Requested symbol library is not registered" );
    return response;
}

HANDLER_RESULT<SymbolDefinitionResponse> API_HANDLER_SCH::handleGetSymbolDefinition(
        const HANDLER_CONTEXT<GetSymbolDefinition>& context )
{
    const auto& request = context.Request;
    if( auto valid = validateLibraryContext( request.document(), false ); !valid ) return tl::unexpected( valid.error() );
    auto* frame = static_cast<SCH_EDIT_FRAME*>( m_frame );
    const LIB_SYMBOL* symbol = nullptr;
    SymbolDefinitionResponse response;
    try
    {
        if( request.has_symbol_id() )
        {
            auto path = resolveDocument( request.document() );
            auto* instance = findSymbol( *path, request.symbol_id().value() );
            if( !instance ) return documentError( "Symbol UUID is not on the selected sheet" );
            if( instance->IsMissingLibSymbol() || !instance->GetLibSymbolRef() )
                return documentError( "The selected symbol has no embedded definition; library substitution is forbidden" );
            symbol = instance->GetLibSymbolRef().get(); response.set_source( "schematic" );
            *response.mutable_source_symbol_id() = request.symbol_id();
        }
        else if( request.has_lib_id() )
        {
            auto* adapter = PROJECT_SCH::SymbolLibAdapter( &frame->Prj() );
            LIB_ID id = LibIdFromProto( request.lib_id() );
            auto error = libraryError( *adapter, id.GetLibNickname() );
            if( !error.empty() ) return documentError( error );
            symbol = adapter->LoadSymbol( id ); response.set_source( "library" );
            if( !symbol ) return documentError( "Symbol does not exist in the selected library" );
        }
        else return documentError( "Choose a library identifier or embedded schematic symbol UUID" );
        std::string error; auto definition = PackLibrarySymbol( *symbol, error );
        if( !definition ) return documentError( error );
        *response.mutable_definition() = *definition; *response.mutable_source_document() = request.document();
        return response;
    }
    catch( const IO_ERROR& error ) { return documentError( error.What().ToStdString() ); }
}

HANDLER_RESULT<CreateSymbolLibraryResponse> API_HANDLER_SCH::handleCreateSymbolLibrary(
        const HANDLER_CONTEXT<CreateSymbolLibrary>& context )
{
    const auto& request = context.Request;
    if( auto valid = validateLibraryContext( request.document(), true ); !valid ) return tl::unexpected( valid.error() );
    auto* frame = static_cast<SCH_EDIT_FRAME*>( m_frame );
    auto* adapter = PROJECT_SCH::SymbolLibAdapter( &frame->Prj() );
    if( auto blocked = editorBlocked( symbolEditor( frame ) ); !blocked.empty() ) return documentError( blocked, ApiStatusCode::AS_BUSY );
    const wxString nickname = wxString::FromUTF8( request.nickname() );
    if( nickname.empty() || LIB_ID::FindIllegalLibraryNameChar( nickname ) || adapter->HasLibrary( nickname ) )
        return documentError( "Invalid or already registered library nickname" );
    wxFileName destination( wxString::FromUTF8( request.path() ) );
    destination.MakeAbsolute( frame->Prj().GetProjectDirectory() ); destination.Normalize( wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE );
    if( destination.GetExt() != "kicad_sym" || !destination.DirExists() || !wxFileName::IsDirWritable( destination.GetPath() ) )
        return documentError( "A writable existing directory and .kicad_sym path are required" );
    if( adapter->FindLibraryByURI( destination.GetFullPath() ) ) return documentError( "This library file is already registered" );
    if( destination.FileExists() != request.link_existing() ) return documentError( "Destination exists, or link_existing target is missing" );
    auto table = adapter->ProjectTable();
    if( !table || ( *table )->IsReadOnly() ) return documentError( "Project symbol library table is unavailable or read-only" );
    LIBRARY_TABLE candidate( **table );
    auto& row = candidate.InsertRow(); row.SetNickname( nickname ); row.SetType( "KiCad" ); row.SetScope( LIBRARY_TABLE_SCOPE::PROJECT );
    wxString uri = destination.GetFullPath();
    const wxString projectPrefix = wxFileName::DirName( frame->Prj().GetProjectDirectory() ).GetPathWithSep();
    if( uri.StartsWith( projectPrefix ) ) uri = "${KIPRJMOD}/" + uri.Mid( projectPrefix.length() );
    row.SetURI( uri );
    bool created = false;
    bool tablePersisted = false;
    try
    {
        SCH_IO_KICAD_SEXPR io;
        if( request.link_existing() )
        { wxArrayString names; io.EnumerateSymbolLib( names, destination.GetFullPath() ); }
        else { io.CreateLibrary( destination.GetFullPath() ); created = true; }
        if( auto saved = candidate.Save(); !saved )
        { if( created ) wxRemoveFile( destination.GetFullPath() ); return documentError( saved.error().message.ToStdString() ); }
        tablePersisted = true;
        adapter->Manager().ReloadTables( LIBRARY_TABLE_SCOPE::PROJECT, { LIBRARY_TABLE_TYPE::SYMBOL } );
        auto resultRow = adapter->GetRow( nickname );
        if( !resultRow ) return documentError( "Library persisted but table reload failed" );
        CreateSymbolLibraryResponse response; *response.mutable_library() = libraryInfo( *adapter, **resultRow ); return response;
    }
    catch( const IO_ERROR& error )
    { if( created && !tablePersisted ) wxRemoveFile( destination.GetFullPath() ); return documentError( error.What().ToStdString() ); }
}

HANDLER_RESULT<SymbolDefinitionResponse> API_HANDLER_SCH::handleSaveSymbolDefinition(
        const HANDLER_CONTEXT<SaveSymbolDefinition>& context )
{
    const auto& request = context.Request;
    if( auto valid = validateLibraryContext( request.document(), true ); !valid ) return tl::unexpected( valid.error() );
    auto* frame = static_cast<SCH_EDIT_FRAME*>( m_frame ); auto* adapter = PROJECT_SCH::SymbolLibAdapter( &frame->Prj() );
    std::string error; auto symbol = UnpackLibrarySymbol( request.definition(), error );
    if( !symbol ) return documentError( error );
    LIB_ID id = symbol->GetLibId();
    auto row = adapter->GetRow( id.GetLibNickname() );
    if( !row || ( *row )->Disabled() || ( *row )->Type() != "KiCad" ) return documentError( "Select a registered writable KiCad-format library" );
    wxString temporary;
    try
    {
        if( auto loadError = libraryError( *adapter, id.GetLibNickname() ); !loadError.empty() ) return documentError( loadError );
        if( !adapter->IsSymbolLibWritable( id.GetLibNickname() ) ) return documentError( "Symbol library is read-only" );
        if( auto blocked = editorBlocked( symbolEditor( frame ), &id ); !blocked.empty() ) return documentError( blocked, ApiStatusCode::AS_BUSY );
        if( !request.overwrite() && adapter->LoadSymbol( id ) ) return documentError( "Destination lib_id already exists; overwrite must be explicit" );
        const wxString path = LIBRARY_MANAGER::GetFullURI( *row, true );
        temporary = wxFileName( wxFileName( path ).GetPath(), ".ipc-" + KIID().AsString() + ".kicad_sym" ).GetFullPath();
        if( !wxCopyFile( path, temporary, false ) ) return documentError( "Could not stage an atomic native-library update" );
        {
            SCH_IO_KICAD_SEXPR io;
            // The native cache owns the symbol, including when disk persistence throws.
            io.SaveSymbol( temporary, symbol.release() );
        }
        if( !wxRenameFile( temporary, path, true ) )
        { wxRemoveFile( temporary ); return documentError( "Could not replace symbol library atomically" ); }
        adapter->ReloadLibraryEntry( id.GetLibNickname() );
        adapter->LoadLibraryEntry( id.GetLibNickname() );
        if( auto* persisted = adapter->LoadSymbol( id ) ) acceptLibraryItem( symbolEditor( frame ), *persisted );
        HANDLER_CONTEXT<GetSymbolDefinition> read;
        *read.Request.mutable_document() = request.document(); *read.Request.mutable_lib_id() = LibIdToProto( id );
        return handleGetSymbolDefinition( read );
    }
    catch( const IO_ERROR& exception )
    { if( !temporary.empty() && wxFileExists( temporary ) ) wxRemoveFile( temporary ); return documentError( exception.What().ToStdString() ); }
}

HANDLER_RESULT<OpenLibrarySymbolResponse> API_HANDLER_SCH::handleOpenLibrarySymbol(
        const HANDLER_CONTEXT<OpenLibrarySymbol>& context )
{
    const auto& request = context.Request;
    if( auto valid = validateLibraryContext( request.document(), true ); !valid ) return tl::unexpected( valid.error() );
    auto* frame = static_cast<SCH_EDIT_FRAME*>( m_frame );
    if( auto blocked = editorBlocked( symbolEditor( frame ) ); !blocked.empty() ) return documentError( blocked, ApiStatusCode::AS_BUSY );
    HANDLER_CONTEXT<GetSymbolDefinition> read;
    *read.Request.mutable_document() = request.document(); *read.Request.mutable_lib_id() = request.lib_id();
    if( auto definition = handleGetSymbolDefinition( read ); !definition ) return tl::unexpected( definition.error() );
    auto* editor = symbolEditor( frame, true );
    if( !editor ) return documentError( "Could not create the symbol editor" );
    const LIB_ID id = LibIdFromProto( request.lib_id() );
    if( !editor->GetLibManager().IsSymbolModified( id.GetLibItemName(), id.GetLibNickname() ) )
    {
        auto* adapter = PROJECT_SCH::SymbolLibAdapter( &frame->Prj() );
        if( auto* native = adapter->LoadSymbol( id ) ) acceptLibraryItem( editor, *native );
    }
    editor->SyncLibraries( false );
    if( !editor->LoadSymbol( id, 1, 1 ) ) return documentError( "Could not open the library symbol" );
    editor->Show( true ); editor->Raise();
    OpenLibrarySymbolResponse response;
    response.mutable_document()->set_type( types::DOCTYPE_SYMBOL );
    *response.mutable_document()->mutable_project() = request.document().project();
    *response.mutable_document()->mutable_lib_id() = request.lib_id();
    return response;
}

HANDLER_RESULT<SymbolDefinitionResponse> API_HANDLER_SCH::handleRecoverSchematicSymbol(
        const HANDLER_CONTEXT<RecoverSchematicSymbol>& context )
{
    const auto& request = context.Request;
    if( auto valid = validateLibraryContext( request.document(), true ); !valid ) return tl::unexpected( valid.error() );
    auto* frame = static_cast<SCH_EDIT_FRAME*>( m_frame );
    if( request.open_in_editor() )
        if( auto blocked = editorBlocked( symbolEditor( frame ) ); !blocked.empty() ) return documentError( blocked, ApiStatusCode::AS_BUSY );
    HANDLER_CONTEXT<GetSymbolDefinition> read;
    *read.Request.mutable_document() = request.document(); *read.Request.mutable_symbol_id() = request.symbol_id();
    auto embedded = handleGetSymbolDefinition( read );
    if( !embedded ) return tl::unexpected( embedded.error() );
    HANDLER_CONTEXT<SaveSymbolDefinition> save;
    *save.Request.mutable_document() = request.document(); *save.Request.mutable_definition() = embedded->definition();
    *save.Request.mutable_definition()->mutable_lib_id() = request.target_lib_id();
    // Recovery never overwrites an existing definition or relinks the source instance.
    auto persisted = handleSaveSymbolDefinition( save );
    if( !persisted ) return tl::unexpected( persisted.error() );
    persisted->set_source( "schematic" ); *persisted->mutable_source_symbol_id() = request.symbol_id();
    if( request.open_in_editor() )
    {
        HANDLER_CONTEXT<OpenLibrarySymbol> open;
        *open.Request.mutable_document() = request.document(); *open.Request.mutable_lib_id() = request.target_lib_id();
        auto opened = handleOpenLibrarySymbol( open );
        if( !opened ) return tl::unexpected( opened.error() );
        *persisted->mutable_editor_document() = opened->document();
    }
    return persisted;
}
