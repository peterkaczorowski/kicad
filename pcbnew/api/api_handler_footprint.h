/* Copyright The KiCad Developers, see AUTHORS.txt.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <api/api_handler_editor.h>

class FOOTPRINT_EDIT_FRAME;

class API_HANDLER_FOOTPRINT : public API_HANDLER_EDITOR
{
public:
    explicit API_HANDLER_FOOTPRINT( FOOTPRINT_EDIT_FRAME* aFrame );
private:
    FOOTPRINT_EDIT_FRAME* frame() const;
    kiapi::common::types::DocumentType thisDocumentType() const override
    { return kiapi::common::types::DOCTYPE_FOOTPRINT; }
    bool validateDocumentInternal( const DocumentSpecifier& ) const override;
    HANDLER_RESULT<Empty> saveDocumentInternal( const DocumentSpecifier& ) override;
    HANDLER_RESULT<Empty> saveCopyInternal( const DocumentSpecifier&, const wxString&, bool ) override;
    HANDLER_RESULT<Empty> revertDocumentInternal( const DocumentSpecifier& ) override;
    HANDLER_RESULT<commands::GetOpenDocumentsResponse> handleGetOpenDocuments(
            const HANDLER_CONTEXT<commands::GetOpenDocuments>& aCtx );
};
