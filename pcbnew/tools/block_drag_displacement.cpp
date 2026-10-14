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

#include "block_drag_displacement.h"

#include <tool/edit_points.h>


static EDIT_POINT* resolveInCollections( const std::vector<EDIT_POINTS*>& aCollections, const PCB_HANDLE_KEY& aKey,
                                         EDIT_POINTS** aOwner = nullptr )
{
    for( EDIT_POINTS* points : aCollections )
    {
        if( EDIT_POINT* point = aKey.Resolve( *points ) )
        {
            if( aOwner )
                *aOwner = points;

            return point;
        }
    }

    return nullptr;
}


std::map<PCB_HANDLE_KEY, VECTOR2I> BlockDragMoveOrigins( const std::vector<EDIT_POINTS*>& aCollections,
                                                         const std::set<PCB_HANDLE_KEY>&  aPicked )
{
    std::map<PCB_HANDLE_KEY, VECTOR2I> origins;

    for( const PCB_HANDLE_KEY& key : aPicked )
    {
        EDIT_POINTS* owner = nullptr;
        EDIT_POINT*  point = resolveInCollections( aCollections, key, &owner );

        if( !point )
            return {};

        EDIT_LINE* line = dynamic_cast<EDIT_LINE*>( point );

        if( !line )
        {
            origins.emplace( key, point->GetPosition() );
            continue;
        }

        for( EDIT_POINT* endpoint : { &line->GetOrigin(), &line->GetEnd() } )
        {
            std::optional<PCB_HANDLE_KEY> endpointKey = PCB_HANDLE_KEY::Make( *owner, endpoint );

            if( !endpointKey )
                return {};

            origins.emplace( *endpointKey, endpoint->GetPosition() );
        }
    }

    return origins;
}


bool BlockDragApplyDisplacement( const std::vector<EDIT_POINTS*>&          aCollections,
                                 const std::map<PCB_HANDLE_KEY, VECTOR2I>& aOrigins, const VECTOR2I& aDelta )
{
    for( const auto& [key, origin] : aOrigins )
    {
        EDIT_POINT* point = resolveInCollections( aCollections, key );

        if( !point )
            return false;

        point->SetPosition( origin + aDelta );
    }

    return true;
}
