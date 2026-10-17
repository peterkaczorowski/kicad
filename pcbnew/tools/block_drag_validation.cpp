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

#include "block_drag_validation.h"

#include <board.h>
#include <board_item.h>
#include <geometry/shape_poly_set.h>
#include <layer_ids.h>
#include <zone.h>

namespace
{

/**
 * Whether a zone outline is sound enough to keep.
 *
 * IsSelfIntersecting() on its own accepts an outline that has collapsed below three
 * corners, and it accepts holes that meet at a corner. It also calls every zone that
 * has a hole self-intersecting, because it walks the outline and the holes as one
 * list of segments.
 */
bool zoneOutlineIsSound( const SHAPE_POLY_SET& aOutline )
{
    if( aOutline.OutlineCount() == 0 || aOutline.IsSelfIntersecting() || aOutline.HasTouchingHoles() )
        return false;

    for( int ii = 0; ii < aOutline.OutlineCount(); ++ii )
    {
        if( aOutline.COutline( ii ).PointCount() < 3 )
            return false;
    }

    return true;
}

} // namespace


BLOCK_DRAG_VALIDATION::BLOCK_DRAG_VALIDATION( BOARD& aBoard ) :
        m_board( &aBoard ),
        m_outlineAffected( false ),
        m_outlineWasValid( false )
{
}


void BLOCK_DRAG_VALIDATION::CaptureBaseline( const std::vector<BOARD_ITEM*>& aCarriedItems )
{
    m_zones.clear();
    m_outlineAffected = false;
    m_outlineWasValid = false;

    // The footprint editor's board has no outline of its own, and a footprint's
    // Edge.Cuts shapes are content the user is meant to move.
    const bool outlineMatters = !m_board->IsFootprintHolder();

    for( BOARD_ITEM* item : aCarriedItems )
    {
        if( !item )
            continue;

        if( item->Type() == PCB_ZONE_T )
        {
            ZONE* zone = static_cast<ZONE*>( item );
            m_zones.emplace_back( zone, zoneOutlineIsSound( *zone->Outline() ) );
        }

        if( outlineMatters && item->IsOnLayer( Edge_Cuts ) )
            m_outlineAffected = true;
    }

    if( m_outlineAffected )
        m_outlineWasValid = outlineIsValid();
}


BLOCK_DRAG_VALIDATION_RESULT BLOCK_DRAG_VALIDATION::Validate() const
{
    for( const auto& [zone, wasValid] : m_zones )
    {
        if( wasValid && !zoneOutlineIsSound( *zone->Outline() ) )
            return BLOCK_DRAG_VALIDATION_RESULT::INVALID_ZONE;
    }

    if( m_outlineAffected && m_outlineWasValid && !outlineIsValid() )
        return BLOCK_DRAG_VALIDATION_RESULT::INVALID_OUTLINE;

    return BLOCK_DRAG_VALIDATION_RESULT::OK;
}


bool BLOCK_DRAG_VALIDATION::outlineIsValid() const
{
    SHAPE_POLY_SET outlines;

    return m_board->GetBoardPolygonOutlines( outlines, false );
}
