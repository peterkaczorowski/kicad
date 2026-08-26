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

#include <mdw/mdw_state_mapper.h>

#include <wx/translation.h>


const MDW_CUSTOM_STATE* MDW_STATE_MAPPER::GetCustomState( const wxString& aCustomStateId ) const
{
    for( const MDW_CUSTOM_STATE& state : m_states )
    {
        if( state.state_id == aCustomStateId )
            return &state;
    }

    return nullptr;
}


MDW_LIFECYCLE_CATEGORY MDW_STATE_MAPPER::GetCategory( const wxString& aCustomStateId ) const
{
    if( const MDW_CUSTOM_STATE* state = GetCustomState( aCustomStateId ) )
        return state->category;

    return MDW_LIFECYCLE_CATEGORY::EDITABLE;
}


wxString MDW_STATE_MAPPER::GetDisplayName( const wxString& aCustomStateId ) const
{
    if( const MDW_CUSTOM_STATE* state = GetCustomState( aCustomStateId ) )
        return state->display_name;

    return CategoryDisplayName( GetCategory( aCustomStateId ) );
}


wxString MDW_STATE_MAPPER::CategoryDisplayName( MDW_LIFECYCLE_CATEGORY aCategory )
{
    switch( aCategory )
    {
    case MDW_LIFECYCLE_CATEGORY::EDITABLE:       return _( "Editable" );
    case MDW_LIFECYCLE_CATEGORY::PENDING_REVIEW: return _( "Pending Review" );
    case MDW_LIFECYCLE_CATEGORY::RESTRICTED:     return _( "Restricted" );
    case MDW_LIFECYCLE_CATEGORY::RELEASED:       return _( "Released" );
    }

    return wxEmptyString;
}


KIGFX::COLOR4D MDW_STATE_MAPPER::CategoryColor( MDW_LIFECYCLE_CATEGORY aCategory )
{
    switch( aCategory )
    {
    case MDW_LIFECYCLE_CATEGORY::EDITABLE:       return KIGFX::COLOR4D( 0.18, 0.49, 0.20, 1.0 );
    case MDW_LIFECYCLE_CATEGORY::PENDING_REVIEW: return KIGFX::COLOR4D( 0.98, 0.66, 0.14, 1.0 );
    case MDW_LIFECYCLE_CATEGORY::RESTRICTED:     return KIGFX::COLOR4D( 0.78, 0.16, 0.16, 1.0 );
    case MDW_LIFECYCLE_CATEGORY::RELEASED:       return KIGFX::COLOR4D( 0.08, 0.40, 0.75, 1.0 );
    }

    return KIGFX::COLOR4D( 0.5, 0.5, 0.5, 1.0 );
}


bool MDW_STATE_MAPPER::CategoryBlocksWrites( MDW_LIFECYCLE_CATEGORY aCategory )
{
    return aCategory == MDW_LIFECYCLE_CATEGORY::PENDING_REVIEW
           || aCategory == MDW_LIFECYCLE_CATEGORY::RESTRICTED
           || aCategory == MDW_LIFECYCLE_CATEGORY::RELEASED;
}
