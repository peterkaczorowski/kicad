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
 * @file mdw_shared_libs.h
 * @brief Workspace-owned shared component libraries.
 *
 * A backend can publish symbol, footprint, and 3D model libraries that the
 * workspace owns centrally. The framework lists them and resolves each to a URI
 * the library table can consume.
 */

#ifndef MDW_SHARED_LIBS_H
#define MDW_SHARED_LIBS_H

#include "mdw_types.h"

#include <wx/string.h>
#include <vector>

/**
 * The kind of asset a shared library holds. Numbering matches
 * kiapi.workspace.SharedLibType.
 */
enum class MDW_SHARED_LIB_TYPE
{
    SYMBOL    = 0,
    FOOTPRINT = 1,
    MODEL_3D  = 2
};


/**
 * One workspace-owned library.
 */
struct MDW_SHARED_LIBRARY
{
    wxString            library_id;
    wxString            display_name;
    MDW_SHARED_LIB_TYPE type = MDW_SHARED_LIB_TYPE::SYMBOL;
    wxString            uri;
    wxString            version;
};


/**
 * The shared libraries the workspace publishes.
 */
struct MDW_SHARED_LIBRARY_LIST : public MDW_RESULT
{
    std::vector<MDW_SHARED_LIBRARY> libraries;
};


#endif // MDW_SHARED_LIBS_H
