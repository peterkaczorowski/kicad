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

#include <qa_utils/wx_utils/unit_test_utils.h>

#include <board.h>
#include <pcb_shape.h>
#include <pcb_track.h>
#include <zone.h>
#include <tool/edit_points.h>
#include <tools/pcb_grid_helper.h>
#include <tools/block_drag_displacement.h>


namespace
{

std::shared_ptr<EDIT_POINTS> makeTriangle( EDA_ITEM* aParent, int aOffset = 0 )
{
    std::shared_ptr<EDIT_POINTS> points = std::make_shared<EDIT_POINTS>( aParent );

    points->AddPoint( VECTOR2I( aOffset, 0 ) );
    points->AddPoint( VECTOR2I( aOffset + 1000, 0 ) );
    points->AddPoint( VECTOR2I( aOffset + 1000, 1000 ) );
    points->AddLine( points->Point( 0 ), points->Point( 1 ) );
    points->AddLine( points->Point( 1 ), points->Point( 2 ) );

    return points;
}

} // namespace


BOOST_AUTO_TEST_SUITE( BlockDragDisplacementTests )


// The reference grid comes from the grabbed handle's parent item, with the
// current grid as the no-handle fallback
BOOST_AUTO_TEST_CASE( ReferenceGridComesFromGrabbedParent )
{
    BOARD board;

    PCB_SHAPE* shape = new PCB_SHAPE( &board, SHAPE_T::SEGMENT );
    ZONE*      zone = new ZONE( &board );
    PCB_VIA*   via = new PCB_VIA( &board );

    board.Add( shape );
    board.Add( zone );
    board.Add( via );

    PCB_GRID_HELPER helper;

    BOOST_CHECK( BlockDragReferenceGrid( helper, shape ) == GRID_GRAPHICS );
    BOOST_CHECK( BlockDragReferenceGrid( helper, via ) == GRID_VIAS );
    BOOST_CHECK( BlockDragReferenceGrid( helper, zone ) == GRID_CURRENT );
    BOOST_CHECK( BlockDragReferenceGrid( helper, nullptr ) == GRID_CURRENT );
}


// Two items, one drag. Every picked handle moves by the same offset, and no point
// is snapped again against its own item's grid
BOOST_AUTO_TEST_CASE( OneDisplacementReachesEveryPickedHandle )
{
    BOARD board;

    ZONE*      zone = new ZONE( &board );
    PCB_SHAPE* poly = new PCB_SHAPE( &board, SHAPE_T::POLY );

    board.Add( zone );
    board.Add( poly );

    std::shared_ptr<EDIT_POINTS> zonePoints = makeTriangle( zone );
    std::shared_ptr<EDIT_POINTS> polyPoints = makeTriangle( poly, 5000 );

    std::set<PCB_HANDLE_KEY> picked;
    picked.insert( *PCB_HANDLE_KEY::Make( *zonePoints, &zonePoints->Point( 0 ) ) );
    picked.insert( *PCB_HANDLE_KEY::Make( *zonePoints, &zonePoints->Point( 2 ) ) );
    picked.insert( *PCB_HANDLE_KEY::Make( *polyPoints, &polyPoints->Point( 1 ) ) );

    std::vector<EDIT_POINTS*> collections{ zonePoints.get(), polyPoints.get() };

    std::map<PCB_HANDLE_KEY, VECTOR2I> origins = BlockDragMoveOrigins( collections, picked );

    BOOST_REQUIRE_EQUAL( origins.size(), 3 );

    // An offset that lines up with no grid, as if it had been snapped against the
    // grabbed item alone. Every point lands at its start plus exactly this offset.
    const VECTOR2I delta( 333, -77 );

    BOOST_REQUIRE( BlockDragApplyDisplacement( collections, origins, delta ) );

    BOOST_CHECK( zonePoints->Point( 0 ).GetPosition() == VECTOR2I( 333, -77 ) );
    BOOST_CHECK( zonePoints->Point( 2 ).GetPosition() == VECTOR2I( 1333, 923 ) );
    BOOST_CHECK( polyPoints->Point( 1 ).GetPosition() == VECTOR2I( 6333, -77 ) );

    // Unpicked points did not move
    BOOST_CHECK( zonePoints->Point( 1 ).GetPosition() == VECTOR2I( 1000, 0 ) );
    BOOST_CHECK( polyPoints->Point( 0 ).GetPosition() == VECTOR2I( 5000, 0 ) );

    // Positions come from the starting points, so a second call with a new offset
    // does not add to the first
    const VECTOR2I delta2( 10, 10 );

    BOOST_REQUIRE( BlockDragApplyDisplacement( collections, origins, delta2 ) );
    BOOST_CHECK( zonePoints->Point( 0 ).GetPosition() == VECTOR2I( 10, 10 ) );
}


// A picked line expands to its endpoints and a shared endpoint moves exactly once
BOOST_AUTO_TEST_CASE( PickedLineExpandsWithoutDoubleMove )
{
    BOARD board;
    ZONE* zone = new ZONE( &board );
    board.Add( zone );

    std::shared_ptr<EDIT_POINTS> points = makeTriangle( zone );

    // Line 0 runs between points 0 and 1, and point 1 is picked on its own too
    std::set<PCB_HANDLE_KEY> picked;
    picked.insert( *PCB_HANDLE_KEY::Make( *points, &points->Line( 0 ) ) );
    picked.insert( *PCB_HANDLE_KEY::Make( *points, &points->Point( 1 ) ) );

    std::vector<EDIT_POINTS*> collections{ points.get() };

    std::map<PCB_HANDLE_KEY, VECTOR2I> origins = BlockDragMoveOrigins( collections, picked );

    // Endpoints 0 and 1 only, the line key itself is not in the map
    BOOST_REQUIRE_EQUAL( origins.size(), 2 );

    for( const auto& [key, origin] : origins )
        BOOST_CHECK( !key.IsLine() );

    const VECTOR2I delta( 500, 500 );

    BOOST_REQUIRE( BlockDragApplyDisplacement( collections, origins, delta ) );

    BOOST_CHECK( points->Point( 0 ).GetPosition() == VECTOR2I( 500, 500 ) );
    BOOST_CHECK( points->Point( 1 ).GetPosition() == VECTOR2I( 1500, 500 ) );
    BOOST_CHECK( points->Point( 2 ).GetPosition() == VECTOR2I( 1000, 1000 ) );
}


BOOST_AUTO_TEST_CASE( MissingIdentityInvalidatesGesture )
{
    BOARD board;
    ZONE* zone = new ZONE( &board );
    board.Add( zone );

    std::shared_ptr<EDIT_POINTS> before = makeTriangle( zone );

    std::set<PCB_HANDLE_KEY> picked;
    picked.insert( *PCB_HANDLE_KEY::Make( *before, &before->Point( 2 ) ) );

    std::vector<EDIT_POINTS*> collections{ before.get() };

    std::map<PCB_HANDLE_KEY, VECTOR2I> origins = BlockDragMoveOrigins( collections, picked );
    BOOST_REQUIRE_EQUAL( origins.size(), 1 );

    // The item loses a corner mid drag, so the collection is rebuilt smaller
    std::shared_ptr<EDIT_POINTS> after = std::make_shared<EDIT_POINTS>( zone );
    after->AddPoint( VECTOR2I( 0, 0 ) );
    after->AddPoint( VECTOR2I( 1000, 0 ) );

    std::vector<EDIT_POINTS*> rebuilt{ after.get() };

    BOOST_CHECK( !BlockDragApplyDisplacement( rebuilt, origins, VECTOR2I( 10, 10 ) ) );

    // Capture also refuses to start when a picked key has gone stale
    BOOST_CHECK( BlockDragMoveOrigins( rebuilt, picked ).empty() );
}


BOOST_AUTO_TEST_SUITE_END()
