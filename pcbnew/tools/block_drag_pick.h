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

#ifndef BLOCK_DRAG_PICK_H
#define BLOCK_DRAG_PICK_H

#include <set>

#include <tools/pcb_handle_key.h>

class EDIT_POINT;
class EDIT_POINTS;

/**
 * True when the user is allowed to pick this handle.
 *
 * The parent item has to be DEFORMABLE. Corner handles can be picked. An edge handle
 * can be picked on a zone or a graphic polygon, where dragging an edge is part of the
 * normal point editor. Midpoints of rectangles and segments are left out, because the
 * two corners they drive are the handles that really define the shape.
 *
 * Pass an item's own handle collection. The scaling box drawn around a multi-selection
 * borrows one member as its parent, so it does not belong here.
 */
bool BlockDragHandlePickable( const EDIT_POINTS& aCollection, const EDIT_POINT& aPoint );

/**
 * Add one clicked handle to the picked set.
 *
 * A plain click keeps just that handle. Shift-click toggles it and leaves the rest of
 * the set alone.
 */
void BlockDragApplyPick( std::set<PCB_HANDLE_KEY>& aPicked, const PCB_HANDLE_KEY& aKey, bool aAccumulate );

#endif // BLOCK_DRAG_PICK_H
