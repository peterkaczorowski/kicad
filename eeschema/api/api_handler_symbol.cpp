/* Copyright The KiCad Developers, see AUTHORS.txt.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <api/api_handler_symbol.h>
#include <api/api_utils.h>
#include <symbol_editor/symbol_edit_frame.h>
#include <lib_symbol.h>

using namespace kiapi::common;
using namespace kiapi::common::commands;

API_HANDLER_SYMBOL::API_HANDLER_SYMBOL( SYMBOL_EDIT_FRAME* aFrame ) : m_frame( aFrame )
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
    if( LIB_SYMBOL* symbol = m_frame->GetCurSymbol() )
    {
        auto* doc = response.add_documents();
        doc->set_type( types::DOCTYPE_SYMBOL );
        *doc->mutable_lib_id() = LibIdToProto( symbol->GetLibId() );
    }
    return response;
}
