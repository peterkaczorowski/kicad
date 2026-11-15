/* Copyright The KiCad Developers, see AUTHORS.txt.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "api_handler_drawing_sheet.h"
#include "pl_editor_frame.h"
#include <drawing_sheet/ds_data_model.h>
#include <project.h>
#include <tool/tool_manager.h>
#include <tool/actions.h>
#include <wx/filename.h>

using namespace kiapi::common;
using namespace kiapi::common::commands;

API_HANDLER_DRAWING_SHEET::API_HANDLER_DRAWING_SHEET( PL_EDITOR_FRAME* aFrame ) :
        API_HANDLER_EDITOR( aFrame )
{
    registerHandler<GetOpenDocuments, GetOpenDocumentsResponse>(
            &API_HANDLER_DRAWING_SHEET::handleGetOpenDocuments );
}

PL_EDITOR_FRAME* API_HANDLER_DRAWING_SHEET::frame() const
{
    return static_cast<PL_EDITOR_FRAME*>( m_frame );
}

HANDLER_RESULT<GetOpenDocumentsResponse> API_HANDLER_DRAWING_SHEET::handleGetOpenDocuments(
        const HANDLER_CONTEXT<GetOpenDocuments>& aCtx )
{
    if( aCtx.Request.type() != types::DOCTYPE_DRAWING_SHEET )
        return documentError( "", ApiStatusCode::AS_UNHANDLED );
    GetOpenDocumentsResponse response;
    {
        auto* doc = response.add_documents();
        doc->set_type( types::DOCTYPE_DRAWING_SHEET );
        doc->set_board_filename( wxFileName( frame()->GetCurrentFileName() ).GetFullName().ToStdString() );
        if( !frame()->Prj().IsNullProject() )
        {
            doc->mutable_project()->set_name( frame()->Prj().GetProjectName().ToStdString() );
            doc->mutable_project()->set_path( frame()->Prj().GetProjectDirectory().ToStdString() );
        }
    }
    return response;
}

bool API_HANDLER_DRAWING_SHEET::validateDocumentInternal( const DocumentSpecifier& aDocument ) const
{
    return aDocument.type() == types::DOCTYPE_DRAWING_SHEET
           && aDocument.board_filename() == wxFileName( frame()->GetCurrentFileName() ).GetFullName().ToStdString()
           && ( aDocument.project().path().empty()
                || wxFileName::DirName( wxString::FromUTF8( aDocument.project().path() ) )
                           == wxFileName::DirName( frame()->Prj().GetProjectDirectory() ) )
           && ( aDocument.project().name().empty()
                || aDocument.project().name() == frame()->Prj().GetProjectName().ToStdString() );
}

HANDLER_RESULT<Empty> API_HANDLER_DRAWING_SHEET::saveDocumentInternal( const DocumentSpecifier& )
{
    wxFileName path( frame()->GetCurrentFileName() );
    if( !path.IsAbsolute() || !path.IsDirWritable()
        || ( path.FileExists() && !path.IsFileWritable() ) )
        return documentError( "Drawing sheet needs an absolute writable save path" );
    if( !frame()->SaveDrawingSheetFile( path.GetFullPath() ) )
        return documentError( "Could not save drawing sheet" );
    return Empty();
}

HANDLER_RESULT<Empty> API_HANDLER_DRAWING_SHEET::saveCopyInternal(
        const DocumentSpecifier&, const wxString& aPath, bool aIncludeProject )
{
    if( aIncludeProject )
        return documentError( "A drawing sheet copy does not include a project" );
    if( wxFileName( aPath ) == wxFileName( frame()->GetCurrentFileName() ) )
        return documentError( "SaveCopy cannot overwrite the open drawing sheet; use SaveDocument" );
    DS_DATA_MODEL::GetTheInstance().Save( aPath );
    return Empty();
}

HANDLER_RESULT<Empty> API_HANDLER_DRAWING_SHEET::revertDocumentInternal( const DocumentSpecifier& )
{
    const wxString filename = frame()->GetCurrentFileName();
    if( !wxFileName::IsFileReadable( filename ) )
        return documentError( "Drawing sheet file cannot be read" );
    wxString error;
    DS_DATA_MODEL validation;
    if( !validation.LoadDrawingSheet( filename, &error ) )
        return documentError( error.ToStdString() );
    // The model is parsed before selection and Undo entries pointing into the old one are cleared.
    frame()->GetToolManager()->RunAction( ACTIONS::selectionClear );
    frame()->ClearUndoRedoList();
    if( !DS_DATA_MODEL::GetTheInstance().LoadDrawingSheet( filename, &error ) )
        return documentError( error.ToStdString() );
    frame()->GetToolManager()->ResetTools( TOOL_BASE::MODEL_RELOAD );
    frame()->OnNewDrawingSheet();
    frame()->GetScreen()->SetContentModified( false );
    return Empty();
}
