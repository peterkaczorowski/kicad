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

#include <boost/test/unit_test.hpp>

#include <X2_gerber_attributes.h>
#include <dialogs/dialog_map_gerber_layers_to_pcb.h>
#include <export_to_pcbnew.h>

#include <wx/filefn.h>
#include <wx/filename.h>

#include <array>
#include <fstream>
#include <iterator>
#include <string>


struct GERBER_LAYER_MAPPING_TEST_FIXTURE
{
    static PCB_LAYER_ID matchAltiumGerberExtension( const wxString& aFileName )
    {
        return DIALOG_MAP_GERBER_LAYERS_TO_PCB::matchAltiumGerberExtension( aFileName );
    }

    static PCB_LAYER_ID matchKiCadGerberFileName( const wxString& aFileName )
    {
        return DIALOG_MAP_GERBER_LAYERS_TO_PCB::matchKiCadGerberFileName( aFileName );
    }

    static PCB_LAYER_ID matchX2FileFunction( X2_ATTRIBUTE_FILEFUNCTION* aFileFunction )
    {
        return DIALOG_MAP_GERBER_LAYERS_TO_PCB::matchX2FileFunction( aFileFunction );
    }

    static bool writePcbHeader( const wxString& aPath, int aCopperLayers )
    {
        GBR_TO_PCB_EXPORTER exporter( nullptr, nullptr, aPath );
        exporter.m_fp = wxFopen( aPath, wxT( "wt" ) );

        if( !exporter.m_fp )
            return false;

        std::array<int, GERBER_DRAWLAYERS_COUNT> layerLookup;
        layerLookup.fill( UNDEFINED_LAYER );
        exporter.m_pcbCopperLayersCount = aCopperLayers;
        exporter.writePcbHeader( layerLookup.data() );

        bool closed = fclose( exporter.m_fp ) == 0;
        exporter.m_fp = nullptr;
        return closed;
    }
};


class TEMP_GERBVIEW_FILE
{
public:
    TEMP_GERBVIEW_FILE() :
            m_path( wxFileName::CreateTempFileName( wxS( "kicad_gerbview_layers_" ) ) )
    {
    }

    ~TEMP_GERBVIEW_FILE()
    {
        if( !m_path.IsEmpty() )
            wxRemoveFile( m_path );
    }

    const wxString& Path() const { return m_path; }

    std::string Read() const
    {
        std::ifstream input( m_path.ToStdString(), std::ios::binary );
        return std::string( ( std::istreambuf_iterator<char>( input ) ), std::istreambuf_iterator<char>() );
    }

private:
    wxString m_path;
};


BOOST_AUTO_TEST_SUITE( GerberLayerMapping )


BOOST_AUTO_TEST_CASE( HighCopperLayerMappings )
{
    char         command[] = ".FileFunction,Copper,L63,Inr*%";
    char*        text = command;
    int          lineNumber = 1;
    X2_ATTRIBUTE attribute;

    BOOST_REQUIRE( attribute.ParseAttribCmd( nullptr, command, sizeof( command ), text, lineNumber ) );
    BOOST_REQUIRE( attribute.IsFileFunction() );

    X2_ATTRIBUTE_FILEFUNCTION fileFunction( attribute );

    BOOST_CHECK_EQUAL( GERBER_LAYER_MAPPING_TEST_FIXTURE::matchX2FileFunction( &fileFunction ), In62_Cu );
    BOOST_CHECK_EQUAL( GERBER_LAYER_MAPPING_TEST_FIXTURE::matchKiCadGerberFileName( wxT( "board-In62_Cu.gbr" ) ),
                       In62_Cu );
    BOOST_CHECK_EQUAL( GERBER_LAYER_MAPPING_TEST_FIXTURE::matchAltiumGerberExtension( wxT( "board.g62" ) ), In62_Cu );
}


BOOST_AUTO_TEST_CASE( CopperLayerCountSelectsBoardFileVersion )
{
    TEMP_GERBVIEW_FILE output;

    // Literals, so a wrong exporter constant cannot pass
    std::string legacyVersion = "20240928";
    std::string expandedVersion = "20261031";

    BOOST_REQUIRE( GERBER_LAYER_MAPPING_TEST_FIXTURE::writePcbHeader( output.Path(), GBR_EXPORT_BOARD_FILE_MAX_CU ) );
    BOOST_CHECK_NE( output.Read().find( "(kicad_pcb (version " + legacyVersion + ")" ), std::string::npos );

    BOOST_REQUIRE(
            GERBER_LAYER_MAPPING_TEST_FIXTURE::writePcbHeader( output.Path(), GBR_EXPORT_BOARD_FILE_MAX_CU + 2 ) );
    std::string highLayerHeader = output.Read();

    BOOST_CHECK_NE( highLayerHeader.find( "(kicad_pcb (version " + expandedVersion + ")" ), std::string::npos );
    BOOST_CHECK_NE( highLayerHeader.find( "(66 In32.Cu signal)" ), std::string::npos );
}


BOOST_AUTO_TEST_SUITE_END()
