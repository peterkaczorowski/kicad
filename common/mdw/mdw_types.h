/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

/**
 * @file mdw_types.h
 * @brief Core enumerations and the shared result base for the Managed Design
 *        Workspace (MDW) framework.
 *
 * These wxString-typed value structs are what MDW dialogs and trackers consume.
 * The MDW_BACKEND proxy maps them to and from the kiapi.workspace protobuf wire
 * vocabulary; nothing above the proxy sees a protobuf. The enum numbering is
 * kept in lockstep with workspace_types.proto so the mapping is a plain cast.
 */

#ifndef MDW_TYPES_H
#define MDW_TYPES_H

#include <kicommon.h>

#include <wx/string.h>
#include <functional>

/**
 * Four universal lifecycle categories. Every backend maps its native states
 * onto one of these. PENDING_REVIEW and RESTRICTED block writes; EDITABLE
 * allows writes silently; RELEASED allows writes but warns on save. RESTRICTED
 * is a policy-imposed read-only state, independent of a per-user lock.
 *
 * Numbering matches kiapi.workspace.LifecycleCategory.
 */
enum class MDW_LIFECYCLE_CATEGORY
{
    EDITABLE       = 0,
    PENDING_REVIEW = 1,
    RESTRICTED     = 2,
    RELEASED       = 3
};


/**
 * Attribute value types rendered by the schema-driven property editor.
 * Numbering matches kiapi.workspace.AttrType.
 */
enum class MDW_ATTR_TYPE
{
    STRING         = 0,
    INTEGER        = 1,
    FLOAT          = 2,
    DATE           = 3,
    DATETIME       = 4,
    BOOLEAN        = 5,
    ENUM           = 6,
    USER_REFERENCE = 7
};


/**
 * Pared framework error vocabulary. Numbering matches
 * kiapi.workspace.WorkspaceErrorCode. Backend-workflow error families that the
 * retired PLM contract carried (ECO, BOM, routing, release, CI, audit) are
 * intentionally absent; those workflows live in the backend or an IPC client.
 */
enum class MDW_ERROR_CODE
{
    SUCCESS                   = 0,
    NOT_SUPPORTED             = 1,
    NOT_AUTHENTICATED         = 2,
    AUTH_FAILED               = 3,
    PERMISSION_DENIED         = 4,
    NETWORK                   = 5,
    TIMEOUT                   = 6,
    LOCK_CONFLICT             = 7,
    NOT_LOCK_OWNER            = 8,
    ITEM_NOT_FOUND            = 9,
    REVISION_NOT_FOUND        = 10,
    VERSION_CONFLICT          = 11,
    INVALID_STATE_TRANSITION  = 12,
    NO_CHANGES                = 13,
    CANCELLED                 = 14,
    INVALID_PARAMETER         = 15,
    BACKEND_LOST              = 16,
    PROTOCOL                  = 17,
    INTERNAL                  = 18
};


/**
 * A translated one-line explanation of an error code, phrased for a user rather
 * than for a log. Callers supply their own context for what was being attempted;
 * this says only what went wrong.
 */
KICOMMON_API wxString MdwErrorMessage( MDW_ERROR_CODE aCode );


/**
 * Shared success/error payload carried by every operation result. Concrete
 * results add their own typed payload fields alongside this base.
 */
struct MDW_RESULT
{
    bool           success = false;
    MDW_ERROR_CODE error   = MDW_ERROR_CODE::SUCCESS;
    wxString       message;

    MDW_RESULT() = default;

    bool IsOk() const { return success; }

    static MDW_RESULT Ok()
    {
        MDW_RESULT r;
        r.success = true;
        return r;
    }

    static MDW_RESULT Fail( MDW_ERROR_CODE aCode, const wxString& aMessage = wxEmptyString )
    {
        MDW_RESULT r;
        r.success = false;
        r.error   = aCode;
        r.message = aMessage;
        return r;
    }
};


/**
 * Phases of a long-running operation for progress reporting.
 */
enum class MDW_PROGRESS_PHASE
{
    PREPARING,
    DOWNLOADING,
    UPLOADING,
    PROCESSING,
    FINALIZING
};


/**
 * Progress state for long-running operations.
 */
struct MDW_PROGRESS_INFO
{
    MDW_PROGRESS_PHASE phase = MDW_PROGRESS_PHASE::PREPARING;
    int                current = 0;
    int                total = 0;
    wxString           current_item;
    int                percent = -1;

    MDW_PROGRESS_INFO() = default;

    MDW_PROGRESS_INFO( MDW_PROGRESS_PHASE aPhase, int aCurrent, int aTotal,
                       const wxString& aItem = wxEmptyString ) :
            phase( aPhase ),
            current( aCurrent ),
            total( aTotal ),
            current_item( aItem )
    {
        if( total > 0 )
            percent = ( current * 100 ) / total;
    }
};


/**
 * Callback for progress reporting during long-running operations. Return false
 * to request cancellation of the operation.
 */
using MDW_PROGRESS_CALLBACK = std::function<bool( const MDW_PROGRESS_INFO& aInfo )>;


#endif // MDW_TYPES_H
