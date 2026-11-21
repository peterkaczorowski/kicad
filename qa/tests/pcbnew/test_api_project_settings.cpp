/* Copyright The KiCad Developers, see AUTHORS.txt. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <api/api_pcb_settings_utils.h>
#include <board_design_settings.h>
#include <project/net_settings.h>
#include <settings/json_settings_internals.h>
#include <stdexcept>

using JSON = nlohmann::json;
BOOST_AUTO_TEST_SUITE( ProjectSettingsApi )
BOOST_AUTO_TEST_CASE( ValidationRefusesBrokenWholeTransactions )
{
    NET_SETTINGS net( nullptr, "net_settings" );
    BOARD_DESIGN_SETTINGS board( nullptr, "board.design_settings" );
    const auto valid = net.GetCurrentParams();
    const auto schema = board.GetCurrentParams();
    BOOST_CHECK_NO_THROW( PCB_PROJECT_SETTINGS::Validate( valid, schema, schema ) );
    for( int kind = 0; kind < 7; ++kind )
    {
        auto candidate = valid;
        auto design = schema;
        switch( kind )
        {
        case 0: candidate["classes"] = JSON::array(); break;
        case 1: candidate["classes"].push_back( candidate["classes"][0] ); break;
        case 2: candidate["classes"][0]["track_width"] = -1; break;
        case 3: candidate["netclass_patterns"] = JSON::array( { { { "pattern", "Net-(IC2-*" }, { "netclass", "Missing" } } } ); break;
        case 4: candidate["netclass_assignments"] = { { "GND", JSON::array( { "Missing" } ) } }; break;
        case 5: design["rules"]["invented"] = .1; break;
        case 6: design["rule_severities"]["clearance"] = "not-a-severity"; break;
        }
        BOOST_CHECK_THROW( PCB_PROJECT_SETTINGS::Validate( candidate, design, schema ), std::invalid_argument );
    }
    BOOST_CHECK( valid == net.GetCurrentParams() );
    BOOST_CHECK( schema == board.GetCurrentParams() );
}

BOOST_AUTO_TEST_CASE( MasksAreExplicitDisjointAndNeverMutateTheSource )
{
    const JSON current = { { "custom_rules", "" }, { "net_settings", { { "classes", JSON::array() } } },
            { "board_design_settings", { { "rules", { { "min_track_width", .2 } } } } } };
    const JSON supplied = { { "board_design_settings", { { "rules", { { "min_track_width", .127 } } } } } };
    const auto result = PCB_PROJECT_SETTINGS::Patch( current, supplied, { "board_design_settings.rules.min_track_width" } );
    BOOST_CHECK_EQUAL( result.at( "board_design_settings" ).at( "rules" ).at( "min_track_width" ).get<double>(), .127 );
    BOOST_CHECK_EQUAL( current.at( "board_design_settings" ).at( "rules" ).at( "min_track_width" ).get<double>(), .2 );
    for( const auto& paths : std::vector<std::vector<std::string>>{ {}, { "project" }, { "../custom_rules" },
            { "board_design_settings", "board_design_settings.rules" },
            { "board_design_settings.rules.min_track_width", "board_design_settings.rules.min_track_width" } } )
        BOOST_CHECK_THROW( PCB_PROJECT_SETTINGS::Patch( current, supplied, paths ), std::invalid_argument );
}

BOOST_AUTO_TEST_CASE( LiteralParenthesesUseNativeNetclassMatching )
{
    NET_SETTINGS net( nullptr, "net_settings" );
    auto video = std::make_shared<NETCLASS>( "VIDEO_CLASS", false );
    video->SetPriority( 0 );
    video->SetTrackWidth( 127000 );
    net.SetNetclass( "VIDEO_CLASS", video );
    net.SetNetclassPatternAssignment( "Net-(IC2-*", "VIDEO_CLASS" );
    BOOST_CHECK( net.GetEffectiveNetClass( "Net-(IC2-A)" )->ContainsNetclassWithName( "VIDEO_CLASS" ) );
    BOOST_CHECK_EQUAL( net.GetEffectiveNetClass( "Net-(IC2-A)" )->GetTrackWidth(), 127000 );
    BOOST_CHECK( !net.GetEffectiveNetClass( "Net-(IC3-A)" )->ContainsNetclassWithName( "VIDEO_CLASS" ) );
}

BOOST_AUTO_TEST_SUITE_END()
