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

#include <lseq.h>
#include <lset.h>
#include <tools/pcb_actions.h>


BOOST_AUTO_TEST_SUITE( PcbActions )


// Every possible copper layer has a layer switch action; the layer selector
// dereferences the result without a null check
BOOST_AUTO_TEST_CASE( LayerIDToActionCoversAllCopperLayers )
{
    LSEQ copperLayers = LSET::AllCuMask().CuStack();

    BOOST_REQUIRE_EQUAL( copperLayers.size(), 64 );

    for( PCB_LAYER_ID layer : copperLayers )
    {
        BOOST_TEST_CONTEXT( "Layer " << LSET::Name( layer ) )
        {
            TOOL_ACTION* action = PCB_ACTIONS::LayerIDToAction( layer );

            BOOST_REQUIRE( action != nullptr );
            BOOST_CHECK_EQUAL( action->GetParam<PCB_LAYER_ID>(), layer );
        }
    }
}


BOOST_AUTO_TEST_SUITE_END()
