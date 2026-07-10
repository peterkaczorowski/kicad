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
 * @file mdw_bom.h
 * @brief BOM value structs for the Managed Design Workspace.
 *
 * The framework treats a BOM as opaque rows carrying schema-typed attributes.
 * It transforms nothing; the vendor vocabulary comes from the BOM schema
 * accessor and the schema-driven property editor renders each line.
 */

#ifndef MDW_BOM_H
#define MDW_BOM_H

#include "mdw_types.h"
#include "mdw_schema.h"

#include <wx/string.h>
#include <vector>

/**
 * One BOM line carrying schema-typed attributes.
 */
struct MDW_BOM_LINE
{
    wxString          item_id;
    wxString          reference;
    unsigned          quantity = 0;
    MDW_ATTRIBUTE_SET attributes;
};


/**
 * A BOM as reported by the backend. bom_type is a backend display string.
 */
struct MDW_BOM
{
    wxString                  bom_id;
    wxString                  bom_type;
    std::vector<MDW_BOM_LINE> lines;
};


/**
 * A single resolved BOM.
 */
struct MDW_BOM_RESULT : public MDW_RESULT
{
    MDW_BOM bom;
};


/**
 * The vendor attribute vocabulary for BOM lines.
 */
struct MDW_BOM_SCHEMA_RESULT : public MDW_RESULT
{
    MDW_SCHEMA schema;
};


#endif // MDW_BOM_H
