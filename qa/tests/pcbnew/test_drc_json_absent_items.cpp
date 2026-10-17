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
#include <pcb_track.h>
#include <drc/drc_item.h>
#include <rc_json_schema.h>
#include <units_provider.h>

BOOST_AUTO_TEST_CASE( DrcJsonDoesNotResolveAbsentItemSlots )
{
    BOARD board;
    PCB_TRACK main( &board );
    PCB_TRACK unrelated( &board );
    std::map<KIID, EDA_ITEM*> items = { { main.m_Uuid, &main }, { niluuid, &unrelated } };
    UNITS_PROVIDER units( pcbIUScale, EDA_UNITS::MM );
    std::shared_ptr<DRC_ITEM> item = DRC_ITEM::Create( DRCE_TRACK_WIDTH );
    item->SetItems( &main );
    RC_JSON::VIOLATION one;
    item->GetJsonViolation( one, &units, RPT_SEVERITY_ERROR, items );
    BOOST_REQUIRE_EQUAL( one.items.size(), 1u );
    BOOST_CHECK_EQUAL( one.items[0].uuid, main.m_Uuid.AsString() );

    item->SetItems( main.m_Uuid );
    RC_JSON::VIOLATION byId;
    item->GetJsonViolation( byId, &units, RPT_SEVERITY_ERROR, items );
    BOOST_CHECK_EQUAL( byId.items.size(), 1u );

    item->SetItems( nullptr );
    RC_JSON::VIOLATION empty;
    item->GetJsonViolation( empty, &units, RPT_SEVERITY_ERROR, items );
    BOOST_CHECK( empty.items.empty() );

    item->SetItems( &main, &unrelated );
    items[unrelated.m_Uuid] = &unrelated;
    RC_JSON::VIOLATION two;
    item->GetJsonViolation( two, &units, RPT_SEVERITY_ERROR, items );
    BOOST_CHECK_EQUAL( two.items.size(), 2u );
}
