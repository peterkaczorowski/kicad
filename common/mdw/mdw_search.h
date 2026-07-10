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
 * @file mdw_search.h
 * @brief Search sub-interface value structs for the Managed Design Workspace.
 *
 * A backend advertises the fields it can filter on through the search schema.
 * The framework builds a query from user input and receives back a page of
 * items carrying their last-known metadata, so the results panel renders
 * without a follow-up call per hit.
 */

#ifndef MDW_SEARCH_H
#define MDW_SEARCH_H

#include "mdw_types.h"
#include "mdw_item.h"

#include <wx/string.h>
#include <utility>
#include <vector>

/**
 * One field a backend can be searched or filtered on.
 */
struct MDW_SEARCH_FIELD
{
    wxString              field_id;
    wxString              display_name;
    MDW_ATTR_TYPE         type = MDW_ATTR_TYPE::STRING;
    std::vector<wxString> enum_values;             ///< MDW_ATTR_TYPE::ENUM only
};


/**
 * The backend-declared set of searchable fields.
 */
struct MDW_SEARCH_SCHEMA
{
    std::vector<MDW_SEARCH_FIELD> fields;
};


/**
 * The search schema plus the outcome of fetching it.
 */
struct MDW_SEARCH_SCHEMA_RESULT : public MDW_RESULT
{
    MDW_SEARCH_SCHEMA schema;
};


/**
 * A search request. filters pairs a field_id with a value; page_size of 0 lets
 * the backend choose. page_token pages through a prior result.
 */
struct MDW_SEARCH_QUERY
{
    wxString                                     text;
    std::vector<std::pair<wxString, wxString>>   filters;
    unsigned                                     page_size = 0;
    wxString                                     page_token;
};


/**
 * A page of matching items with their last-known metadata. next_page_token is
 * empty on the last page.
 */
struct MDW_SEARCH_RESULT : public MDW_RESULT
{
    std::vector<MDW_ITEM_INFO> items;
    wxString                   next_page_token;
};


#endif // MDW_SEARCH_H
