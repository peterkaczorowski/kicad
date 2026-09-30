/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file test_3d_viewer_layer_presets.cpp
 * Regression test for 3D viewer layer-preset serialization.
 *
 * The PARAM_LAYER_PRESET_3D name<->layer map omitted the numbered User.1..User.45
 * layers and the grid axes layer. Those bits were lost on a save/load round-trip
 * through the JSON settings, which is why kicad-cli renders ignored them.
 */

#include <qa_utils/wx_utils/unit_test_utils.h>

#include <layer_ids.h>
#include <3d_viewer/eda_3d_viewer_settings.h>
#include <3d_canvas/board_adapter.h>

#include <bitset>


BOOST_AUTO_TEST_SUITE( Viewer3DLayerPresets )


// A preset that enables named user layers, numbered User.N layers and the grid axes
// must survive a Store()/Load() round-trip unchanged.
BOOST_AUTO_TEST_CASE( PresetLayerVisibilityRoundTrip )
{
    EDA_3D_VIEWER_SETTINGS cfg;

    LAYER_PRESET_3D preset( wxS( "regression" ) );
    preset.layers.reset();
    preset.layers.set( LAYER_3D_BOARD );
    preset.layers.set( LAYER_3D_USER_COMMENTS );
    preset.layers.set( LAYER_3D_USER_1 );
    preset.layers.set( LAYER_3D_USER_45 );
    preset.layers.set( LAYER_GRID_AXES );

    const std::bitset<LAYER_3D_END> expected = preset.layers;

    cfg.m_LayerPresets.push_back( preset );

    cfg.Store();
    cfg.m_LayerPresets.clear();
    cfg.Load();

    BOOST_REQUIRE_EQUAL( cfg.m_LayerPresets.size(), 1u );

    const std::bitset<LAYER_3D_END>& actual = cfg.m_LayerPresets[0].layers;

    BOOST_CHECK( actual.test( LAYER_3D_USER_COMMENTS ) );
    BOOST_CHECK( actual.test( LAYER_3D_USER_1 ) );
    BOOST_CHECK( actual.test( LAYER_3D_USER_45 ) );
    BOOST_CHECK( actual.test( LAYER_GRID_AXES ) );

    // Unknown layer names also corrupt bit 0 on load, so the full set must match.
    BOOST_CHECK( actual == expected );
}


BOOST_AUTO_TEST_CASE( AllInnerCopperLayersRoundTrip )
{
    EDA_3D_VIEWER_SETTINGS cfg;
    cfg.Load();

    LAYER_PRESET_3D preset( wxS( "inner copper" ) );
    BOARD_ADAPTER   adapter;
    adapter.m_Cfg = &cfg;
    cfg.m_CurrentPreset.clear();

    for( int inner = 1; inner <= 62; ++inner )
    {
        PCB_LAYER_ID pcbLayer = static_cast<PCB_LAYER_ID>( In1_Cu + ( inner - 1 ) * 2 );
        int          layer3D = MapPCBLayerTo3DLayer( pcbLayer );

        BOOST_REQUIRE_LT( layer3D, LAYER_3D_END );
        BOOST_CHECK_EQUAL( Map3DLayerToPCBLayer( layer3D ), pcbLayer );
        BOOST_CHECK( preset.layers.test( layer3D ) );
        BOOST_CHECK( adapter.GetDefaultVisibleLayers().test( layer3D ) );
        BOOST_CHECK( cfg.m_Render.show_copper_inner[inner - 1] );
        preset.layers.set( layer3D, inner % 2 == 0 );
    }

    adapter.SetVisibleLayers( preset.layers );

    for( int inner = 1; inner <= 62; ++inner )
    {
        PCB_LAYER_ID pcbLayer = static_cast<PCB_LAYER_ID>( In1_Cu + ( inner - 1 ) * 2 );
        BOOST_CHECK_EQUAL( adapter.Is3dLayerEnabled( pcbLayer, adapter.GetVisibleLayers() ), inner % 2 == 0 );
    }

    cfg.m_LayerPresets.push_back( preset );
    cfg.Store();
    cfg.m_LayerPresets.clear();

    for( bool& visible : cfg.m_Render.show_copper_inner )
        visible = true;

    cfg.Load();

    BOOST_REQUIRE_EQUAL( cfg.m_LayerPresets.size(), 1u );
    BOOST_CHECK( cfg.m_LayerPresets[0].layers == preset.layers );

    for( int inner = 1; inner <= 62; ++inner )
        BOOST_CHECK_EQUAL( cfg.m_Render.show_copper_inner[inner - 1], inner % 2 == 0 );
}


BOOST_AUTO_TEST_SUITE_END()
