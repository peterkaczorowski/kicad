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
 * @file mdw_parts.h
 * @brief Managed part identity for the Managed Design Workspace.
 *
 * A backend that owns a part library exposes each part's identity and lifecycle
 * category. custom_state_id names the backend display state layered on category.
 */

#ifndef MDW_PARTS_H
#define MDW_PARTS_H

#include "mdw_types.h"

#include <wx/string.h>
#include <vector>

/**
 * A managed part's identity and lifecycle position.
 */
struct MDW_PART
{
    wxString               part_id;
    wxString               part_number;
    wxString               description;
    wxString               revision;
    MDW_LIFECYCLE_CATEGORY category = MDW_LIFECYCLE_CATEGORY::EDITABLE;
    wxString               custom_state_id;
};


/**
 * A single resolved part.
 */
struct MDW_PART_RESULT : public MDW_RESULT
{
    MDW_PART part;
};


/**
 * A page of parts. next_page_token is empty on the last page.
 */
struct MDW_PART_LIST : public MDW_RESULT
{
    std::vector<MDW_PART> parts;
    wxString              next_page_token;
};


#endif // MDW_PARTS_H
