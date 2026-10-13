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

#ifndef BLOCK_DRAG_DEPENDENTS_H
#define BLOCK_DRAG_DEPENDENTS_H

#include <vector>

#include <math/vector2d.h>

class PCB_SHAPE;

/**
 * One end of a graphic segment, either a moved seed or a discovered dependent.
 */
struct BLOCK_DRAG_ENDPOINT
{
    PCB_SHAPE* shape;
    bool       isStart;
    VECTOR2I   position;
};

enum class BLOCK_DRAG_DEPENDENT_RESULT
{
    OK,

    /// A shape that would have to follow is locked and Override Locks is off, so the
    /// drag has to stop.
    LOCKED
};

/**
 * Find the joined endpoints that have to follow the ones the drag moves.
 *
 * An endpoint follows when it shares a layer with a moved endpoint and sits within
 * 1 micrometre of it. Only ends count, never a point along a shape. When the endpoints
 * gathered around one moved endpoint are not all within tolerance of each other the
 * match is ambiguous, so that endpoint is left alone and the tolerance stays too small
 * to close a gap the user left on purpose.
 *
 * The caller assembles aCandidates: unselected graphic segments of the editing
 * scope. Selected items are excluded there because an unpicked selected object
 * translates rigidly in the full gesture and must not also be deformed. The scope
 * also decides footprint children: the board editor passes board drawings only,
 * the footprint editor passes the footprint's own children, and this module
 * accepts whatever it is given, including children with the parent lock clause.
 *
 * @return LOCKED when a shape that would follow, or its parent footprint, is locked and
 *         aOverrideLocks is false, with aDependents cleared.
 */
BLOCK_DRAG_DEPENDENT_RESULT
BlockDragFindDependents( const std::vector<PCB_SHAPE*>&          aCandidates,
                         const std::vector<BLOCK_DRAG_ENDPOINT>& aMovedEndpoints, bool aOverrideLocks,
                         std::vector<BLOCK_DRAG_ENDPOINT>& aDependents );

#endif // BLOCK_DRAG_DEPENDENTS_H
