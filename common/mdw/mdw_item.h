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
 * @file mdw_item.h
 * @brief Managed item metadata and the checkout/checkin operation structs.
 *
 * A single struct describes both files and folders; is_folder distinguishes
 * them. The category and lock fields carry last-known state so a browser
 * listing renders without follow-up calls.
 */

#ifndef MDW_ITEM_H
#define MDW_ITEM_H

#include "mdw_types.h"
#include "mdw_schema.h"
#include "mdw_revision.h"

#include <wx/string.h>
#include <wx/datetime.h>

/**
 * Metadata for one managed file or folder.
 */
struct MDW_ITEM_INFO
{
    wxString               item_id;
    wxString               display_name;
    wxString               folder_id;
    bool                   is_folder = false;
    wxDateTime             modified;
    wxString               revision_id;                                  ///< Empty for folders
    MDW_LIFECYCLE_CATEGORY category = MDW_LIFECYCLE_CATEGORY::EDITABLE;
    wxString               custom_state_id;
    bool                   is_locked = false;
    wxString               lock_owner;
};


/**
 * Request to check out an item to a local working copy. idempotency_id lets a
 * retry after a mid-call crash avoid double-applying the mutation.
 */
struct MDW_CHECKOUT_REQUEST
{
    wxString item_id;
    wxString local_path;
    bool     lock = false;
    wxString idempotency_id;
};


/**
 * Result of a checkout. On success, item reflects post-checkout state and
 * local_path names the working copy.
 */
struct MDW_CHECKOUT_RESULT : public MDW_RESULT
{
    MDW_ITEM_INFO item;
    wxString      local_path;
};


/**
 * Request to check in local modifications of an item.
 */
struct MDW_CHECKIN_REQUEST
{
    wxString          item_id;
    wxString          local_path;
    wxString          comment;
    MDW_ATTRIBUTE_SET attributes;
    wxString          idempotency_id;
};


/**
 * Result of a checkin. On success, revision names the created revision.
 */
struct MDW_CHECKIN_RESULT : public MDW_RESULT
{
    MDW_REVISION revision;
};


#endif // MDW_ITEM_H
