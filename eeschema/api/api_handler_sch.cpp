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
#include <api/schematic/page_settings.pb.h>
#include <sch_page_format_undo.h>
#include <cmath>
#include <api/api_sch_utils.h>
#include <api/api_utils.h>
#include <api/api_enums.h>
#include <magic_enum.hpp>
#include <sch_commit.h>
#include <sch_edit_frame.h>
#include <wx/filename.h>
#include <sch_sheet.h>
#include <sch_sheet_pin.h>
#include <project.h>
#include <set>
#include <sch_line.h>
#include <sch_symbol.h>
#include <lib_symbol.h>
#include <project_sch.h>
#include <libraries/symbol_library_adapter.h>
#include <refdes_utils.h>
#include <validators.h>
#include <sch_commit_flags.h>
#include <view/view.h>
#include <google/protobuf/util/field_mask_util.h>
#include <sch_io/kicad_sexpr/sch_io_kicad_sexpr.h>
#include <wx/file.h>
#include <wx/filefn.h>
#include <tools/sch_actions.h>
#include <tool/tool_manager.h>

#include <api/common/types/base_types.pb.h>

using namespace kiapi::common::commands;
using kiapi::common::types::CommandStatus;
using kiapi::common::types::DocumentType;
using kiapi::common::types::ItemRequestStatus;


namespace
{
// Screens own live additions, and the undo list owns them after Push.  A dropped
// API transaction detaches them, so it must also release their storage.
class SCHEMATIC_API_COMMIT : public SCH_COMMIT
{
public:
    explicit SCHEMATIC_API_COMMIT( SCH_EDIT_FRAME* frame ) : SCH_COMMIT( frame ), m_frame( frame ) {}
    void Track( SCH_ITEM* item ) { m_added.push_back( item ); }
    void Push( const wxString& message, int flags = 0 ) override
    {
        SCH_COMMIT::Push( message, flags );
        m_added.clear();
    }
    void Revert() override
    {
        // A user may have entered a newly created sheet while the transaction
        // was open. Leave that subtree before its screen is detached.
        SCH_SHEET_PATH safePath = m_frame->GetCurrentSheet();
        for( size_t i = 1; i < safePath.size(); ++i )
        {
            if( std::find( m_added.begin(), m_added.end(), safePath.at( i ) ) != m_added.end() )
            {
                while( safePath.size() > i )
                    safePath.pop_back();
                m_frame->GetToolManager()->RunAction<SCH_SHEET_PATH*>( SCH_ACTIONS::changeSheet,
                                                                       &safePath );
                break;
            }
        }
        SCH_COMMIT::Revert();
        for( SCH_ITEM* item : m_added )
            delete item;
        m_added.clear();
    }
private:
    SCH_EDIT_FRAME* m_frame;
    std::vector<SCH_ITEM*> m_added;
};
}


API_HANDLER_SCH::API_HANDLER_SCH( SCH_EDIT_FRAME* aFrame ) :
        API_HANDLER_EDITOR( aFrame )
{
    registerLibraryHandlers();
    registerPcbExportHandler();
    registerConnectivityHandlers();
    registerNetRenameHandlers();
    registerPageSettingsHandlers();
    registerHandler<kiapi::schematic::commands::CreateSheet, kiapi::schematic::commands::CreateSheetResponse>(
            &API_HANDLER_SCH::handleCreateSheet );
    registerHandler<GetItems, GetItemsResponse>( &API_HANDLER_SCH::handleGetItems );
    registerHandler<GetItemsById, GetItemsResponse>( &API_HANDLER_SCH::handleGetItemsById );
    registerHandler<GetOpenDocuments, GetOpenDocumentsResponse>(
            &API_HANDLER_SCH::handleGetOpenDocuments );
}


std::unique_ptr<COMMIT> API_HANDLER_SCH::createCommit()
{
    return std::make_unique<SCHEMATIC_API_COMMIT>( static_cast<SCH_EDIT_FRAME*>( m_frame ) );
}


std::vector<DocumentSpecifier> API_HANDLER_SCH::documentsAffectedBy( const DocumentSpecifier& document ) const
{
    std::vector<DocumentSpecifier> result;
    auto selected = resolveDocument( document );
    if( selected )
        for( const auto& path : static_cast<SCH_EDIT_FRAME*>( m_frame )->Schematic().Hierarchy() )
            if( path.LastScreen() == selected->LastScreen() ) result.push_back( describeDocument( path ) );
    return result;
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
        error.set_status( aCtx.Request.header().document().type() == DocumentType::DOCTYPE_SCHEMATIC
                                  ? ApiStatusCode::AS_BAD_REQUEST : ApiStatusCode::AS_UNHANDLED );
        error.set_error_message( "The requested schematic page is not open" );
        return tl::unexpected( error );
    }

    if( auto busy = checkForBusy() )
        return tl::unexpected( *busy );

    const auto& header = aCtx.Request.header();
    std::set<int> requested( aCtx.Request.types().begin(), aCtx.Request.types().end() );

    auto supported = SupportedSchematicApiTypes();
    if( requested.empty() ) requested = supported;
    if( header.field_mask().paths_size() != 0
        || std::any_of( requested.begin(), requested.end(),
                        [&]( int type ) { return !supported.count( type ); } ) )
        return documentError( "Unsupported schematic read type or field mask" );

    GetItemsResponse response;
    *response.mutable_header() = header;
    *response.mutable_header()->mutable_document() = describeDocument( *path );
    response.set_status( types::IRS_OK );
    bool foundContainer = header.container().value().empty();

    for( SCH_ITEM* item : path->LastScreen()->Items() )
    {
        if( item->Type() != SCH_SHEET_T )
        {
            if( header.container().value().empty()
                && requested.count( ToProtoEnum<KICAD_T, types::KiCadObjectType>( item->Type() ) ) )
                if( auto packed = PackSchematicItem( *item, &*path ) ) *response.add_items() = *packed;
            continue;
        }

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
    for( int type : SupportedSchematicApiTypes() )
        all.Request.add_types( static_cast<types::KiCadObjectType>( type ) );
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
        if( ids.count( SchematicApiItemId( item ) ) )
            *response.add_items() = item;
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


HANDLER_RESULT<kiapi::schematic::types::Sheet> API_HANDLER_SCH::createSheet(
        const types::ItemHeader& aHeader, const kiapi::schematic::types::Sheet& aInput,
        const std::string& aClientName, std::optional<bool> aLinkExisting, bool aDeferPush )
{
    auto fail = []( const std::string& message )
            -> HANDLER_RESULT<kiapi::schematic::types::Sheet>
    {
        ApiResponseStatus error;
        error.set_status( ApiStatusCode::AS_BAD_REQUEST );
        error.set_error_message( message );
        return tl::unexpected( error );
    };
    auto path = resolveDocument( aHeader.document() );
    if( !path )
        return fail( "The requested schematic page is not open" );
    if( !aHeader.container().value().empty() || aHeader.field_mask().paths_size() )
        return fail( "CreateSheet requires a page container and does not accept field masks" );

    auto* frame = static_cast<SCH_EDIT_FRAME*>( m_frame );
    SCHEMATIC& schematic = frame->Schematic();
    std::string error;
    auto sheet = UnpackSchematicSheet( aInput, path->Last(), error );
    if( !sheet )
        return fail( error );

    if( schematic.ResolveItem( sheet->m_Uuid, nullptr, true ) )
        return fail( "The sheet UUID already exists in this schematic" );
    for( const SCH_SHEET_PIN* pin : sheet->GetPins() )
    {
        if( schematic.ResolveItem( pin->m_Uuid, nullptr, true ) )
            return fail( "A sheet pin UUID already exists in this schematic" );
    }
    for( SCH_ITEM* item : path->LastScreen()->Items().OfType( SCH_SHEET_T ) )
    {
        if( static_cast<SCH_SHEET*>( item )->GetName() == sheet->GetName() )
            return fail( "A sheet with this name already exists on the parent page" );
    }

    wxFileName parentFile( path->LastScreen()->GetFileName() );
    wxFileName target( sheet->GetFileName() );
    if( target.GetExt().CmpNoCase( wxS( "kicad_sch" ) ) != 0 || target.GetName().IsEmpty() )
        return fail( "The child filename must end in .kicad_sch" );
    if( !parentFile.IsAbsolute() || !target.MakeAbsolute( parentFile.GetPath() )
        || !target.Normalize( wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE ) )
        return fail( "The parent schematic must have an absolute file location" );

    for( size_t ancestorIndex = 0; ancestorIndex < path->size(); ++ancestorIndex )
    {
        SCH_SHEET* ancestor = path->at( ancestorIndex );
        if( wxFileName( ancestor->GetScreen()->GetFileName() ) == target )
            return fail( "Attaching an ancestor would create a recursive sheet hierarchy" );
    }

    SCH_SCREEN* shared = nullptr;
    schematic.Root().SearchHierarchy( target.GetFullPath(), &shared );
    const bool exists = target.FileExists() || shared;
    if( aLinkExisting && *aLinkExisting != exists )
        return fail( *aLinkExisting ? "The child schematic does not exist" : "The child schematic already exists" );
    const bool link = aLinkExisting.value_or( exists );
    SCH_IO_KICAD_SEXPR io;
    try
    {
        if( shared )
        {
            sheet->SetScreen( shared );
        }
        else if( link )
        {
            // Use the non-interactive loader: an IPC error must never open a modal dialog.
            auto loaded = std::make_unique<SCH_SHEET>( &schematic );
            loaded->SetFileName( target.GetFullPath() );
            const_cast<KIID&>( loaded->m_Uuid ) = sheet->m_Uuid;
            io.LoadSchematicFile( target.GetFullPath(), &schematic, loaded.get() );
            if( !io.GetError().IsEmpty() || !loaded->GetScreen() )
                return fail( "The entire child hierarchy could not be loaded: " + io.GetError().ToStdString() );
            sheet->SetScreen( loaded->GetScreen() );
        }
        else
        {
            if( !target.DirExists() )
                return fail( "The child schematic directory does not exist" );
            frame->InitSheet( sheet.get(), target.GetFullPath() );
        }

        SCH_SHEET_LIST children( sheet.get() );
        // Also catch a loaded descendant referencing any parent ancestor.
        for( const SCH_SHEET_PATH& child : children )
        {
            for( size_t ancestorIndex = 0; ancestorIndex < path->size(); ++ancestorIndex )
            {
                SCH_SHEET* ancestor = path->at( ancestorIndex );
                if( wxFileName( child.LastScreen()->GetFileName() )
                    == wxFileName( ancestor->GetScreen()->GetFileName() ) )
                    return fail( "The child hierarchy contains an ancestor of the target page" );
            }
        }

        children.AddNewSymbolInstances( *path, schematic.Project().GetProjectName() );
        // Assign only full hierarchy paths. AddNewSheetInstances also creates
        // local, unprefixed instances, whose blank page numbers are not valid
        // on disk for a new API-created sheet.
        std::set<wxString> usedPages;
        for( const SCH_SHEET_PATH& existing : schematic.Hierarchy() )
            usedPages.insert( existing.GetPageNumber() );
        unsigned nextPage = 1;
        for( const SCH_SHEET_PATH& child : children )
        {
            SCH_SHEET_PATH fullPath( *path );
            fullPath = fullPath + child;
            wxString page = fullPath.GetPageNumber();
            if( page.IsEmpty() || usedPages.count( page ) )
            {
                do
                    page = wxString::Format( wxS( "%u" ), nextPage++ );
                while( usedPages.count( page ) );
                fullPath.SetPageNumber( page );
            }
            usedPages.insert( page );
        }
        wxFileName relative( target );
        relative.MakeRelativeTo( parentFile.GetPath() );
        sheet->SetFileName( relative.GetFullPath( wxPATH_UNIX ) );

        if( !link )
        {
            // Reserve without overwriting a file created since the existence check.
            wxFile reservation;
            if( !reservation.Create( target.GetFullPath(), false ) )
                return fail( "Could not create the child file without overwriting an existing file" );
            reservation.Close();
            try
            {
                io.SaveSchematicFile( target.GetFullPath(), sheet.get(), &schematic );
            }
            catch( ... )
            {
                wxRemoveFile( target.GetFullPath() );
                throw;
            }
        }
    }
    catch( const IO_ERROR& e )
    {
        return fail( e.What().ToStdString() );
    }

    auto result = PackSchematicSheet( *sheet );
    auto* commit = static_cast<SCHEMATIC_API_COMMIT*>( getCurrentCommit( aClientName ) );
    // Attach now so subsequent requests in BeginCommit can see their own additions.
    SCH_SHEET* created = sheet.release();
    if( path->LastScreen() == frame->GetScreen() )
        frame->AddToScreen( created );
    else
        path->LastScreen()->Append( created );
    commit->Added( created, path->LastScreen() );
    commit->Track( created );
    schematic.RefreshHierarchy();
    if( !aDeferPush && !m_activeClients.count( aClientName ) )
        pushCurrentCommit( aClientName, _( "Created sheet via API" ) );
    return result;
}


HANDLER_RESULT<kiapi::schematic::commands::CreateSheetResponse> API_HANDLER_SCH::handleCreateSheet(
        const HANDLER_CONTEXT<kiapi::schematic::commands::CreateSheet>& aCtx )
{
    if( !resolveDocument( aCtx.Request.header().document() ) )
    {
        ApiResponseStatus error;
        error.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( error );
    }
    if( auto busy = checkForWrite( aCtx.ClientName ) )
        return tl::unexpected( *busy );
    auto result = createSheet( aCtx.Request.header(), aCtx.Request.sheet(), aCtx.ClientName,
                              aCtx.Request.link_existing() );
    if( !result )
        return tl::unexpected( result.error() );
    kiapi::schematic::commands::CreateSheetResponse response;
    *response.mutable_header() = aCtx.Request.header();
    *response.mutable_sheet() = *result;
    return response;
}


HANDLER_RESULT<ItemRequestStatus> API_HANDLER_SCH::handleCreateUpdateItemsInternal( bool aCreate,
        const std::string& aClientName, const types::ItemHeader& aHeader,
        const google::protobuf::RepeatedPtrField<google::protobuf::Any>& aItems,
        std::function<void( ItemStatus, google::protobuf::Any )> aItemHandler )
{
    auto path = resolveDocument( aHeader.document() );
    if( !path || aHeader.document().project().path().empty() || !aHeader.document().has_sheet_path() )
        return documentError( "Item edits require an explicit project and sheet UUID path" );
    if( !aHeader.container().value().empty() )
        return documentError( "These schematic items must be created on the selected sheet" );
    auto* frame = static_cast<SCH_EDIT_FRAME*>( m_frame );
    SCH_SCREEN* screen = path->LastScreen();
    auto* commit = static_cast<SCHEMATIC_API_COMMIT*>( getCurrentCommit( aClientName ) );
    for( const auto& input : aItems )
    {
        ItemStatus status;
        google::protobuf::Any output;
        kiapi::schematic::types::Sheet sheet;
        if( aCreate && input.UnpackTo( &sheet ) )
        {
            auto result = createSheet( aHeader, sheet, aClientName, std::nullopt, true );
            if( result ) { status.set_code( ISC_OK ); output.PackFrom( *result ); }
            else { status.set_code( ISC_INVALID_DATA ); status.set_error_message( result.error().error_message() ); }
            aItemHandler( status, output );
            commit = static_cast<SCHEMATIC_API_COMMIT*>( getCurrentCommit( aClientName ) );
            continue;
        }
        auto fail = [&]( ItemStatusCode code, const std::string& message )
                    { status.set_code( code ); status.set_error_message( message ); aItemHandler( status, input ); };
        const std::string id = SchematicApiItemId( input );
        SCH_ITEM* existing = nullptr;
        for( SCH_ITEM* candidate : screen->Items() )
            if( candidate->m_Uuid.AsStdString() == id ) { existing = candidate; break; }
        if( aCreate && !id.empty() )
        {
            bool collision = false;
            for( const auto& page : frame->Schematic().Hierarchy() )
            {
                collision |= page.Last()->m_Uuid.AsStdString() == id
                             || page.LastScreen()->GetUuid().AsStdString() == id;
                for( SCH_ITEM* candidate : page.LastScreen()->Items() )
                {
                    collision |= candidate->m_Uuid.AsStdString() == id;
                    candidate->RunOnChildren( [&]( SCH_ITEM* child )
                            { collision |= child->m_Uuid.AsStdString() == id; }, RECURSE_MODE::RECURSE );
                }
            }
            if( collision ) { fail( ISC_EXISTING, "UUID is already in use in this schematic" ); continue; }
        }
        if( !aCreate && !existing ) { fail( ISC_NONEXISTENT, "UUID is not on the selected sheet" ); continue; }
        std::string error;
        google::protobuf::Any full = input;
        if( !aCreate )
        {
            auto original = PackSchematicItem( *existing, &*path );
            if( !original || !MergeSchematicApiItem( input, *original, aHeader.field_mask(), full, error ) )
            { fail( ISC_INVALID_DATA, error.empty() ? "Unsupported update type" : error ); continue; }
        }
        else if( aHeader.field_mask().paths_size() )
        { fail( ISC_INVALID_DATA, "CreateItems does not accept a field mask" ); continue; }
        std::unique_ptr<SCH_ITEM> item;
        kiapi::schematic::types::Symbol symbolInput;
        if( full.UnpackTo( &symbolInput ) )
        {
            auto* previous = dynamic_cast<SCH_SYMBOL*>( existing );
            if( existing && !previous ) { fail( ISC_INVALID_DATA, "Cannot change item type" ); continue; }
            for( const auto& field : symbolInput.fields() )
                if( field.name() == "Reference" && ( symbolInput.reference().empty()
                    || ( previous && symbolInput.reference() == previous->GetRef( &*path, false ).ToStdString()
                         && field.text().text() != symbolInput.reference() ) ) )
                    symbolInput.set_reference( field.text().text() );
            const LIB_SYMBOL* definition = nullptr;
            LIB_ID libId = LibIdFromProto( symbolInput.lib_id() );
            if( libId.GetLibNickname().empty() || libId.GetLibItemName().empty() )
            { fail( ISC_INVALID_DATA, "A complete library identifier is required" ); continue; }
            if( previous && previous->GetLibId() == libId && !previous->IsMissingLibSymbol() )
                definition = previous->GetLibSymbolRef().get();
            else
            {
                try
                {
                    auto* adapter = PROJECT_SCH::SymbolLibAdapter( &frame->Prj() );
                    auto status = adapter->LoadLibraryEntry( libId.GetLibNickname() );
                    if( status && status->load_status == LOAD_STATUS::LOADED ) definition = adapter->LoadSymbol( libId );
                    else if( status && status->error ) error = status->error->message.ToStdString();
                    else error = "Symbol library is not registered or could not be loaded";
                }
                catch( const IO_ERROR& e ) { error = e.What().ToStdString(); }
            }
            if( !definition ) { fail( ISC_INVALID_DATA, error.empty() ? "Symbol library or entry does not exist" : error ); continue; }
            auto symbol = UnpackSchematicSymbol( symbolInput, *definition, *path, previous, error );
            if( symbol )
            {
                // Reserve references for all instances before touching the live screen.
                std::map<std::string, std::pair<wxString, int>> explicitRefs;
                for( const auto& instance : symbolInput.instances() )
                {
                    std::string key;
                    for( const auto& uuid : instance.sheet_path().path() ) key += "/" + uuid.value();
                    if( !explicitRefs.emplace( key, std::make_pair( wxString::FromUTF8( instance.reference() ), instance.unit() ) ).second )
                    { error = "Duplicate symbol instance path"; break; }
                }
                std::set<wxString> reserved;
                for( const auto& page : frame->Schematic().Hierarchy() )
                {
                    if( page.LastScreen() != screen ) continue;
                    std::string key;
                    for( const KIID& uuid : page.Path() ) key += "/" + uuid.AsStdString();
                    wxString ref;
                    int unit = symbol->GetUnit();
                    auto explicitIt = explicitRefs.find( key );
                    if( explicitIt != explicitRefs.end() )
                    {
                        ref = explicitIt->second.first;
                        unit = explicitIt->second.second;
                        explicitRefs.erase( explicitIt );
                    }
                    else if( previous ) { ref = previous->GetRef( &page, false ); unit = previous->GetUnitSelection( &page ); }
                    if( page.Path() == path->Path() )
                    { ref = wxString::FromUTF8( symbolInput.reference() ); unit = symbol->GetUnit(); }
                    wxString prefix = UTIL::GetRefDesPrefix( definition->GetReferenceField().GetText() );
                    if( prefix.empty() ) prefix = "U";
                    auto occupied = [&]( const wxString& candidate, bool allowOtherUnits )
                    {
                        for( const wxString& ref : reserved ) if( ref.CmpNoCase( candidate ) == 0 ) return true;
                        for( const auto& otherPage : frame->Schematic().Hierarchy() )
                            for( SCH_ITEM* other : otherPage.LastScreen()->Items() )
                                if( auto* otherSymbol = dynamic_cast<SCH_SYMBOL*>( other ); otherSymbol && otherSymbol != existing
                                    && otherSymbol->GetRef( &otherPage, false ).CmpNoCase( candidate ) == 0 )
                                {
                                    if( !allowOtherUnits || otherSymbol->GetLibId() != libId
                                        || otherSymbol->GetUnitSelection( &otherPage ) == unit ) return true;
                                }
                        return false;
                    };
                    if( ref.empty() || ref.Contains( "?" ) )
                    {
                        int n = 1;
                        do { ref = prefix + wxString::Format( "%d", n++ ); } while( occupied( ref, false ) );
                    }
                    if( unit < 1 || unit > definition->GetUnitCount() || UTIL::GetRefDesNumber( ref ) < 0
                        || UTIL::GetRefDesPrefix( ref ).empty()
                        || !GetFieldValidationErrorMessage( FIELD_T::REFERENCE, ref ).empty() || occupied( ref, true ) )
                    { error = "Invalid unit or conflicting reference in schematic hierarchy"; break; }
                    symbol->SetRef( &page, ref ); symbol->SetUnitSelection( &page, unit );
                    reserved.insert( ref );
                }
                if( !explicitRefs.empty() && error.empty() ) error = "Symbol instance path does not refer to this shared screen";
                if( error.empty() )
                {
                    symbol->GetField( FIELD_T::REFERENCE )->SetText( symbol->GetRef( &*path, false ) );
                    item = std::move( symbol );
                }
            }
        }
        else item = UnpackSchematicItem( full, error, existing, &*path );
        if( !item ) { fail( ISC_INVALID_DATA, error ); continue; }
        if( aCreate )
        {
            SCH_ITEM* added = item.release();
            screen->Append( added );
            if( screen == frame->GetScreen() ) frame->GetCanvas()->GetView()->Add( added );
            commit->Added( added, screen );
            commit->Track( added );
            std::vector<SCH_ITEM*> changed{ added };
            frame->Schematic().OnItemsAdded( changed );
            added->SetConnectivityDirty();
            if( auto packed = PackSchematicItem( *added, &*path ) ) output = *packed;
        }
        else
        {
            commit->Modify( existing, screen );
            existing->SwapItemData( item.get() );
            existing->SetConnectivityDirty();
            screen->Update( existing );
            std::vector<SCH_ITEM*> changed{ existing };
            frame->Schematic().OnItemsChanged( changed );
            if( screen == frame->GetScreen() ) frame->UpdateItem( existing, true, true );
            if( auto packed = PackSchematicItem( *existing, &*path ) ) output = *packed;
        }
        status.set_code( ISC_OK );
        aItemHandler( status, output );
    }
    if( !m_activeClients.count( aClientName ) )
        pushCurrentCommit( aClientName, aCreate ? _( "Created items via API" ) : _( "Updated items via API" ) );
    frame->GetCanvas()->Refresh();
    return ItemRequestStatus::IRS_OK;
}

void API_HANDLER_SCH::deleteItemsInternal( const DocumentSpecifier& document,
        std::map<KIID, ItemDeletionStatus>& items, const std::string& client )
{
    auto path = resolveDocument( document );
    if( !path ) return;
    auto* frame = static_cast<SCH_EDIT_FRAME*>( m_frame );
    auto* commit = static_cast<SCHEMATIC_API_COMMIT*>( getCurrentCommit( client ) );
    for( auto& [id, status] : items )
    {
        SCH_ITEM* found = nullptr;
        for( SCH_ITEM* item : path->LastScreen()->Items() )
            if( item->m_Uuid == id ) { found = item; break; }
        if( !found ) continue;
        auto packed = PackSchematicItem( *found, &*path );
        if( !packed || found->Type() == SCH_SHEET_T ) { status = IDS_IMMUTABLE; continue; }
        frame->RemoveFromScreen( found, path->LastScreen() );
        commit->Removed( found, path->LastScreen() );
        std::vector<SCH_ITEM*> removed{ found };
        frame->Schematic().OnItemsRemoved( removed );
        status = IDS_OK;
    }
    if( !m_activeClients.count( client ) ) pushCurrentCommit( client, _( "Deleted items via API" ) );
    frame->GetCanvas()->Refresh();
}

std::optional<EDA_ITEM*> API_HANDLER_SCH::getItemFromDocument( const DocumentSpecifier& document,
                                                               const KIID& id )
{
    auto path = resolveDocument( document );
    if( path )
        for( SCH_ITEM* item : path->LastScreen()->Items() )
            if( item->m_Uuid == id ) return item;
    return std::nullopt;
}

namespace
{
using namespace kiapi::schematic::commands;

ApiResponseStatus pageSettingsError( const char* aMessage )
{
    ApiResponseStatus error;
    error.set_status( ApiStatusCode::AS_BAD_REQUEST );
    error.set_error_message( aMessage );
    return error;
}

ApiResponseStatus pageSettingsUnhandled()
{
    ApiResponseStatus error;
    error.set_status( ApiStatusCode::AS_UNHANDLED );
    return error;
}

bool isExplicitSchematicPage( const DocumentSpecifier& aDocument )
{
    return aDocument.type() == DocumentType::DOCTYPE_SCHEMATIC
           && !aDocument.project().path().empty() && aDocument.has_sheet_path()
           && aDocument.sheet_path().path_size() > 0;
}

PageSettings packPageSettings( const DocumentSpecifier& aDocument, const PAGE_INFO& aPage )
{
    PageSettings result;
    *result.mutable_document() = aDocument;
    result.set_paper_format( aPage.GetTypeAsString().ToStdString() );
    result.set_orientation( aPage.IsPortrait() ? PO_PORTRAIT : PO_LANDSCAPE );
    result.set_width_nm( std::llround( aPage.GetWidthMM() * 1000000 ) );
    result.set_height_nm( std::llround( aPage.GetHeightMM() * 1000000 ) );
    return result;
}
}


void API_HANDLER_SCH::registerPageSettingsHandlers()
{
    using namespace kiapi::schematic::commands;
    registerHandler<GetPageSettings, GetPageSettingsResponse>( &API_HANDLER_SCH::handleGetPageSettings );
    registerHandler<SetPageSettings, SetPageSettingsResponse>( &API_HANDLER_SCH::handleSetPageSettings );
}


HANDLER_RESULT<kiapi::schematic::commands::GetPageSettingsResponse> API_HANDLER_SCH::handleGetPageSettings(
        const HANDLER_CONTEXT<kiapi::schematic::commands::GetPageSettings>& aCtx )
{
    const auto& document = aCtx.Request.document();
    auto* frame = static_cast<SCH_EDIT_FRAME*>( m_frame );
    if( !isExplicitSchematicPage( document ) )
        return tl::unexpected( pageSettingsError( "An explicit schematic project and sheet UUID path are required" ) );
    if( document.project().path() != frame->Schematic().Project().GetProjectPath().ToStdString() )
        return tl::unexpected( pageSettingsUnhandled() );
    if( auto busy = checkForBusy() )
        return tl::unexpected( *busy );
    auto path = resolveDocument( document );
    if( !path || !path->LastScreen() )
        return tl::unexpected( pageSettingsError( "The selected schematic sheet is not open" ) );

    kiapi::schematic::commands::GetPageSettingsResponse response;
    *response.mutable_settings() = packPageSettings( describeDocument( *path ),
                                                     path->LastScreen()->GetPageSettings() );
    return response;
}


HANDLER_RESULT<kiapi::schematic::commands::SetPageSettingsResponse> API_HANDLER_SCH::handleSetPageSettings(
        const HANDLER_CONTEXT<kiapi::schematic::commands::SetPageSettings>& aCtx )
{
    using namespace kiapi::schematic::commands;
    const auto& request = aCtx.Request;
    if( request.documents().empty() || !isExplicitSchematicPage( request.documents( 0 ) ) )
        return tl::unexpected( pageSettingsError( "Specify a nonempty list of explicit schematic documents" ) );

    auto* frame = static_cast<SCH_EDIT_FRAME*>( m_frame );
    if( request.documents( 0 ).project().path() != frame->Schematic().Project().GetProjectPath().ToStdString() )
        return tl::unexpected( pageSettingsUnhandled() );
    if( auto busy = checkForBusy() )
        return tl::unexpected( *busy );
    if( !m_commits.empty() )
        return tl::unexpected( pageSettingsError( "Finish pending API transactions first; page changes are one independent Undo step" ) );

    PAGE_INFO page;
    if( ( request.orientation() != PO_PORTRAIT && request.orientation() != PO_LANDSCAPE )
        || !page.SetType( wxString::FromUTF8( request.paper_format() ), request.orientation() == PO_PORTRAIT )
        || page.IsCustom() || page.GetType() == PAGE_SIZE_TYPE::GERBER )
        return tl::unexpected( pageSettingsError( "Specify a standard schematic paper format and portrait or landscape orientation" ) );

    std::set<wxString> selected;
    std::vector<SCH_SHEET_PATH> paths;
    std::set<SCH_SCREEN*> changed;
    for( const auto& document : request.documents() )
    {
        if( !isExplicitSchematicPage( document ) )
            return tl::unexpected( pageSettingsError( "Every target must specify a project and sheet UUID path" ) );
        auto path = resolveDocument( document );
        if( !path || !path->LastScreen() )
            return tl::unexpected( pageSettingsError( "All target sheets must be open in the same schematic hierarchy" ) );
        if( !selected.insert( path->Path().AsString() ).second )
            continue;

        paths.push_back( *path );
        SCH_SCREEN* screen = path->LastScreen();
        const PAGE_INFO& old = screen->GetPageSettings();
        if( old.GetType() == page.GetType() && old.IsPortrait() == page.IsPortrait()
            && old.GetSizeMils() == page.GetSizeMils() )
            continue;
        if( screen->IsReadOnly() )
            return tl::unexpected( pageSettingsError( "A target schematic sheet is read-only" ) );
        changed.insert( screen );
    }

    for( const auto& path : frame->Schematic().Hierarchy() )
    {
        if( changed.count( path.LastScreen() ) && !selected.count( path.Path().AsString() ) )
            return tl::unexpected( pageSettingsError( "A changed file is shared: include every loaded instance of that sheet" ) );
    }

    PICKED_ITEMS_LIST undo;
    undo.SetDescription( _( "Changed paper format via API" ) );
    std::vector<std::unique_ptr<SCH_PAGE_FORMAT_UNDO_ITEM>> images;
    for( SCH_SCREEN* screen : changed )
    {
        images.push_back( std::make_unique<SCH_PAGE_FORMAT_UNDO_ITEM>( screen->GetPageSettings() ) );
        ITEM_PICKER picker( screen, images.back().get(), UNDO_REDO::PAGESETTINGS );
        picker.SetFlags( UR_TRANSIENT );
        undo.PushItem( picker );
    }
    if( !changed.empty() )
    {
        frame->SaveCopyInUndoList( undo, UNDO_REDO::PAGESETTINGS, false );
        for( auto& image : images ) image.release(); // The undo list owns the images.
        for( SCH_SCREEN* screen : changed ) screen->SetPageSettings( page );
        NotifySchematicPageFormatChanged( *frame, changed );
    }

    SetPageSettingsResponse response;
    response.set_changed_screens( changed.size() );
    for( const auto& path : paths )
        *response.add_pages() = packPageSettings( describeDocument( path ), path.LastScreen()->GetPageSettings() );
    return response;
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::saveDocumentInternal( const DocumentSpecifier& )
{
    auto* frame = static_cast<SCH_EDIT_FRAME*>( m_frame );
    auto hierarchy = frame->Schematic().Hierarchy();
    std::set<SCH_SCREEN*> screens;
    for( const auto& path : hierarchy )
    {
        SCH_SCREEN* screen = path.LastScreen();
        if( !screens.insert( screen ).second )
            continue;
        wxFileName file( screen->GetFileName() );
        if( !file.IsAbsolute() || file.GetExt() != "kicad_sch" || !file.IsDirWritable()
            || ( file.FileExists() && !file.IsFileWritable() ) )
            return documentError( "Every sheet needs an absolute writable native save path" );
    }
    screens.clear();
    SCH_IO_KICAD_SEXPR io;
    // Save the entire live hierarchy, including off-screen unsaved changes, once per screen.
    for( const auto& path : hierarchy )
    {
        SCH_SCREEN* screen = path.LastScreen();
        if( !screens.insert( screen ).second )
            continue;
        io.SaveSchematicFile( screen->GetFileName(), path.Last(), &frame->Schematic() );
        screen->SetContentModified( false );
    }
    frame->updateTitle();
    return Empty();
}

HANDLER_RESULT<Empty> API_HANDLER_SCH::saveCopyInternal(
        const DocumentSpecifier& aDocument, const wxString& aPath, bool aIncludeProject )
{
    if( aIncludeProject )
        return documentError( "Schematic SaveCopy exports one sheet; include_project is not supported" );
    auto* frame = static_cast<SCH_EDIT_FRAME*>( m_frame );
    auto path = resolveDocument( aDocument );
    for( const auto& instance : frame->Schematic().Hierarchy() )
    {
        if( wxFileName( instance.LastScreen()->GetFileName() ) == wxFileName( aPath ) )
            return documentError( "SaveCopy cannot overwrite an open sheet; use SaveDocument" );
    }
    SCH_IO_KICAD_SEXPR io;
    io.SaveSchematicFile( aPath, path->Last(), &frame->Schematic() );
    return Empty();
}

HANDLER_RESULT<Empty> API_HANDLER_SCH::revertDocumentInternal( const DocumentSpecifier& )
{
    auto* frame = static_cast<SCH_EDIT_FRAME*>( m_frame );
    const KIID_PATH active = frame->GetCurrentSheet().Path();
    const wxString filename = frame->Schematic().GetFileName();
    if( !wxFileName::IsFileReadable( filename ) )
        return documentError( "Schematic file cannot be read" );
    // Validate the hierarchy first. In particular, a missing/broken child must not clear dirty state.
    SCHEMATIC validation( &frame->Prj() );
    SCH_IO_KICAD_SEXPR io;
    std::unique_ptr<SCH_SHEET> parsed( io.LoadSchematicFile( filename, &validation ) );
    if( !parsed || !io.GetError().empty() )
        return documentError( "Cannot reload schematic hierarchy: " + io.GetError().ToStdString() );

    SCH_SHEET_PATH root = frame->Schematic().Hierarchy().at( 0 );
    frame->GetToolManager()->RunAction<SCH_SHEET_PATH*>( SCH_ACTIONS::changeSheet, &root );
    SCH_SCREENS screens( frame->Schematic().Root() );
    for( auto* screen = screens.GetFirst(); screen; screen = screens.GetNext() )
        screen->SetContentModified( false );
    frame->ReleaseFile();
    if( !frame->OpenProjectFiles( { filename }, KICTL_REVERT ) )
        return documentError( "Could not reload schematic" );
    if( auto restored = frame->Schematic().Hierarchy().GetSheetPathByKIIDPath( active ) )
        frame->GetToolManager()->RunAction<SCH_SHEET_PATH*>( SCH_ACTIONS::changeSheet, &*restored );
    m_netRenamePlans.clear();
    return Empty();
}
