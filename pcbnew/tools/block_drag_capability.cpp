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

#include "block_drag_capability.h"

#include <algorithm>
#include <functional>
#include <unordered_set>

#include <board.h>
#include <pcb_generator.h>
#include <pcb_group.h>
#include <pcb_shape.h>
#include <zone.h>


BLOCK_DRAG_CAPABILITY BlockDragCapability( const BOARD_ITEM& aItem )
{
    const BOARD* board = aItem.GetBoard();
    bool         footprintHolder = board && board->IsFootprintHolder();

    switch( aItem.Type() )
    {
    case PCB_TRACE_T:
    case PCB_ARC_T:
    case PCB_VIA_T:
    case PCB_FOOTPRINT_T: return footprintHolder ? BLOCK_DRAG_CAPABILITY::UNSUPPORTED : BLOCK_DRAG_CAPABILITY::ROUTER;

    case PCB_PAD_T: return footprintHolder ? BLOCK_DRAG_CAPABILITY::RIGID : BLOCK_DRAG_CAPABILITY::ROUTER;

    case PCB_ZONE_T:
        if( static_cast<const ZONE&>( aItem ).IsTeardropArea() )
            return BLOCK_DRAG_CAPABILITY::UNSUPPORTED;

        // A zone inside a footprint belongs to that footprint. Reshaping it from the
        // board editor would edit the footprint from outside its own editor.
        if( !footprintHolder && aItem.GetParentFootprint() )
            return BLOCK_DRAG_CAPABILITY::RIGID;

        return BLOCK_DRAG_CAPABILITY::DEFORMABLE;

    case PCB_SHAPE_T:
    {
        // Same reason as a zone inside a footprint.
        if( !footprintHolder && aItem.GetParentFootprint() )
            return BLOCK_DRAG_CAPABILITY::RIGID;

        switch( static_cast<const PCB_SHAPE&>( aItem ).GetShape() )
        {
        case SHAPE_T::SEGMENT:
        case SHAPE_T::POLY:
        case SHAPE_T::RECTANGLE: return BLOCK_DRAG_CAPABILITY::DEFORMABLE;

        case SHAPE_T::ARC:
        case SHAPE_T::CIRCLE:
        case SHAPE_T::BEZIER: return BLOCK_DRAG_CAPABILITY::RIGID;

        default: return BLOCK_DRAG_CAPABILITY::UNSUPPORTED;
        }
    }

    case PCB_GENERATOR_T:
    {
        // Safe to carry only when Move() takes every member along. A via stitch
        // pattern re-places its vias when it regenerates, so it needs the router.
        wxString genType = static_cast<const PCB_GENERATOR&>( aItem ).GetGeneratorType();

        if( genType == wxS( "tuning_pattern" ) || genType == wxS( "via_stack" ) )
            return BLOCK_DRAG_CAPABILITY::RIGID;

        return BLOCK_DRAG_CAPABILITY::UNSUPPORTED;
    }

    case PCB_TEXT_T:
    case PCB_FIELD_T:
    case PCB_TEXTBOX_T:
    case PCB_TABLE_T:
    case PCB_TABLECELL_T:
    case PCB_REFERENCE_IMAGE_T:
    case PCB_BARCODE_T:
    case PCB_DIM_ALIGNED_T:
    case PCB_DIM_LEADER_T:
    case PCB_DIM_CENTER_T:
    case PCB_DIM_RADIAL_T:
    case PCB_DIM_ORTHOGONAL_T:
    case PCB_TARGET_T:
    case PCB_POINT_T:
    case PCB_GRID_ITEM_T:
    case PCB_DRILL_CHART_T:
    case PCB_DRILL_MAP_T: return BLOCK_DRAG_CAPABILITY::RIGID;

    default: return BLOCK_DRAG_CAPABILITY::UNSUPPORTED;
    }
}


std::vector<BOARD_ITEM*> BlockDragExpandSelection( const std::vector<BOARD_ITEM*>& aSelection )
{
    std::vector<BOARD_ITEM*>              expanded;
    std::unordered_set<const BOARD_ITEM*> seen;

    std::function<void( BOARD_ITEM* )> add = [&]( BOARD_ITEM* aItem )
    {
        if( !aItem || !seen.insert( aItem ).second )
            return;

        // A DRC marker is not board content, so there is nothing here to edit.
        if( aItem->Type() == PCB_MARKER_T )
            return;

        if( aItem->Type() == PCB_GROUP_T )
        {
            for( BOARD_ITEM* member : static_cast<PCB_GROUP*>( aItem )->GetBoardItems() )
            {
                add( member );
            }

            return;
        }

        expanded.push_back( aItem );
    };

    for( BOARD_ITEM* item : aSelection )
        add( item );

    // A footprint moves its own children, so a child selected alongside its parent would
    // take the displacement once from the parent and once from itself
    std::erase_if( expanded,
                   [&]( const BOARD_ITEM* aItem )
                   {
                       FOOTPRINT* parent = aItem->GetParentFootprint();

                       return parent && seen.count( parent );
                   } );

    return expanded;
}
