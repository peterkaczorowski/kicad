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
 * @file mdw_state_mapper.h
 * @brief Maps backend custom states onto the four universal categories and the
 *        presentation the status pill draws.
 *
 * The backend advertises its custom states in the capability handshake, so the
 * mapper is fed that vocabulary rather than a per-lookup callback. The framework
 * consults only the universal category; the standard UI shows the custom-state
 * display name in the category's colour.
 */

#ifndef MDW_STATE_MAPPER_H
#define MDW_STATE_MAPPER_H

#include <kicommon.h>
#include "mdw_capabilities.h"

#include <gal/color4d.h>

#include <wx/string.h>
#include <vector>

class KICOMMON_API MDW_STATE_MAPPER
{
public:
    MDW_STATE_MAPPER() = default;

    void SetCustomStates( const std::vector<MDW_CUSTOM_STATE>& aStates ) { m_states = aStates; }

    /// Map a custom-state id onto its category, or EDITABLE when unknown.
    MDW_LIFECYCLE_CATEGORY GetCategory( const wxString& aCustomStateId ) const;

    /// The custom state for an id, or nullptr when unknown.
    const MDW_CUSTOM_STATE* GetCustomState( const wxString& aCustomStateId ) const;

    /// The display name to show for a custom-state id, falling back to the
    /// category name when the id is empty or unknown.
    wxString GetDisplayName( const wxString& aCustomStateId ) const;

    /// Human-readable name for a universal category.
    static wxString CategoryDisplayName( MDW_LIFECYCLE_CATEGORY aCategory );

    /// The pill colour for a category: green editable, yellow pending review,
    /// red restricted, blue released.
    static KIGFX::COLOR4D CategoryColor( MDW_LIFECYCLE_CATEGORY aCategory );

    /// True when writes to an item in this category should warn or be blocked.
    static bool CategoryBlocksWrites( MDW_LIFECYCLE_CATEGORY aCategory );

private:
    std::vector<MDW_CUSTOM_STATE> m_states;
};

#endif // MDW_STATE_MAPPER_H
