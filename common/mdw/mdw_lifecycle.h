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
 * @file mdw_lifecycle.h
 * @brief Lifecycle transition requests and results.
 *
 * The framework consults only the four universal categories. A transition
 * targets a backend custom state; the backend reports the resulting category.
 */

#ifndef MDW_LIFECYCLE_H
#define MDW_LIFECYCLE_H

#include "mdw_types.h"

#include <wx/string.h>

/**
 * A transition available from an item's current state.
 */
struct MDW_VALID_TRANSITION
{
    wxString               target_state_id;
    wxString               display_name;
    wxString               description;
    MDW_LIFECYCLE_CATEGORY target_category = MDW_LIFECYCLE_CATEGORY::EDITABLE;
    bool                   requires_comment = false;
};


/**
 * Result of a lifecycle transition request.
 */
struct MDW_TRANSITION_RESULT : public MDW_RESULT
{
    wxString               new_state_id;
    MDW_LIFECYCLE_CATEGORY new_category = MDW_LIFECYCLE_CATEGORY::EDITABLE;
};


#endif // MDW_LIFECYCLE_H
