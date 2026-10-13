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
#include <zone.h>
#include <constraints/pcb_constraint.h>
#include <tool/edit_points.h>
#include <tools/block_drag_pick.h>


namespace
{

std::shared_ptr<EDIT_POINTS> makeCollection( EDA_ITEM* aParent )
{
    std::shared_ptr<EDIT_POINTS> points = std::make_shared<EDIT_POINTS>( aParent );

    points->AddPoint( VECTOR2I( 0, 0 ) );
    points->AddPoint( VECTOR2I( 100, 0 ) );
    points->AddPoint( VECTOR2I( 100, 100 ) );
    points->AddLine( points->Point( 0 ), points->Point( 1 ) );

    return points;
}

} // namespace


BOOST_AUTO_TEST_SUITE( BlockDragPickTests )


BOOST_AUTO_TEST_CASE( PickableFollowsCapabilityAndHandleKind )
{
    BOARD board;

    ZONE*      zone = new ZONE( &board );
    PCB_SHAPE* poly = new PCB_SHAPE( &board, SHAPE_T::POLY );
    PCB_SHAPE* arc = new PCB_SHAPE( &board, SHAPE_T::ARC );

    board.Add( zone );
    board.Add( poly );
    board.Add( arc );

    std::shared_ptr<EDIT_POINTS> zonePoints = makeCollection( zone );
    std::shared_ptr<EDIT_POINTS> polyPoints = makeCollection( poly );
    std::shared_ptr<EDIT_POINTS> arcPoints = makeCollection( arc );

    // Corners of reshapeable items can be picked
    BOOST_CHECK( BlockDragHandlePickable( *zonePoints, zonePoints->Point( 0 ) ) );
    BOOST_CHECK( BlockDragHandlePickable( *polyPoints, polyPoints->Point( 2 ) ) );

    // Line handles are pickable only on zones
    BOOST_CHECK( BlockDragHandlePickable( *zonePoints, zonePoints->Line( 0 ) ) );
    BOOST_CHECK( !BlockDragHandlePickable( *polyPoints, polyPoints->Line( 0 ) ) );

    // An item that only travels whole offers nothing to pick
    BOOST_CHECK( !BlockDragHandlePickable( *arcPoints, arcPoints->Point( 0 ) ) );
}


BOOST_AUTO_TEST_CASE( ConstrainedParentIsPickable )
{
    BOARD board;

    PCB_SHAPE* segment = new PCB_SHAPE( &board, SHAPE_T::SEGMENT );
    board.Add( segment );

    PCB_CONSTRAINT* constraint = new PCB_CONSTRAINT( &board, PCB_CONSTRAINT_TYPE::HORIZONTAL );
    constraint->AddMember( segment->m_Uuid, CONSTRAINT_ANCHOR::WHOLE );
    board.Add( constraint );

    std::shared_ptr<EDIT_POINTS> points = makeCollection( segment );

    BOOST_CHECK( BlockDragHandlePickable( *points, points->Point( 0 ) ) );
}


BOOST_AUTO_TEST_CASE( PlainPickReplacesSet )
{
    BOARD board;
    ZONE* zone = new ZONE( &board );
    board.Add( zone );

    std::shared_ptr<EDIT_POINTS> points = makeCollection( zone );

    std::optional<PCB_HANDLE_KEY> key0 = PCB_HANDLE_KEY::Make( *points, &points->Point( 0 ) );
    std::optional<PCB_HANDLE_KEY> key1 = PCB_HANDLE_KEY::Make( *points, &points->Point( 1 ) );
    std::optional<PCB_HANDLE_KEY> key2 = PCB_HANDLE_KEY::Make( *points, &points->Point( 2 ) );

    BOOST_REQUIRE( key0 && key1 && key2 );

    std::set<PCB_HANDLE_KEY> picked{ *key0, *key1 };

    BlockDragApplyPick( picked, *key2, false );

    BOOST_REQUIRE_EQUAL( picked.size(), 1 );
    BOOST_CHECK( picked.count( *key2 ) == 1 );

    // A plain click on an already picked handle leaves just that handle
    picked = { *key0, *key1 };
    BlockDragApplyPick( picked, *key1, false );

    BOOST_REQUIRE_EQUAL( picked.size(), 1 );
    BOOST_CHECK( picked.count( *key1 ) == 1 );
}


BOOST_AUTO_TEST_CASE( AccumulatingPickToggles )
{
    BOARD board;
    ZONE* zone = new ZONE( &board );
    board.Add( zone );

    std::shared_ptr<EDIT_POINTS> points = makeCollection( zone );

    std::optional<PCB_HANDLE_KEY> key0 = PCB_HANDLE_KEY::Make( *points, &points->Point( 0 ) );
    std::optional<PCB_HANDLE_KEY> key1 = PCB_HANDLE_KEY::Make( *points, &points->Point( 1 ) );

    BOOST_REQUIRE( key0 && key1 );

    std::set<PCB_HANDLE_KEY> picked;

    BlockDragApplyPick( picked, *key0, true );
    BlockDragApplyPick( picked, *key1, true );

    BOOST_REQUIRE_EQUAL( picked.size(), 2 );

    // Toggling one off leaves the rest alone
    BlockDragApplyPick( picked, *key0, true );

    BOOST_REQUIRE_EQUAL( picked.size(), 1 );
    BOOST_CHECK( picked.count( *key1 ) == 1 );
}


BOOST_AUTO_TEST_SUITE_END()
