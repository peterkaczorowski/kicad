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

#include "block_drag_dependents.h"

#include <base_units.h>
#include <footprint.h>
#include <pcb_shape.h>


BLOCK_DRAG_DEPENDENT_RESULT
BlockDragFindDependents( const std::vector<PCB_SHAPE*>&          aCandidates,
                         const std::vector<BLOCK_DRAG_ENDPOINT>& aMovedEndpoints, bool aOverrideLocks,
                         std::vector<BLOCK_DRAG_ENDPOINT>& aDependents )
{
    aDependents.clear();

    const int     tol = pcbIUScale.mmToIU( 0.001 );
    const int64_t tolSq = (int64_t) tol * tol;

    auto withinTol = [&]( const VECTOR2I& aFirst, const VECTOR2I& aSecond )
    {
        return aFirst.SquaredDistance( aSecond ) <= tolSq;
    };

    auto isMoved = [&]( const PCB_SHAPE* aShape, bool aIsStart )
    {
        for( const BLOCK_DRAG_ENDPOINT& moved : aMovedEndpoints )
        {
            if( moved.shape == aShape && moved.isStart == aIsStart )
                return true;
        }

        return false;
    };

    auto alreadyAccepted = [&]( const PCB_SHAPE* aShape, bool aIsStart )
    {
        for( const BLOCK_DRAG_ENDPOINT& dep : aDependents )
        {
            if( dep.shape == aShape && dep.isStart == aIsStart )
                return true;
        }

        return false;
    };

    for( const BLOCK_DRAG_ENDPOINT& moved : aMovedEndpoints )
    {
        std::vector<BLOCK_DRAG_ENDPOINT> cluster;

        for( PCB_SHAPE* candidate : aCandidates )
        {
            if( !candidate || candidate->GetShape() != SHAPE_T::SEGMENT )
                continue;

            if( candidate->GetLayer() != moved.shape->GetLayer() )
                continue;

            for( bool isStart : { true, false } )
            {
                VECTOR2I pos = isStart ? candidate->GetStart() : candidate->GetEnd();

                // Distance first, so the scan over moved endpoints only runs for the
                // handful of candidates that sit on one.
                if( !withinTol( pos, moved.position ) )
                    continue;

                if( !isMoved( candidate, isStart ) )
                    cluster.push_back( { candidate, isStart, pos } );
            }
        }

        // Every end meeting at this joint has to sit on every other one, or there is
        // no single shape to follow.
        bool ambiguous = false;

        for( size_t ii = 0; ii < cluster.size() && !ambiguous; ++ii )
        {
            for( size_t jj = ii + 1; jj < cluster.size(); ++jj )
            {
                if( !withinTol( cluster[ii].position, cluster[jj].position ) )
                {
                    ambiguous = true;
                    break;
                }
            }
        }

        if( ambiguous )
            continue;

        for( const BLOCK_DRAG_ENDPOINT& member : cluster )
        {
            if( !alreadyAccepted( member.shape, member.isStart ) )
                aDependents.push_back( member );
        }
    }

    if( !aOverrideLocks )
    {
        for( const BLOCK_DRAG_ENDPOINT& dep : aDependents )
        {
            FOOTPRINT* parent = dep.shape->GetParentFootprint();

            // Same lock test the selected items get. IsLocked on its own misses a
            // locked parent footprint.
            if( dep.shape->IsLocked() || ( parent && parent->IsLocked() ) )
            {
                aDependents.clear();
                return BLOCK_DRAG_DEPENDENT_RESULT::LOCKED;
            }
        }
    }

    return BLOCK_DRAG_DEPENDENT_RESULT::OK;
}
