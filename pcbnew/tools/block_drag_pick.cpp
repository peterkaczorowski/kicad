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
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "block_drag_pick.h"

#include <board_item.h>
#include <tool/edit_points.h>
#include <tools/block_drag_capability.h>


bool BlockDragHandlePickable( const EDIT_POINTS& aCollection, const EDIT_POINT& aPoint )
{
    const EDA_ITEM* parent = aCollection.GetParent();

    if( !parent || !parent->IsBOARD_ITEM() )
        return false;

    const BOARD_ITEM& item = static_cast<const BOARD_ITEM&>( *parent );

    if( BlockDragCapability( item ) != BLOCK_DRAG_CAPABILITY::DEFORMABLE )
        return false;

    if( dynamic_cast<const EDIT_LINE*>( &aPoint ) )
        return item.Type() == PCB_ZONE_T;

    return true;
}


void BlockDragApplyPick( std::set<PCB_HANDLE_KEY>& aPicked, const PCB_HANDLE_KEY& aKey, bool aAccumulate )
{
    if( !aAccumulate )
    {
        aPicked.clear();
        aPicked.insert( aKey );
        return;
    }

    if( !aPicked.erase( aKey ) )
        aPicked.insert( aKey );
}
