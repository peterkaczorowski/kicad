/* Copyright The KiCad Developers, see AUTHORS.txt. SPDX-License-Identifier: GPL-3.0-or-later */
#include <api/api_pcb_settings_utils.h>
#include <settings/json_settings_internals.h>
#include <cmath>
#include <set>
#include <stdexcept>
#include <limits>
#include <climits>
#include <gal/color4d.h>

namespace
{
using JSON = nlohmann::json;
void require( bool condition, const std::string& message )
{
    if( !condition ) throw std::invalid_argument( message );
}
void number( const JSON& value, const std::string& key, double maximum = 1000, double minimum = 0 )
{
    require( value.is_number() && std::isfinite( value.get<double>() )
             && value.get<double>() >= minimum && value.get<double>() <= maximum,
             key + ": expected a finite number in range" );
}
void dimensions( const JSON& value, const std::set<std::string>& keys, const std::string& path )
{
    require( value.is_array(), path + ": expected an array" );
    for( const auto& entry : value )
    {
        require( entry.is_object() && entry.size() == keys.size(), path + ": incomplete dimensions" );
        for( const auto& key : keys )
        {
            require( entry.contains( key ), path + ": missing " + key );
            number( entry.at( key ), path + "." + key );
        }
        if( entry.contains( "drill" ) )
            require( entry.at( "drill" ) <= entry.at( "diameter" ), "Via drill exceeds diameter" );
    }
}
}

void PCB_PROJECT_SETTINGS::Validate( const JSON& net, const JSON& board, const JSON& schema )
{
    require( net.is_object() && board.is_object(), "Settings must be objects" );
    const std::set<std::string> netKeys{ "meta", "classes", "net_colors", "netclass_assignments", "netclass_patterns" };
    for( const auto& entry : net.items() )
        require( netKeys.count( entry.key() ), "Unknown net setting: " + entry.key() );
    require( net.contains( "classes" ) && net.at( "classes" ).is_array(), "Net classes must be an array" );
    std::set<std::string> names;
    const std::set<std::string> textKeys{ "name", "pcb_color", "schematic_color", "tuning_profile" };
    const std::set<std::string> lengthKeys{ "clearance", "track_width", "via_diameter", "via_drill",
            "microvia_diameter", "microvia_drill", "diff_pair_width", "diff_pair_gap", "diff_pair_via_gap",
            "wire_width", "bus_width" };
    for( const auto& nc : net.at( "classes" ) )
    {
        require( nc.is_object() && nc.contains( "name" ) && nc.at( "name" ).is_string(), "Netclass needs a name" );
        const auto name = nc.at( "name" ).get<std::string>();
        require( !name.empty() && name.find( '\0' ) == std::string::npos && names.insert( name ).second,
                 "Empty or duplicate netclass name" );
        for( const auto& entry : nc.items() )
        {
            const auto& key = entry.key();
            if( textKeys.count( key ) )
            {
                require( entry.value().is_string(), key + ": expected text" );
                if( key == "pcb_color" || key == "schematic_color" )
                {
                    KIGFX::COLOR4D color;
                    require( color.SetFromWxString( wxString::FromUTF8( entry.value().get<std::string>() ) ), "Invalid color" );
                }
            }
            else if( lengthKeys.count( key ) )
                number( entry.value(), key );
            else if( key == "priority" || key == "line_style" )
            {
                number( entry.value(), key, key == "priority" ? INT_MAX : 4, key == "priority" ? -1 : 0 );
                require( std::floor( entry.value().get<double>() ) == entry.value().get<double>(), key + ": expected integer" );
            }
            else
                throw std::invalid_argument( "Unknown netclass property: " + key );
        }
        for( const auto& pair : { std::make_pair( "via_drill", "via_diameter" ),
                                 std::make_pair( "microvia_drill", "microvia_diameter" ) } )
            if( nc.contains( pair.first ) && nc.contains( pair.second ) )
                require( nc.at( pair.first ) <= nc.at( pair.second ), "Netclass via drill exceeds diameter" );
        if( name == "Default" )
            for( const std::string& key : { "clearance", "track_width", "via_diameter", "via_drill",
                    "microvia_diameter", "microvia_drill", "diff_pair_width", "diff_pair_gap", "diff_pair_via_gap" } )
                require( nc.contains( key ), "Default netclass is missing " + key );
    }
    require( names.count( "Default" ), "The Default netclass is required" );
    if( net.contains( "netclass_patterns" ) )
    {
        require( net.at( "netclass_patterns" ).is_array(), "Patterns must be an array" );
        for( const auto& pattern : net.at( "netclass_patterns" ) )
        {
            require( pattern.is_object() && pattern.size() == 2 && pattern.contains( "pattern" )
                     && pattern.at( "pattern" ).is_string() && !pattern.at( "pattern" ).get<std::string>().empty()
                     && pattern.contains( "netclass" ) && pattern.at( "netclass" ).is_string(), "Invalid netclass pattern" );
            require( names.count( pattern.at( "netclass" ).get<std::string>() ), "Pattern refers to an undefined netclass" );
        }
    }
    if( net.contains( "netclass_assignments" ) )
    {
        require( net.at( "netclass_assignments" ).is_object(), "Assignments must be an object" );
        for( const auto& assignment : net.at( "netclass_assignments" ).items() )
        {
            require( assignment.value().is_array(), "Assignment values must be arrays" );
            for( const auto& name : assignment.value() )
                require( name.is_string() && names.count( name.get<std::string>() ), "Assignment refers to an undefined netclass" );
        }
    }
    if( net.contains( "net_colors" ) )
    {
        require( net.at( "net_colors" ).is_object(), "Net colors must be an object" );
        for( const auto& color : net.at( "net_colors" ).items() )
        {
            require( color.value().is_string(), "Net colors must be strings" );
            KIGFX::COLOR4D parsed;
            require( parsed.SetFromWxString( wxString::FromUTF8( color.value().get<std::string>() ) ), "Invalid net color" );
        }
    }
    for( const auto& entry : board.items() )
    {
        const auto& key = entry.key();
        require( schema.contains( key ), "Unknown board design setting: " + key );
        if( key == "rules" )
        {
            require( entry.value().is_object(), "Rules must be an object" );
            for( const auto& rule : entry.value().items() )
            {
                require( schema.at( key ).contains( rule.key() ), "Unknown board constraint: " + rule.key() );
                if( schema.at( key ).at( rule.key() ).is_boolean() )
                    require( rule.value().is_boolean(), "Expected boolean constraint: " + rule.key() );
                else
                    number( rule.value(), rule.key(), 1000,
                            rule.key() == "min_copper_edge_clearance" ? -.01 :
                            rule.key() == "min_silk_clearance" ? -10 : 0 );
            }
        }
        else if( key == "rule_severities" )
        {
            require( entry.value().is_object(), "Severities must be an object" );
            for( const auto& severity : entry.value().items() )
                require( schema.at( key ).contains( severity.key() ) && severity.value().is_string()
                         && ( severity.value() == "error" || severity.value() == "warning" || severity.value() == "ignore" ),
                         "Unknown violation or invalid severity: " + severity.key() );
        }
        else if( key == "track_widths" )
        {
            require( entry.value().is_array(), "Track widths must be an array" );
            for( const auto& width : entry.value() ) number( width, key );
        }
        else if( key == "via_dimensions" )
            dimensions( entry.value(), { "diameter", "drill" }, key );
        else if( key == "diff_pair_dimensions" )
            dimensions( entry.value(), { "width", "gap", "via_gap" }, key );
        else
            require( entry.value() == schema.at( key ), "This operation does not edit board setting: " + key );
    }
}

nlohmann::json PCB_PROJECT_SETTINGS::Patch( const JSON& current, const JSON& supplied,
                                            const std::vector<std::string>& paths )
{
    require( !paths.empty(), "An explicit field mask is required" );
    JSON candidate = current;
    std::set<std::string> seen;
    for( const auto& path : paths )
    {
        require( !path.empty() && path.front() != '.' && path.back() != '.'
                 && path.find( ".." ) == std::string::npos && path.find( '/' ) == std::string::npos
                 && path.find( '~' ) == std::string::npos, "Invalid settings path" );
        require( path == "net_settings" || path.rfind( "net_settings.", 0 ) == 0
                 || path == "board_design_settings" || path.rfind( "board_design_settings.", 0 ) == 0
                 || path == "custom_rules", "Unknown settings field: " + path );
        for( const auto& previous : seen )
            require( path != previous && path.rfind( previous + ".", 0 ) != 0
                     && previous.rfind( path + ".", 0 ) != 0, "Overlapping settings paths" );
        seen.insert( path );
        const auto pointer = JSON_SETTINGS_INTERNALS::PointerFromString( path );
        require( supplied.contains( pointer ) && current.contains( pointer ), "Unknown or missing settings path: " + path );
        candidate[pointer] = supplied.at( pointer );
    }
    return candidate;
}
