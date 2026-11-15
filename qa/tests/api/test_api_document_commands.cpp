/* Copyright The KiCad Developers, see AUTHORS.txt.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <boost/test/unit_test.hpp>
#include <api/api_handler_editor.h>
#include <drawing_sheet/ds_data_model.h>
#include <wx/ffile.h>
#include <wx/filename.h>

using namespace kiapi::common;
using namespace kiapi::common::commands;

namespace
{
// Exercises the actual common handlers used by all five frame implementations.
class DOCUMENT_HANDLER : public API_HANDLER_EDITOR
{
public:
    explicit DOCUMENT_HANDLER( types::DocumentType aType ) : type( aType ) {}
    using API_HANDLER_EDITOR::handleSaveDocument;
    using API_HANDLER_EDITOR::handleSaveCopyOfDocument;
    using API_HANDLER_EDITOR::handleRevertDocument;
    using API_HANDLER_EDITOR::handleRunAction;
    using API_HANDLER_EDITOR::handleBeginCommit;
    using API_HANDLER_EDITOR::handleEndCommit;
    types::DocumentType type;
    bool busy = false;
    bool dirty = false;
    int saved = 0;
    int reverted = 0;
protected:
    types::DocumentType thisDocumentType() const override { return type; }
    bool validateDocumentInternal( const DocumentSpecifier& doc ) const override
    { return doc.board_filename() == "open"; }
    bool documentModified() const override { return dirty; }
    std::optional<ApiResponseStatus> checkForBusy() override
    {
        if( busy )
        {
            ApiResponseStatus error;
            error.set_status( ApiStatusCode::AS_BUSY );
            return error;
        }
        return std::nullopt;
    }
    HANDLER_RESULT<Empty> saveDocumentInternal( const DocumentSpecifier& ) override
    { ++saved; return Empty(); }
    HANDLER_RESULT<Empty> revertDocumentInternal( const DocumentSpecifier& ) override
    { ++reverted; dirty = false; return Empty(); }
};

DocumentSpecifier document( types::DocumentType type )
{
    DocumentSpecifier doc;
    doc.set_type( type );
    doc.set_board_filename( "open" );
    return doc;
}
}

BOOST_AUTO_TEST_SUITE( ApiDocumentCommands )

BOOST_AUTO_TEST_CASE( DirtyRevertRequiresExplicitPermissionInEveryEditor )
{
    for( int type = 1; type <= 5; ++type )
    {
        DOCUMENT_HANDLER handler( static_cast<types::DocumentType>( type ) );
        HANDLER_CONTEXT<RevertDocument> ctx;
        *ctx.Request.mutable_document() = document( handler.type );
        handler.dirty = true;
        auto rejected = handler.handleRevertDocument( ctx );
        BOOST_REQUIRE( !rejected );
        BOOST_CHECK_EQUAL( rejected.error().status(), ApiStatusCode::AS_BAD_REQUEST );
        BOOST_CHECK( handler.dirty );
        BOOST_CHECK_EQUAL( handler.reverted, 0 );
        ctx.Request.set_discard_unsaved( true );
        BOOST_REQUIRE( handler.handleRevertDocument( ctx ) );
        BOOST_CHECK_EQUAL( handler.reverted, 1 );
        BOOST_CHECK( !handler.dirty );
    }
}

BOOST_AUTO_TEST_CASE( DocumentRoutingAndBusyPrecedeAllSideEffects )
{
    DOCUMENT_HANDLER handler( types::DOCTYPE_PCB );
    HANDLER_CONTEXT<SaveDocument> ctx;
    *ctx.Request.mutable_document() = document( types::DOCTYPE_SCHEMATIC );
    auto foreign = handler.handleSaveDocument( ctx );
    BOOST_REQUIRE( !foreign );
    BOOST_CHECK_EQUAL( foreign.error().status(), ApiStatusCode::AS_UNHANDLED );
    ctx.Request.mutable_document()->set_type( types::DOCTYPE_PCB );
    ctx.Request.mutable_document()->set_board_filename( "missing" );
    auto missing = handler.handleSaveDocument( ctx );
    BOOST_REQUIRE( !missing );
    BOOST_CHECK_EQUAL( missing.error().status(), ApiStatusCode::AS_BAD_REQUEST );
    *ctx.Request.mutable_document() = document( types::DOCTYPE_PCB );
    handler.busy = true;
    auto busy = handler.handleSaveDocument( ctx );
    BOOST_REQUIRE( !busy );
    BOOST_CHECK_EQUAL( busy.error().status(), ApiStatusCode::AS_BUSY );
    BOOST_CHECK_EQUAL( handler.saved, 0 );
}

BOOST_AUTO_TEST_CASE( OpenCommitBlocksAllFourCommandsIncludingOtherClients )
{
    for( int type = 1; type <= 5; ++type )
    {
        DOCUMENT_HANDLER handler( static_cast<types::DocumentType>( type ) );
        auto doc = document( handler.type );
        HANDLER_CONTEXT<BeginCommit> begin;
        begin.ClientName = "owner";
        *begin.Request.mutable_header()->mutable_document() = doc;
        auto started = handler.handleBeginCommit( begin );
        BOOST_REQUIRE( started );
        HANDLER_CONTEXT<SaveDocument> save;
        *save.Request.mutable_document() = doc;
        HANDLER_CONTEXT<SaveCopyOfDocument> copy;
        *copy.Request.mutable_document() = doc;
        HANDLER_CONTEXT<RevertDocument> revert;
        *revert.Request.mutable_document() = doc;
        revert.Request.set_discard_unsaved( true );
        HANDLER_CONTEXT<RunAction> action;
        *action.Request.mutable_document() = doc;
        action.Request.set_action( "common.Interactive.undo" );
        BOOST_CHECK( !handler.handleSaveDocument( save ) );
        BOOST_CHECK( !handler.handleSaveCopyOfDocument( copy ) );
        BOOST_CHECK( !handler.handleRevertDocument( revert ) );
        BOOST_CHECK( !handler.handleRunAction( action ) );
        BOOST_CHECK_EQUAL( handler.saved, 0 );
        BOOST_CHECK_EQUAL( handler.reverted, 0 );
        HANDLER_CONTEXT<EndCommit> end;
        end.ClientName = "owner";
        *end.Request.mutable_header()->mutable_document() = doc;
        *end.Request.mutable_id() = started->id();
        end.Request.set_action( CMA_DROP );
        BOOST_REQUIRE( handler.handleEndCommit( end ) );
        BOOST_CHECK( handler.handleSaveDocument( save ) );
    }
}

BOOST_AUTO_TEST_CASE( ActionRevertCannotBypassDirtyPolicyAndUnknownActionIsExplicit )
{
    DOCUMENT_HANDLER handler( types::DOCTYPE_PCB );
    HANDLER_CONTEXT<RunAction> ctx;
    *ctx.Request.mutable_document() = document( types::DOCTYPE_PCB );
    ctx.Request.set_action( "common.Control.revert" );
    handler.dirty = true;
    BOOST_CHECK( !handler.handleRunAction( ctx ) );
    BOOST_CHECK_EQUAL( handler.reverted, 0 );
    ctx.Request.set_action( "common.Control.save" );
    BOOST_REQUIRE( handler.handleRunAction( ctx ) );
    BOOST_CHECK_EQUAL( handler.saved, 1 );
    ctx.Request.set_action( "unknown.action" );
    auto result = handler.handleRunAction( ctx );
    BOOST_REQUIRE( result );
    BOOST_CHECK_EQUAL( result->status(), RAS_INVALID );
}

BOOST_AUTO_TEST_CASE( DrawingSheetParserRejectsForeignAndIncompleteDocuments )
{
    const wxString path = wxFileName::CreateTempFileName( "ipc-drawing-sheet" );
    for( const auto* input : { "(foreign_document)", "(page_layout", "(page_layout) junk",
                               "(kicad_wks (version 99999999))" } )
    {
        wxFFile file( path, "w" );
        BOOST_REQUIRE( file.Write( input ) );
        file.Close();
        DS_DATA_MODEL model;
        wxString error;
        BOOST_CHECK( !model.LoadDrawingSheet( path, &error ) );
        BOOST_CHECK( !error.IsEmpty() );
    }
    wxFFile file( path, "w" );
    BOOST_REQUIRE( file.Write( "(page_layout)" ) );
    file.Close();
    DS_DATA_MODEL model;
    wxString error;
    BOOST_CHECK( model.LoadDrawingSheet( path, &error ) );
    wxRemoveFile( path );
}

BOOST_AUTO_TEST_SUITE_END()
