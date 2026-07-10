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
 * @file mdw_diff.h
 * @brief Backend-side semantic compare value structs.
 *
 * This group carries a server-computed change summary for the history dialog.
 * The framework's visual schematic and PCB diff needs only two downloaded
 * revisions (the revisions group), not this summary.
 */

#ifndef MDW_DIFF_H
#define MDW_DIFF_H

#include "mdw_types.h"

#include <wx/string.h>
#include <vector>

/**
 * The kind of change a summary entry describes. Numbering matches
 * kiapi.mdw.ChangeType.
 */
enum class MDW_CHANGE_TYPE
{
    UNKNOWN  = 0,
    ADDED    = 1,
    REMOVED  = 2,
    MODIFIED = 3,
    MOVED    = 4
};


/**
 * One change between two revisions of an item.
 */
struct MDW_CHANGE_ENTRY
{
    MDW_CHANGE_TYPE change_type = MDW_CHANGE_TYPE::UNKNOWN;
    wxString        element_id;
    wxString        description;
};


/**
 * The full change summary between two revisions.
 */
struct MDW_CHANGE_SUMMARY_RESULT : public MDW_RESULT
{
    std::vector<MDW_CHANGE_ENTRY> changes;
};


#endif // MDW_DIFF_H
