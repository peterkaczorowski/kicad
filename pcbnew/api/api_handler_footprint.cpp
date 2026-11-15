/* Copyright The KiCad Developers, see AUTHORS.txt.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <api/api_handler_footprint.h>
#include <api/api_utils.h>
#include <footprint_edit_frame.h>
#include <footprint.h>
#include <board.h>
#include <footprint_library_adapter.h>
#include <project_pcb.h>
#include <project.h>
#include <pcb_io/kicad_sexpr/pcb_io_kicad_sexpr.h>
#include <wx/filename.h>

using namespace kiapi::common;
using namespace kiapi::common::commands;

API_HANDLER_FOOTPRINT::API_HANDLER_FOOTPRINT( FOOTPRINT_EDIT_FRAME* aFrame ) :
        API_HANDLER_EDITOR( aFrame )
{
    registerHandler<GetOpenDocuments, GetOpenDocumentsResponse>(
            &API_HANDLER_FOOTPRINT::handleGetOpenDocuments );
}

FOOTPRINT_EDIT_FRAME* API_HANDLER_FOOTPRINT::frame() const
{
    return static_cast<FOOTPRINT_EDIT_FRAME*>( m_frame );
}

HANDLER_RESULT<GetOpenDocumentsResponse> API_HANDLER_FOOTPRINT::handleGetOpenDocuments(
        const HANDLER_CONTEXT<GetOpenDocuments>& aCtx )
{
    if( aCtx.Request.type() != types::DOCTYPE_FOOTPRINT )
        return documentError( "", ApiStatusCode::AS_UNHANDLED );
    GetOpenDocumentsResponse response;
    if( auto* footprint = frame()->GetBoard()->GetFirstFootprint() )
    {
        auto* doc = response.add_documents();
        doc->set_type( types::DOCTYPE_FOOTPRINT );
        *doc->mutable_lib_id() = LibIdToProto( footprint->GetFPID() );
        if( !frame()->Prj().IsNullProject() )
        {
            doc->mutable_project()->set_name( frame()->Prj().GetProjectName().ToStdString() );
            doc->mutable_project()->set_path( frame()->Prj().GetProjectDirectory().ToStdString() );
        }
    }
    return response;
}

bool API_HANDLER_FOOTPRINT::validateDocumentInternal( const DocumentSpecifier& aDocument ) const
{
    auto* footprint = frame()->GetBoard()->GetFirstFootprint();
    return footprint && aDocument.type() == types::DOCTYPE_FOOTPRINT
           && aDocument.lib_id().SerializeAsString() == LibIdToProto( footprint->GetFPID() ).SerializeAsString()
           && ( aDocument.project().path().empty()
                || wxFileName::DirName( wxString::FromUTF8( aDocument.project().path() ) )
                           == wxFileName::DirName( frame()->Prj().GetProjectDirectory() ) )
           && ( aDocument.project().name().empty()
                || aDocument.project().name() == frame()->Prj().GetProjectName().ToStdString() );
}

HANDLER_RESULT<Empty> API_HANDLER_FOOTPRINT::saveDocumentInternal( const DocumentSpecifier& )
{
    auto* footprint = frame()->GetBoard()->GetFirstFootprint();
    const auto& id = footprint->GetFPID();
    if( frame()->IsCurrentFPFromBoard() || id.GetLibNickname().empty() )
        return documentError( "SaveDocument requires a library footprint; use SaveCopy for standalone footprints" );
    auto* adapter = PROJECT_PCB::FootprintLibAdapter( &frame()->Prj() );
    if( !adapter->IsFootprintLibWritable( id.GetLibNickname() ) )
        return documentError( "Footprint library is read-only" );
    if( adapter->SaveFootprint( id.GetLibNickname(), footprint, true ) != FOOTPRINT_LIBRARY_ADAPTER::SAVE_OK )
        return documentError( "Could not save footprint" );
    frame()->m_originalFootprintCopy = std::make_unique<FOOTPRINT>( *footprint );
    frame()->m_originalFootprintCopy->SetParent( nullptr );
    frame()->GetScreen()->SetContentModified( false );
    frame()->UpdateTitle();
    return Empty();
}

HANDLER_RESULT<Empty> API_HANDLER_FOOTPRINT::saveCopyInternal(
        const DocumentSpecifier&, const wxString& aPath, bool aIncludeProject )
{
    if( aIncludeProject )
        return documentError( "A footprint copy does not include a project" );
    const auto* footprint = frame()->GetBoard()->GetFirstFootprint();
    auto* adapter = PROJECT_PCB::FootprintLibAdapter( &frame()->Prj() );
    if( auto row = adapter->GetRow( footprint->GetFPID().GetLibNickname() ) )
    {
        wxFileName original( LIBRARY_MANAGER::GetFullURI( *row, true ),
                             footprint->GetFPID().GetLibItemName() + ".kicad_mod" );
        if( original == wxFileName( aPath ) )
            return documentError( "SaveCopy cannot overwrite the open footprint; use SaveDocument" );
    }
    // Native footprint writer names the output from the library item name.
    FOOTPRINT copy( *footprint );
    wxFileName destination( aPath );
    copy.SetFPID( LIB_ID( wxEmptyString, destination.GetName() ) );
    PCB_IO_KICAD_SEXPR io;
    io.FootprintSave( destination.GetPath(), &copy );
    return Empty();
}

HANDLER_RESULT<Empty> API_HANDLER_FOOTPRINT::revertDocumentInternal( const DocumentSpecifier& )
{
    const LIB_ID id = frame()->GetBoard()->GetFirstFootprint()->GetFPID();
    if( frame()->IsCurrentFPFromBoard() || id.GetLibNickname().empty() )
        return documentError( "RevertDocument requires a footprint backed by a library" );
    auto* adapter = PROJECT_PCB::FootprintLibAdapter( &frame()->Prj() );
    auto row = adapter->GetRow( id.GetLibNickname() );
    if( !row )
        return documentError( "Footprint library is not in the library tables" );
    IO_RELEASER<PCB_IO> io( PCB_IO_MGR::FindPlugin( PCB_IO_MGR::EnumFromStr( ( *row )->Type() ) ) );
    if( !io )
        return documentError( "Footprint library format is not supported" );
    auto options = ( *row )->GetOptionsMap();
    // A new plugin deliberately bypasses both the adapter's preloaded copies and timestamp cache.
    std::unique_ptr<FOOTPRINT> loaded( io->FootprintLoad( LIBRARY_MANAGER::GetFullURI( *row, true ),
                                                       id.GetLibItemName(), true, &options ) );
    if( !loaded )
        return documentError( "Footprint no longer exists in its library" );
    loaded->SetFPID( id );
    if( !frame()->Clear_Pcb( false ) )
        return documentError( "Could not clear old footprint model" );
    frame()->AddFootprintToBoard( loaded.release() );
    frame()->GetBoard()->GetFirstFootprint()->ClearFlags();
    frame()->GetScreen()->SetContentModified( false );
    frame()->UpdateView();
    frame()->UpdateTitle();
    return Empty();
}
