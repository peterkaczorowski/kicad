/* Copyright The KiCad Developers, see AUTHORS.txt. SPDX-License-Identifier: GPL-3.0-or-later */
#include <api/api_handler_pcb.h>
#include <api/api_handler_footprint.h>
#include <api/api_pcb_library_utils.h>
#include <api/api_utils.h>
#include <api/api_server.h>
#include <board.h>
#include <footprint.h>
#include <footprint_library_adapter.h>
#include <footprint_edit_frame.h>
#include <pcb_edit_frame.h>
#include <pcb_draw_panel_gal.h>
#include <pcb_io/kicad_sexpr/pcb_io_kicad_sexpr.h>
#include <project_pcb.h>
#include <project.h>
#include <pgm_base.h>
#include <kiway.h>
#include <view/view.h>
#include <wx/filename.h>
#include <wx/filefn.h>
#include <wx/toplevel.h>
#include <wx/log.h>
#include <algorithm>
#include <set>
#include <functional>
#include <vector>

using namespace kiapi::common;
using namespace kiapi::board::commands;

namespace
{
// Keep rollback armed until both native persistence and cache verification finish.
// A failed filesystem restore retains the recovery directory for manual recovery.
class ROLLBACK
{
public:
    explicit ROLLBACK( std::function<void()> restore ) : m_restore( std::move( restore ) ) {}
    ~ROLLBACK() { if( m_restore ) { try { m_restore(); } catch( ... ) {} } }
    void Accept() { m_restore = {}; }
private:
    std::function<void()> m_restore;
};

std::vector<FOOTPRINT_EDIT_FRAME*> footprintEditors()
{
    std::vector<FOOTPRINT_EDIT_FRAME*> result;
    for( wxWindow* window : wxTopLevelWindows )
        if( auto* editor = dynamic_cast<FOOTPRINT_EDIT_FRAME*>( window ) ) result.push_back( editor );
    return result;
}

bool matchesDestination( FOOTPRINT_EDIT_FRAME* editor, PCB_EDIT_FRAME* frame, const LIB_ID& target )
{
    auto* current = editor->GetBoard()->GetFirstFootprint();
    if( !current || editor->IsCurrentFPFromBoard()
        || current->GetFPID().GetLibItemName() != target.GetLibItemName() ) return false;
    auto source = PROJECT_PCB::FootprintLibAdapter( &frame->Prj() )->GetRow( target.GetLibNickname() );
    auto destination = PROJECT_PCB::FootprintLibAdapter( &editor->Prj() )->GetRow( current->GetFPID().GetLibNickname() );
    return source && destination
           && LIBRARY_MANAGER::UrisAreEquivalent( LIBRARY_MANAGER::GetFullURI( *source, true ),
                                                 LIBRARY_MANAGER::GetFullURI( *destination, true ) );
}

std::string editorBlocked( PCB_EDIT_FRAME* frame, const LIB_ID* target = nullptr )
{
    for( const auto* handler : Pgm().GetApiServer().Handlers() )
        if( auto* footprint = dynamic_cast<const API_HANDLER_FOOTPRINT*>( handler );
            footprint && footprint->HasPendingCommit() )
            return "Finish the footprint editor API commit first";
    for( auto* editor : footprintEditors() )
    {
        if( !editor->ToolStackIsEmpty() || !editor->CanAcceptApiCommands() )
            return "Footprint editor has an active tool or operation";
        if( target && matchesDestination( editor, frame, *target )
            && editor->GetScreen()->IsContentModified() )
            return "The destination footprint has unsaved editor changes";
    }
    return {};
}

std::string libraryError( FOOTPRINT_LIBRARY_ADAPTER& adapter, const wxString& nickname )
{
    auto status = adapter.LoadLibraryEntry( nickname );
    if( !status ) return "Footprint library is not registered";
    if( status->load_status != LOAD_STATUS::LOADED )
        return status->error ? status->error->message.ToStdString() : "Footprint library could not be loaded";
    return {};
}

FootprintLibraryInfo libraryInfo( FOOTPRINT_LIBRARY_ADAPTER& adapter, const LIBRARY_TABLE_ROW& row )
{
    FootprintLibraryInfo result;
    result.set_nickname( row.Nickname().ToStdString() );
    result.set_uri( row.URI().ToStdString() );
    result.set_scope( row.Scope() == LIBRARY_TABLE_SCOPE::PROJECT ? "project" : "global" );
    try
    {
        result.set_writable( row.Scope() == LIBRARY_TABLE_SCOPE::PROJECT && row.Type() == "KiCad"
                             && !row.Disabled() && libraryError( adapter, row.Nickname() ).empty()
                             && adapter.IsFootprintLibWritable( row.Nickname() ) );
    }
    catch( const IO_ERROR& ) {}
    return result;
}

bool safeId( const LIB_ID& id )
{
    const wxString name = id.GetLibItemName().wx_str();
    return id.IsValid() && !LIB_ID::FindIllegalLibraryNameChar( id.GetLibNickname() )
           && name != "." && name != ".." && name.find_first_of( "/\\:" ) == wxString::npos;
}

void refreshEditor( PCB_EDIT_FRAME* frame, FOOTPRINT_LIBRARY_ADAPTER& adapter, const LIB_ID& id )
{
    for( auto* editor : footprintEditors() )
    {
        if( matchesDestination( editor, frame, id ) )
        {
            const LIB_ID editorId = editor->GetBoard()->GetFirstFootprint()->GetFPID();
            auto replacement = std::unique_ptr<FOOTPRINT>( adapter.LoadFootprint( id, true ) );
            if( replacement )
            {
                const double scale = editor->GetCanvas()->GetView()->GetScale();
                const auto center = editor->GetCanvas()->GetView()->GetCenter();
                // A different project/nickname can refer to the same physical library.
                replacement->SetFPID( editorId );
                editor->AddFootprintToBoard( replacement.release() );
                editor->GetScreen()->SetContentModified( false );
                editor->UpdateView();
                editor->GetCanvas()->GetView()->SetScale( scale );
                editor->GetCanvas()->GetView()->SetCenter( center );
                editor->UpdateTitle();
            }
        }
        editor->SyncLibraryTree( false );
    }
}
}

void API_HANDLER_PCB::registerFootprintLibraryHandlers()
{
    registerHandler<SearchFootprintLibraries, SearchFootprintLibrariesResponse>( &API_HANDLER_PCB::handleSearchFootprintLibraries );
    registerHandler<GetFootprintDefinition, FootprintDefinitionResponse>( &API_HANDLER_PCB::handleGetFootprintDefinition );
    registerHandler<CreateFootprintLibrary, CreateFootprintLibraryResponse>( &API_HANDLER_PCB::handleCreateFootprintLibrary );
    registerHandler<SaveFootprintDefinition, FootprintDefinitionResponse>( &API_HANDLER_PCB::handleSaveFootprintDefinition );
    registerHandler<RecoverBoardFootprint, FootprintDefinitionResponse>( &API_HANDLER_PCB::handleRecoverBoardFootprint );
}

HANDLER_RESULT<bool> API_HANDLER_PCB::validateFootprintLibraryContext( const DocumentSpecifier& document, bool write )
{
    if( auto valid = validateDocument( document ); !valid ) return tl::unexpected( valid.error() );
    if( document.project().path().empty() || document.project().name().empty()
        || document.board_filename().empty() || frame()->Prj().IsNullProject() )
        return documentError( "Footprint library commands require an explicit open PCB project/document" );
    if( write ) return validateDocumentCommand( document );
    if( auto busy = checkForBusy() ) return tl::unexpected( *busy );
    return true;
}

HANDLER_RESULT<SearchFootprintLibrariesResponse> API_HANDLER_PCB::handleSearchFootprintLibraries(
        const HANDLER_CONTEXT<SearchFootprintLibraries>& context )
{
    const auto& request = context.Request;
    if( auto valid = validateFootprintLibraryContext( request.document(), false ); !valid ) return tl::unexpected( valid.error() );
    wxLogNull quiet; // Filesystem/plugin failures belong in the IPC response, never a log dialog.
    auto* adapter = PROJECT_PCB::FootprintLibAdapter( &frame()->Prj() );
    SearchFootprintLibrariesResponse response;
    const wxString query = wxString::FromUTF8( request.query() ).Lower();
    const wxString requested = wxString::FromUTF8( request.library_nickname() );
    const uint32_t limit = request.limit() ? std::min( request.limit(), 1000u ) : 100;
    std::set<wxString> visited;
    for( const auto* row : adapter->Rows( LIBRARY_TABLE_SCOPE::BOTH, true ) )
    {
        const wxString nickname = row->Nickname();
        if( ( !requested.empty() && nickname != requested ) || !visited.insert( nickname ).second ) continue;
        auto resolved = adapter->GetRow( nickname );
        const auto* actual = resolved ? *resolved : row;
        try
        {
            const auto error = actual->Disabled() ? std::string( "Library is disabled" ) : libraryError( *adapter, nickname );
            if( !error.empty() )
            {
                auto* failure = response.add_errors(); failure->set_nickname( nickname.ToStdString() );
                failure->set_message( error ); continue;
            }
            adapter->RefreshLibraryIfChanged( nickname );
            auto names = adapter->GetFootprintNames( nickname );
            std::sort( names.begin(), names.end() );
            const auto info = libraryInfo( *adapter, *actual );
            for( const auto& name : names )
            {
                auto footprint = std::unique_ptr<FOOTPRINT>( adapter->LoadFootprint( nickname, name, true ) );
                if( !footprint ) THROW_IO_ERROR( "Footprint disappeared while enumerating library" );
                if( !query.empty() && !name.Lower().Contains( query ) && !nickname.Lower().Contains( query )
                    && !footprint->GetKeywords().Lower().Contains( query ) && !footprint->GetLibDescription().Lower().Contains( query ) ) continue;
                const uint32_t index = response.total_matches(); response.set_total_matches( index + 1 );
                if( index < request.offset() || response.matches_size() >= int( limit ) ) continue;
                auto* match = response.add_matches();
                *match->mutable_lib_id() = LibIdToProto( LIB_ID( nickname, name ) );
                *match->mutable_library() = info;
                match->set_description( footprint->GetLibDescription().ToStdString() );
                match->set_keywords( footprint->GetKeywords().ToStdString() );
            }
        }
        catch( const IO_ERROR& error )
        {
            auto* failure = response.add_errors(); failure->set_nickname( nickname.ToStdString() );
            failure->set_message( error.What().ToStdString() );
        }
    }
    if( !requested.empty() && visited.empty() ) return documentError( "Requested footprint library is not registered" );
    return response;
}

HANDLER_RESULT<FootprintDefinitionResponse> API_HANDLER_PCB::handleGetFootprintDefinition(
        const HANDLER_CONTEXT<GetFootprintDefinition>& context )
{
    const auto& request = context.Request;
    if( auto valid = validateFootprintLibraryContext( request.document(), false ); !valid ) return tl::unexpected( valid.error() );
    wxLogNull quiet;
    FootprintDefinitionResponse response;
    *response.mutable_source_document() = request.document();
    try
    {
        if( request.has_footprint_id() )
        {
            auto item = getItemById( KIID( request.footprint_id().value() ) );
            auto* footprint = item ? dynamic_cast<FOOTPRINT*>( *item ) : nullptr;
            if( !footprint ) return documentError( "Footprint UUID does not exist on the selected live board" );
            *response.mutable_definition() = kiapi::board::PackFootprintDefinition( *footprint );
            *response.mutable_source_footprint_id() = request.footprint_id();
            response.set_source( "board" );
        }
        else if( request.has_lib_id() )
        {
            const LIB_ID id = LibIdFromProto( request.lib_id() );
            if( !safeId( id ) ) return documentError( "A complete safe library:footprint identifier is required" );
            auto* adapter = PROJECT_PCB::FootprintLibAdapter( &frame()->Prj() );
            if( auto error = libraryError( *adapter, id.GetLibNickname() ); !error.empty() ) return documentError( error );
            adapter->RefreshLibraryIfChanged( id.GetLibNickname() );
            auto footprint = std::unique_ptr<FOOTPRINT>( adapter->LoadFootprint( id, true ) );
            if( !footprint ) return documentError( "Footprint does not exist in the selected library" );
            footprint->SetFPID( id );
            *response.mutable_definition() = kiapi::board::PackFootprintDefinition( *footprint );
            response.set_source( "library" );
        }
        else return documentError( "Select a library identifier or a live footprint UUID" );
        return response;
    }
    catch( const IO_ERROR& error ) { return documentError( error.What().ToStdString() ); }
    catch( const std::exception& error ) { return documentError( error.what() ); }
}

HANDLER_RESULT<CreateFootprintLibraryResponse> API_HANDLER_PCB::handleCreateFootprintLibrary(
        const HANDLER_CONTEXT<CreateFootprintLibrary>& context )
{
    const auto& request = context.Request;
    if( auto valid = validateFootprintLibraryContext( request.document(), true ); !valid ) return tl::unexpected( valid.error() );
    wxLogNull quiet;
    if( auto error = editorBlocked( frame() ); !error.empty() ) return documentError( error, ApiStatusCode::AS_BUSY );
    auto* adapter = PROJECT_PCB::FootprintLibAdapter( &frame()->Prj() );
    const wxString nickname = wxString::FromUTF8( request.nickname() );
    wxFileName destination( wxString::FromUTF8( request.path() ) );
    destination.MakeAbsolute( frame()->Prj().GetProjectDirectory() );
    destination.Normalize( wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE );
    const wxString path = destination.GetFullPath();
    if( nickname.empty() || LIB_ID::FindIllegalLibraryNameChar( nickname ) || destination.GetExt() != "pretty"
        || !destination.DirExists() || !wxFileName::IsDirWritable( destination.GetPath() ) )
        return documentError( "Select a safe nickname and .pretty path with a writable existing parent" );
    if( auto existing = adapter->GetRow( nickname ) )
    {
        if( request.link_existing() && ( *existing )->Scope() == LIBRARY_TABLE_SCOPE::PROJECT
            && !( *existing )->Disabled() && ( *existing )->Type() == "KiCad"
            && LIBRARY_MANAGER::UrisAreEquivalent( LIBRARY_MANAGER::GetFullURI( *existing, true ), path ) )
        {
            if( auto error = libraryError( *adapter, nickname ); !error.empty() ) return documentError( error );
            CreateFootprintLibraryResponse response; *response.mutable_library() = libraryInfo( *adapter, **existing ); return response;
        }
        return documentError( "Library nickname is already registered" );
    }
    if( adapter->FindLibraryByURI( path ) ) return documentError( "This library URI is already registered" );
    if( wxDirExists( path ) != request.link_existing() || wxFileExists( path ) )
        return documentError( "Destination exists, or explicit link_existing target is missing" );
    const wxString tablePath = wxFileName( frame()->Prj().GetProjectDirectory(), "fp-lib-table" ).GetFullPath();
    const bool hadTable = wxFileExists( tablePath );
    const wxString backup = tablePath + ".ipc-" + KIID().AsString();
    bool created = false, tablePersisted = false, backedUp = false;
    try
    {
        auto table = adapter->ProjectTable();
        auto candidate = table ? std::make_unique<LIBRARY_TABLE>( **table )
                               : std::make_unique<LIBRARY_TABLE>( wxFileName( tablePath ), LIBRARY_TABLE_SCOPE::PROJECT, LIBRARY_TABLE_TYPE::FOOTPRINT );
        if( candidate->IsReadOnly() || ( hadTable && !candidate->IsOk() ) )
            return documentError( "Project footprint library table is invalid or read-only" );
        candidate->SetPath( tablePath ); candidate->SetType( LIBRARY_TABLE_TYPE::FOOTPRINT );
        candidate->SetScope( LIBRARY_TABLE_SCOPE::PROJECT ); candidate->SetOk();
        auto& row = candidate->InsertRow(); row.SetNickname( nickname ); row.SetType( "KiCad" );
        row.SetScope( LIBRARY_TABLE_SCOPE::PROJECT );
        wxString uri = path;
        const auto prefix = wxFileName::DirName( frame()->Prj().GetProjectDirectory() ).GetPathWithSep();
        if( uri.StartsWith( prefix ) ) uri = "${KIPRJMOD}/" + uri.Mid( prefix.length() );
        row.SetURI( uri );
        if( hadTable )
        {
            if( !wxCopyFile( tablePath, backup, false ) ) return documentError( "Could not back up the project library table" );
            backedUp = true;
        }
        PCB_IO_KICAD_SEXPR io;
        if( request.link_existing() ) { wxArrayString names; io.FootprintEnumerate( names, path, false ); }
        else { io.CreateLibrary( path ); created = true; }
        if( auto saved = candidate->Save(); !saved ) THROW_IO_ERROR( saved.error().message );
        tablePersisted = true;
        adapter->Manager().ReloadTables( LIBRARY_TABLE_SCOPE::PROJECT, { LIBRARY_TABLE_TYPE::FOOTPRINT } );
        auto actual = adapter->GetRow( nickname );
        if( !actual || !libraryError( *adapter, nickname ).empty() ) THROW_IO_ERROR( "Footprint library table reload failed" );
        CreateFootprintLibraryResponse response; *response.mutable_library() = libraryInfo( *adapter, **actual );
        for( auto* editor : footprintEditors() ) editor->SyncLibraryTree( false );
        if( backedUp ) wxRemoveFile( backup );
        return response;
    }
    catch( const IO_ERROR& error )
    {
        if( tablePersisted )
        {
            if( backedUp ) wxRenameFile( backup, tablePath, true );
            else wxRemoveFile( tablePath );
            adapter->Manager().ReloadTables( LIBRARY_TABLE_SCOPE::PROJECT, { LIBRARY_TABLE_TYPE::FOOTPRINT } );
        }
        if( created ) wxRmdir( path );
        if( wxFileExists( backup ) ) wxRemoveFile( backup );
        return documentError( error.What().ToStdString() );
    }
}

HANDLER_RESULT<FootprintDefinitionResponse> API_HANDLER_PCB::handleSaveFootprintDefinition(
        const HANDLER_CONTEXT<SaveFootprintDefinition>& context )
{
    const auto& request = context.Request;
    if( auto valid = validateFootprintLibraryContext( request.document(), true ); !valid ) return tl::unexpected( valid.error() );
    wxLogNull quiet;
    std::string error;
    std::unique_ptr<FOOTPRINT> footprint;
    try { footprint = kiapi::board::UnpackFootprintDefinition( request.definition(), error ); }
    catch( const IO_ERROR& exception ) { return documentError( exception.What().ToStdString() ); }
    catch( const std::exception& exception ) { return documentError( exception.what() ); }
    if( !footprint ) return documentError( error );
    const LIB_ID id = footprint->GetFPID();
    auto* adapter = PROJECT_PCB::FootprintLibAdapter( &frame()->Prj() );
    auto row = adapter->GetRow( id.GetLibNickname() );
    if( !row || ( *row )->Disabled() || ( *row )->Type() != "KiCad" || ( *row )->Scope() != LIBRARY_TABLE_SCOPE::PROJECT )
        return documentError( "Select a registered writable KiCad project footprint library" );
    if( auto blocked = editorBlocked( frame(), &id ); !blocked.empty() ) return documentError( blocked, ApiStatusCode::AS_BUSY );
    wxString temporary, target, backup;
    bool replaced = false, hadTarget = false;
    ROLLBACK rollback( [&]()
    {
        if( replaced )
        {
            if( hadTarget )
            {
                if( !wxRenameFile( backup, target, true ) ) return; // retain recovery directory
            }
            else if( !wxRemoveFile( target ) ) return;
            adapter->ReloadLibraryEntry( id.GetLibNickname() );
        }
        if( !temporary.empty() && wxDirExists( temporary ) )
            wxFileName::Rmdir( temporary, wxPATH_RMDIR_RECURSIVE );
    } );
    try
    {
        if( auto failure = libraryError( *adapter, id.GetLibNickname() ); !failure.empty() ) return documentError( failure );
        if( !adapter->IsFootprintLibWritable( id.GetLibNickname() ) ) return documentError( "Footprint library is read-only" );
        const wxString libraryPath = LIBRARY_MANAGER::GetFullURI( *row, true );
        target = wxFileName( libraryPath, id.GetLibItemName().wx_str() + ".kicad_mod" ).GetFullPath();
        hadTarget = wxFileExists( target );
        if( wxFileExists( target ) && !request.overwrite() ) return documentError( "Destination exists; overwrite must be explicit" );
        if( wxFileExists( target ) && !wxIsWritable( target ) ) return documentError( "Destination footprint file is read-only" );
        temporary = wxFileName( libraryPath, ".ipc-" + KIID().AsString() + ".pretty" ).GetFullPath();
        PCB_IO_KICAD_SEXPR io;
        io.CreateLibrary( temporary );
        io.FootprintSave( temporary, footprint.get() );
        const wxString staged = wxFileName( temporary, id.GetLibItemName().wx_str() + ".kicad_mod" ).GetFullPath();
        // Reparse before committing, using the same native reader as placement.
        auto checked = std::unique_ptr<FOOTPRINT>( io.FootprintLoad( temporary, id.GetLibItemName().wx_str(), true ) );
        if( !checked ) THROW_IO_ERROR( "Native footprint roundtrip failed" );
        if( wxFileExists( target ) )
        {
            backup = wxFileName( temporary, "original.backup" ).GetFullPath();
            if( !wxCopyFile( target, backup, false ) ) THROW_IO_ERROR( "Could not back up destination footprint" );
            wxStructStat permissions;
            if( wxStat( target, &permissions ) == 0 ) wxChmod( staged, permissions.st_mode & 0777 );
        }
        if( !wxRenameFile( staged, target, request.overwrite() ) ) THROW_IO_ERROR( "Could not commit footprint file atomically" );
        replaced = true;
        adapter->ReloadLibraryEntry( id.GetLibNickname() ); adapter->LoadLibraryEntry( id.GetLibNickname() );
        HANDLER_CONTEXT<GetFootprintDefinition> read;
        *read.Request.mutable_document() = request.document(); *read.Request.mutable_lib_id() = LibIdToProto( id );
        auto result = handleGetFootprintDefinition( read );
        if( !result ) return tl::unexpected( result.error() );
        refreshEditor( frame(), *adapter, id );
        rollback.Accept();
        wxFileName::Rmdir( temporary, wxPATH_RMDIR_RECURSIVE );
        return result;
    }
    catch( const IO_ERROR& exception )
    {
        return documentError( exception.What().ToStdString() );
    }
    catch( const std::exception& exception ) { return documentError( exception.what() ); }
}

HANDLER_RESULT<FootprintDefinitionResponse> API_HANDLER_PCB::handleRecoverBoardFootprint(
        const HANDLER_CONTEXT<RecoverBoardFootprint>& context )
{
    HANDLER_CONTEXT<GetFootprintDefinition> read;
    *read.Request.mutable_document() = context.Request.document();
    *read.Request.mutable_footprint_id() = context.Request.footprint_id();
    auto source = handleGetFootprintDefinition( read );
    if( !source ) return tl::unexpected( source.error() );
    HANDLER_CONTEXT<SaveFootprintDefinition> save;
    save.ClientName = context.ClientName;
    *save.Request.mutable_document() = context.Request.document();
    *save.Request.mutable_definition() = source->definition();
    *save.Request.mutable_definition()->mutable_definition()->mutable_id() = context.Request.target_lib_id();
    save.Request.set_overwrite( context.Request.overwrite() );
    auto persisted = handleSaveFootprintDefinition( save );
    if( !persisted ) return tl::unexpected( persisted.error() );
    persisted->set_source( "board" );
    *persisted->mutable_source_footprint_id() = context.Request.footprint_id();
    return persisted;
}
