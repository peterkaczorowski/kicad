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
 * @file mdw_eco.h
 * @brief Engineering change order value structs for the Managed Design Workspace.
 *
 * The framework reads ECO identity and the items it touches; it does not drive
 * the change workflow. state and eco_type are backend display strings the UI
 * renders verbatim.
 */

#ifndef MDW_ECO_H
#define MDW_ECO_H

#include "mdw_types.h"
#include "mdw_schema.h"

#include <wx/string.h>
#include <vector>

/**
 * An engineering change order as reported by the backend.
 */
struct MDW_ECO
{
    wxString              eco_id;
    wxString              title;
    wxString              eco_type;
    wxString              state;
    wxString              author;
    std::vector<wxString> affected_item_ids;
};


/**
 * A single resolved ECO.
 */
struct MDW_ECO_RESULT : public MDW_RESULT
{
    MDW_ECO eco;
};


/**
 * The vendor attribute vocabulary for ECOs.
 */
struct MDW_ECO_SCHEMA_RESULT : public MDW_RESULT
{
    MDW_SCHEMA schema;
};


#endif // MDW_ECO_H
