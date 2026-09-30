/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.TXT for contributors.
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

#include <board_stackup_manager/board_stackup.h>


BOOST_AUTO_TEST_SUITE( BoardStackup )


// A full 64 layer copper stack is thicker than the default board, so the computed dielectric
// thickness is negative and must fall back to the minimum. 64 copper + 63 dielectric + 6 items.
BOOST_AUTO_TEST_CASE( FullStackupPositiveThickness )
{
    BOARD_STACKUP stackup;
    stackup.BuildDefaultStackupList( nullptr, 0 );
    BOOST_REQUIRE_EQUAL( stackup.GetCount(), 133 );

    for( const BOARD_STACKUP_ITEM* item : stackup.GetList() )
    {
        if( item->GetType() == BS_ITEM_TYPE_DIELECTRIC )
        {
            for( int idx = 0; idx < item->GetSublayersCount(); idx++ )
                BOOST_CHECK_GT( item->GetThickness( idx ), 0 );
        }
    }
}


BOOST_AUTO_TEST_SUITE_END()
