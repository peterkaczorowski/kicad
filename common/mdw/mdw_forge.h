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
 * @file mdw_forge.h
 * @brief Hosted-Git forge value structs for the Managed Design Workspace.
 *
 * A forge-backed workspace surfaces its merge-request state to the standard UI.
 * The framework does not model the review workflow; state is a backend display
 * string the UI renders verbatim.
 */

#ifndef MDW_FORGE_H
#define MDW_FORGE_H

#include "mdw_types.h"

#include <wx/string.h>
#include <vector>

/**
 * A single merge request as reported by the forge. state is a backend display
 * string.
 */
struct MDW_MERGE_REQUEST
{
    wxString id;
    wxString title;
    wxString author;
    wxString source_ref;
    wxString target_ref;
    wxString state;
    wxString url;
};


/**
 * Connection and identity summary for the backing forge.
 */
struct MDW_FORGE_INFO
{
    wxString provider;
    wxString repo_url;
    bool     authenticated = false;
};


/**
 * The forge summary plus the outcome of fetching it.
 */
struct MDW_FORGE_INFO_RESULT : public MDW_RESULT
{
    MDW_FORGE_INFO info;
};


/**
 * The merge requests visible to the current user.
 */
struct MDW_MERGE_REQUEST_LIST : public MDW_RESULT
{
    std::vector<MDW_MERGE_REQUEST> requests;
};


#endif // MDW_FORGE_H
