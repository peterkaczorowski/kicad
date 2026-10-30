/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2023 Roberto Fernandez Bautista <roberto.fer.bau@gmail.com>
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
 * @file test_cadstar_footprints.cpp
 * Test suite for import of cadstar *.cpa footprints and board files
 */

#include <pcbnew_utils/board_test_utils.h>
#include <pcbnew_utils/board_file_utils.h>
#include <qa_utils/wx_utils/unit_test_utils.h>

#include <pcbnew/pcb_io/cadstar/pcb_io_cadstar_archive.h>
#include <pcbnew/pcb_io/cadstar/cadstar_pcb_archive_parser.h>
#include <pcbnew/pcb_io/kicad_sexpr/pcb_io_kicad_sexpr.h>

#include <board.h>
#include <footprint.h>
#include <pad.h>
#include <pcb_track.h>
#include <zone.h>
#include <wx/filename.h>
#include <wx/log.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>


struct CADSTAR_IMPORT_FIXTURE
{
    CADSTAR_IMPORT_FIXTURE()
    {
        // Don't internally throw on font substitution warnings
        wxLog::SetTimestamp( wxEmptyString );
    }

    PCB_IO_CADSTAR_ARCHIVE cstarPlugin;
    PCB_IO_KICAD_SEXPR     kicadPlugin;
};


namespace
{
class TEMP_CADSTAR_FILE
{
public:
    explicit TEMP_CADSTAR_FILE( const wxString& aPrefix ) :
            m_path( std::string( wxFileName::CreateTempFileName( aPrefix ).ToUTF8() ) )
    {
    }

    ~TEMP_CADSTAR_FILE()
    {
        std::error_code ec;
        std::filesystem::remove( m_path, ec );
    }

    const std::filesystem::path& Path() const { return m_path; }
    wxString                     WxPath() const { return wxString::FromUTF8( m_path.string() ); }

private:
    std::filesystem::path m_path;
};


void replaceOnce( std::string& aText, const std::string& aNeedle, const std::string& aReplacement )
{
    size_t pos = aText.find( aNeedle );

    if( pos == std::string::npos )
        throw std::runtime_error( "CADSTAR QA source marker not found" );

    aText.replace( pos, aNeedle.size(), aReplacement );
}


void writeExpandedCadstarBoard( const std::filesystem::path& aSource, const std::filesystem::path& aDestination,
                                int aCopperLayerCount, bool aAddBuriedVia )
{
    std::ifstream     input( aSource );
    std::stringstream inputBuffer;
    inputBuffer << input.rdbuf();
    std::string archive = inputBuffer.str();

    if( archive.empty() )
        throw std::runtime_error( "Unable to read CADSTAR QA source archive" );

    size_t stackStart = archive.find( "   (LAYERSTACK " );
    size_t stackEnd = archive.find( "\n   )", stackStart );

    if( stackStart == std::string::npos || stackEnd == std::string::npos )
        throw std::runtime_error( "CADSTAR QA layer stack marker not found" );

    std::stringstream layerStack;
    layerStack << "   (LAYERSTACK LAY7 LAY14 LAY17 LAY18 LAY8 LAY12 LAY6";

    for( int physicalLayer = 2; physicalLayer < aCopperLayerCount; ++physicalLayer )
        layerStack << " LAY" << 100 + physicalLayer - 2;

    layerStack << " LAY21 LAY19 LAY20 LAY10 LAY16 LAY15 LAY5\n   )";
    archive.replace( stackStart, stackEnd + 5 - stackStart, layerStack.str() );

    std::stringstream innerLayers;

    for( int physicalLayer = 2; physicalLayer < aCopperLayerCount; ++physicalLayer )
    {
        int innerNumber = physicalLayer - 1;
        int layerId = 100 + physicalLayer - 2;
        innerLayers << "   (LAYER LAY" << layerId << " \"Inner " << innerNumber << "\"\n"
                    << "    (ELEC " << physicalLayer << "\n"
                    << "     (BIAS UNBIASED)\n"
                    << "     (MAKE MTL1 10000)\n"
                    << "    )\n"
                    << "   )\n";
    }

    size_t bottomLayer = archive.find( "   (LAYER LAY10 \"PP1\"" );

    if( bottomLayer == std::string::npos )
        throw std::runtime_error( "CADSTAR QA bottom layer marker not found" );

    archive.insert( bottomLayer, innerLayers.str() );
    replaceOnce( archive, "    (POWER 32", "    (POWER " + std::to_string( aCopperLayerCount ) );

    int         buriedStart = aCopperLayerCount - 2;
    int         buriedEnd = aCopperLayerCount - 1;
    std::string buriedLayerPair;

    if( aAddBuriedVia )
    {
        buriedLayerPair = "\n   (LAYERPAIR LP1 \"Buried near bottom\" " + std::to_string( buriedStart ) + " "
                          + std::to_string( buriedEnd )
                          + "\n"
                            "    (VIACODEREF VC0)\n"
                            "   )";
    }

    replaceOnce( archive,
                 "   (LAYERPAIR LP0 \"(Through Hole)\" 1 32\n"
                 "    (VIACODEREF VC0)\n"
                 "   )",
                 "   (LAYERPAIR LP0 \"(Through Hole)\" 1 " + std::to_string( aCopperLayerCount )
                         + "\n"
                           "    (VIACODEREF VC0)\n"
                           "   )"
                         + buriedLayerPair );
    replaceOnce( archive, "   (MAXPHYSLAYER 32)", "   (MAXPHYSLAYER " + std::to_string( aCopperLayerCount ) + ")" );

    const std::string bottomNonelectricalMarker = "(NONELEC 32)";
    size_t            bottomNonelectrical = 0;

    while( ( bottomNonelectrical = archive.find( bottomNonelectricalMarker, bottomNonelectrical ) )
           != std::string::npos )
    {
        archive.replace( bottomNonelectrical, bottomNonelectricalMarker.size(),
                         "(NONELEC " + std::to_string( aCopperLayerCount ) + ")" );
        ++bottomNonelectrical;
    }

    if( aAddBuriedVia )
    {
        replaceOnce( archive, "  (NET N0\n   (SIGNUM 11)",
                     "  (NET N0\n   (SIGNUM 11)\n"
                     "   (VIA V0 VC0 LP1\n"
                     "    (PT 9000000 7000000)\n"
                     "   )" );
    }

    std::ofstream output( aDestination, std::ios::binary | std::ios::trunc );
    output << archive;

    if( !output )
        throw std::runtime_error( "Unable to write expanded CADSTAR QA archive" );
}
} // namespace


BOOST_FIXTURE_TEST_SUITE( CadstarFootprintsImport, CADSTAR_IMPORT_FIXTURE )


/**
 * Compare all footprints with their KiCad reference footprint
 * TODO: Refactor this code so it can be made common to all importers!
 * (right now this is copy/paste from EAGLE)
 */
BOOST_AUTO_TEST_CASE( CadstarFootprintImport )
{
    std::vector<std::pair<wxString, wxString>> tests = {
        { "footprint-with-thermal-pad.cpa", "footprint-with-thermal-pad.pretty" }
    };

    std::string dataPath = KI_TEST::GetPcbnewTestDataDir() + "plugins/cadstar/lib/";

    for( const std::pair<wxString, wxString>& libName : tests )
    {
        wxString cstarLibraryPath = dataPath + libName.first;
        wxString kicadLibraryPath = dataPath + libName.second;

        wxArrayString cstarFootprintNames;
        wxArrayString kicadFootprintNames;

        BOOST_REQUIRE_NO_THROW(
            cstarPlugin.FootprintEnumerate( cstarFootprintNames, cstarLibraryPath, true, nullptr ) );
        BOOST_REQUIRE_NO_THROW(
            kicadPlugin.FootprintEnumerate( kicadFootprintNames, kicadLibraryPath, true, nullptr ) );

        BOOST_CHECK_EQUAL( cstarFootprintNames.GetCount(), kicadFootprintNames.GetCount() );

        for( size_t i = 0; i < cstarFootprintNames.GetCount(); i++ )
        {
            wxString footprintName = cstarFootprintNames[i];

            BOOST_TEST_CONTEXT( wxString::Format( wxT( "Import '%s' from '%s'" ),
                                                  footprintName,
                                                  libName.first ) )
            {
                std::unique_ptr<FOOTPRINT> eagleFp =
                        cstarPlugin.FootprintLoad( cstarLibraryPath, footprintName, false, nullptr );
                BOOST_CHECK( eagleFp );

                BOOST_CHECK_EQUAL( "REF**", eagleFp->GetReference() );
                BOOST_CHECK_EQUAL( footprintName, eagleFp->GetValue() );

                std::unique_ptr<FOOTPRINT> kicadFp =
                        kicadPlugin.FootprintLoad( kicadLibraryPath, footprintName, true, nullptr );
                BOOST_CHECK( kicadFp );

                KI_TEST::CheckFootprint( kicadFp.get(), eagleFp.get() );
            }
        }
    }
}


/**
 * Test that CADSTAR Revision 7 format files without ROUTEWIDTH nodes can be imported.
 * This tests the fix for GitLab issue #17783.
 */
BOOST_AUTO_TEST_CASE( CadstarRevision7FormatImport )
{
    std::string dataPath = KI_TEST::GetPcbnewTestDataDir() + "plugins/cadstar/route_offset/";
    wxString    filePath = dataPath + "revision7_format_no_routewidth.cpa";

    std::unique_ptr<BOARD> board;

    BOOST_CHECK_NO_THROW( board = cstarPlugin.LoadBoard( filePath ) );

    BOOST_REQUIRE( board != nullptr );

    // The test file has 5 nets with routes
    std::vector<PCB_TRACK*> tracks;

    for( PCB_TRACK* track : board->Tracks() )
        tracks.push_back( track );

    // Should have imported some tracks
    BOOST_CHECK( tracks.size() > 0 );

    // All nets use route code W1 (OptimalWidth 100000 hundredth-micron = 1.0 mm). Track widths
    // are derived from that route code since the routes have no explicit ROUTEWIDTH nodes.
    const int expectedWidth = pcbIUScale.mmToIU( 1.0 );
    bool      foundRouteCodeWidth = false;

    for( PCB_TRACK* track : tracks )
    {
        // No track should be left at zero width by the missing-ROUTEWIDTH fallback
        BOOST_CHECK( track->GetWidth() > 0 );

        if( track->GetWidth() == expectedWidth )
            foundRouteCodeWidth = true;
    }

    // At least one track must take the route-code-derived width, proving the fallback was used
    BOOST_CHECK( foundRouteCodeWidth );
}


/// Expand the Revision 7 board to 64 copper layers with a buried via on physical layers 62 and 63.
BOOST_AUTO_TEST_CASE( CadstarPhysicalLayerViaMappingBeyond32Layers )
{
    std::filesystem::path source = std::filesystem::path( KI_TEST::GetPcbnewTestDataDir() )
                                   / "plugins/cadstar/route_offset/revision7_format_no_routewidth.cpa";
    TEMP_CADSTAR_FILE expanded( wxT( "kicad_cadstar_64_layers_" ) );

    BOOST_REQUIRE_NO_THROW( writeExpandedCadstarBoard( source, expanded.Path(), 64, true ) );

    std::unique_ptr<BOARD> board;
    BOOST_REQUIRE_NO_THROW( board = cstarPlugin.LoadBoard( expanded.WxPath() ) );
    BOOST_REQUIRE( board );
    BOOST_CHECK_EQUAL( board->GetCopperLayerCount(), 64 );

    PCB_VIA* buriedVia = nullptr;
    size_t   viaCount = 0;

    for( PCB_TRACK* track : board->Tracks() )
    {
        if( PCB_VIA* via = dynamic_cast<PCB_VIA*>( track ) )
        {
            buriedVia = via;
            ++viaCount;
        }
    }

    BOOST_REQUIRE_EQUAL( viaCount, 1 );
    BOOST_REQUIRE( buriedVia );
    BOOST_CHECK_EQUAL( buriedVia->TopLayer(), In61_Cu );
    BOOST_CHECK_EQUAL( buriedVia->BottomLayer(), In62_Cu );
}


/// A CADSTAR stack above the maximum is rejected before the board is changed.
BOOST_AUTO_TEST_CASE( CadstarRejectsMoreThanMaximumCopperLayers )
{
    std::filesystem::path source = std::filesystem::path( KI_TEST::GetPcbnewTestDataDir() )
                                   / "plugins/cadstar/route_offset/revision7_format_no_routewidth.cpa";
    TEMP_CADSTAR_FILE      expanded( wxT( "kicad_cadstar_65_layers_" ) );
    std::unique_ptr<BOARD> board = std::make_unique<BOARD>();

    BOOST_REQUIRE_NO_THROW( writeExpandedCadstarBoard( source, expanded.Path(), 65, false ) );
    BOOST_CHECK_EXCEPTION( cstarPlugin.LoadAndAppendBoard( expanded.WxPath(), *board ), IO_ERROR,
                           []( const IO_ERROR& aError )
                           {
                               return aError.Problem().Contains( "defines 65 copper layers" );
                           } );
    BOOST_CHECK_EQUAL( board->GetCopperLayerCount(), 2 );
}


/**
 * A PADREASSIGN/VIAREASSIGN whose shape node is unknown (e.g. from a newer CADSTAR version)
 * must warn and continue rather than throwing, and must not report a (default-circle) shape so
 * the caller can skip the bogus reassignment instead of corrupting the pad geometry.
 */
BOOST_AUTO_TEST_CASE( UnknownReassignShapeIsSkipped )
{
    XNODE padReassign( wxXML_ELEMENT_NODE, wxT( "PADREASSIGN" ) );
    padReassign.AddAttribute( wxT( "attr0" ), wxT( "TOP" ) );
    padReassign.AddChild( new XNODE( wxXML_ELEMENT_NODE, wxT( "FUTURE_SHAPE" ) ) );

    CADSTAR_PCB_ARCHIVE_PARSER::PADREASSIGN          padParser;
    CADSTAR_PCB_ARCHIVE_PARSER::PARSER_CONTEXT       ctx;

    BOOST_CHECK_NO_THROW( padParser.Parse( &padReassign, &ctx ) );
    BOOST_CHECK( !padParser.HasShape );

    XNODE viaReassign( wxXML_ELEMENT_NODE, wxT( "VIAREASSIGN" ) );
    viaReassign.AddAttribute( wxT( "attr0" ), wxT( "TOP" ) );
    viaReassign.AddChild( new XNODE( wxXML_ELEMENT_NODE, wxT( "FUTURE_SHAPE" ) ) );

    CADSTAR_PCB_ARCHIVE_PARSER::VIAREASSIGN viaParser;

    BOOST_CHECK_NO_THROW( viaParser.Parse( &viaReassign, &ctx ) );
    BOOST_CHECK( !viaParser.HasShape );

    // An empty reassignment (no shape child at all) must not crash either
    XNODE emptyReassign( wxXML_ELEMENT_NODE, wxT( "PADREASSIGN" ) );
    emptyReassign.AddAttribute( wxT( "attr0" ), wxT( "TOP" ) );

    CADSTAR_PCB_ARCHIVE_PARSER::PADREASSIGN emptyParser;

    BOOST_CHECK_NO_THROW( emptyParser.Parse( &emptyReassign, &ctx ) );
    BOOST_CHECK( !emptyParser.HasShape );
}


BOOST_AUTO_TEST_SUITE_END()
