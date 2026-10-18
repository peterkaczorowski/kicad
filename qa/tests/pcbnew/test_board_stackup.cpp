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

#include "qa_utils/wx_utils/unit_test_utils.h"
#include <boost/test/unit_test.hpp>

#include <board.h>
#include <board_design_settings.h>
#include <board_stackup_manager/board_stackup.h>
#include <layer_ids.h>


BOOST_AUTO_TEST_SUITE( BoardStackupValidation )

/**
 * Test that a default stackup for 2, 4, and MAX_CU_LAYERS copper layers is valid.
 */
BOOST_AUTO_TEST_CASE( AcceptsDefaultStackup )
{
    for( int copperCount : { 2, 4, MAX_CU_LAYERS } )
    {
        BOOST_TEST_CONTEXT( "Copper layer count: " << copperCount )
        {
            BOARD_DESIGN_SETTINGS settings( nullptr, "" );
            settings.SetCopperLayerCount( copperCount );

            BOARD_STACKUP stackup;
            stackup.BuildDefaultStackupList( &settings );

            const auto errors = stackup.Validate( settings.GetEnabledLayers() );

            // The default stackup must always be valid
            BOOST_CHECK( errors.empty() );
        }
    }
}

/**
 * Test that a negative sublayer thickness produces exactly one error
 * and that the error identifies the correct field, stackup item and sublayer.
 */
BOOST_AUTO_TEST_CASE( RejectsNegativeSublayerThickness )
{
    BOARD_DESIGN_SETTINGS settings( nullptr, "" );
    settings.SetCopperLayerCount( 2 );

    BOARD_STACKUP stackup;
    stackup.BuildDefaultStackupList( &settings );

    int dielectricIndex = -1;

    for( int i = 0; i < stackup.GetCount(); ++i )
    {
        if( stackup.GetStackupLayer( i )->GetType() == BS_ITEM_TYPE_DIELECTRIC )
        {
            dielectricIndex = i;
            break;
        }
    }

    BOOST_REQUIRE( dielectricIndex >= 0 );

    BOARD_STACKUP_ITEM* dielectric = stackup.GetStackupLayer( dielectricIndex );
    dielectric->AddDielectricPrms( 1 );

    const LSET enabledLayers = settings.GetEnabledLayers();

    // Test that an additional dielectric sublayer with default values
    // is accepted with its default values.
    BOOST_REQUIRE( stackup.Validate( enabledLayers ).empty() );

    dielectric->SetThickness( -1, 1 );

    const auto errors = stackup.Validate( enabledLayers );
    BOOST_REQUIRE_EQUAL( errors.size(), 1 );

    const auto& error = errors.front();
    BOOST_CHECK( error.m_FieldName == wxS( "thickness" ) );
    BOOST_CHECK_EQUAL( error.m_StackupItemIndex, dielectricIndex );
    BOOST_CHECK_EQUAL( error.m_SublayerIndex, 1 );
}


BOOST_AUTO_TEST_SUITE_END()
