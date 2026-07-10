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
 * @file mdw_capabilities.h
 * @brief Capability advertisement for the Managed Design Workspace framework.
 *
 * The retired PLM contract carried ~45 boolean capability flags. That surface
 * collapses here to a set of capability groups plus a few scalars. A backend
 * advertises the groups it implements; the framework consults the set before
 * exposing UI or dispatching any optional sub-interface.
 */

#ifndef MDW_CAPABILITIES_H
#define MDW_CAPABILITIES_H

#include "mdw_types.h"

#include <wx/string.h>
#include <set>
#include <vector>

/**
 * Optional capability groups. Mandatory checkout is not a group. Numbering
 * matches kiapi.workspace.CapabilityGroup.
 */
enum class MDW_CAPABILITY_GROUP
{
    UNKNOWN          = 0,
    LOCKING          = 1,
    REVISIONS        = 2,
    ATTRIBUTES       = 3,
    LIFECYCLE        = 4,
    EVENTS           = 5,
    SEARCH           = 6,
    DIFF             = 7,
    FORGE            = 8,
    SHARED_LIBRARIES = 9,
    BOM              = 10,
    ECO              = 11,
    REVIEW           = 12,
    PARTS            = 13,
    PERMISSIONS      = 14
};


/**
 * A backend-supplied display state layered on a lifecycle category. The
 * standard UI shows display_name; the framework consults only category.
 */
struct MDW_CUSTOM_STATE
{
    wxString               state_id;
    wxString               display_name;
    wxString               description;
    MDW_LIFECYCLE_CATEGORY category = MDW_LIFECYCLE_CATEGORY::EDITABLE;
    wxString               icon_name;
    int                    sort_order = 0;
};


/**
 * The capability advertisement returned by the handshake.
 */
struct MDW_CAPABILITIES
{
    std::set<MDW_CAPABILITY_GROUP> groups;
    bool                           supports_offline = false;   ///< Reserved; offline queue deferred
    std::vector<MDW_CUSTOM_STATE>  custom_states;

    bool Has( MDW_CAPABILITY_GROUP aGroup ) const
    {
        return groups.find( aGroup ) != groups.end();
    }

    const MDW_CUSTOM_STATE* FindCustomState( const wxString& aStateId ) const
    {
        for( const MDW_CUSTOM_STATE& state : custom_states )
        {
            if( state.state_id == aStateId )
                return &state;
        }

        return nullptr;
    }
};


#endif // MDW_CAPABILITIES_H
