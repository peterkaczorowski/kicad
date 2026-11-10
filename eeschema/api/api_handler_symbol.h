/* Copyright The KiCad Developers, see AUTHORS.txt.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef KICAD_API_HANDLER_SYMBOL_H
#define KICAD_API_HANDLER_SYMBOL_H

#include <api/api_handler.h>
#include <api/common/commands/editor_commands.pb.h>

class SYMBOL_EDIT_FRAME;

class API_HANDLER_SYMBOL : public API_HANDLER
{
public:
    explicit API_HANDLER_SYMBOL( SYMBOL_EDIT_FRAME* aFrame );

private:
    HANDLER_RESULT<kiapi::common::commands::GetOpenDocumentsResponse> handleGetOpenDocuments(
            const HANDLER_CONTEXT<kiapi::common::commands::GetOpenDocuments>& aCtx );
    SYMBOL_EDIT_FRAME* m_frame;
};

#endif
