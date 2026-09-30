/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or
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
#include <pcbnew_utils/board_file_utils.h>

#include <pcbnew/pcb_io/pcad/pcb_io_pcad.h>

#include <board.h>
#include <board_item.h>

#include <fmt/format.h>

#include <wx/filename.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>


namespace
{

class TEMP_PCAD_FILE
{
public:
    explicit TEMP_PCAD_FILE( const wxString& aPrefix ) :
            m_path( std::string( wxFileName::CreateTempFileName( aPrefix ).ToUTF8() ) )
    {
    }

    ~TEMP_PCAD_FILE()
    {
        std::error_code ec;
        std::filesystem::remove( m_path, ec );
    }

    const std::filesystem::path& Path() const { return m_path; }
    wxString                     WxPath() const { return wxString::FromUTF8( m_path.string() ); }

private:
    std::filesystem::path m_path;
};


void writePcadBoardWithSignalLayerCount( const std::filesystem::path& aDestination, int aLayerCount )
{
    const std::filesystem::path sourcePath =
            std::filesystem::path( KI_TEST::GetPcbnewTestDataDir() ) / "plugins/pcad/pcad_4layer_glyph_test_ascii.PCB";
    std::ifstream source( sourcePath, std::ios::binary );

    if( !source )
        throw std::runtime_error( "Unable to read P-CAD QA source board" );

    std::string              contents{ std::istreambuf_iterator<char>( source ), std::istreambuf_iterator<char>() };
    static const std::string marker = "  (multiLayer ";
    size_t                   markerPos = contents.find( marker );

    if( markerPos == std::string::npos )
        throw std::runtime_error( "P-CAD QA source marker not found" );

    // Add signal layers next to the fixture's Top, In1, In2 and Bottom definitions
    std::string extraLayers;

    for( int i = 0; i < aLayerCount - 4; ++i )
    {
        int layerNumber = 32 + i;
        extraLayers += fmt::format( "  (layerDef \"QA Inner {}\"\n"
                                    "    (layerNum {})\n"
                                    "    (layerType Signal)\n"
                                    "    (fieldSetRef \"(Default)\")\n"
                                    "  )\n",
                                    i + 1, layerNumber );
    }

    contents.insert( markerPos, extraLayers );

    std::ofstream output( aDestination, std::ios::binary | std::ios::trunc );
    output.write( contents.data(), static_cast<std::streamsize>( contents.size() ) );

    if( !output )
        throw std::runtime_error( "Unable to write expanded P-CAD QA board" );
}

} // namespace


BOOST_AUTO_TEST_SUITE( PcadImport )


/// A 33 signal layer board imports with 34 copper layers and keeps Bottom on B_Cu.
BOOST_AUTO_TEST_CASE( OddCopperLayerCountRoundsUp )
{
    TEMP_PCAD_FILE expanded( wxT( "kicad_qa_pcad_layers_" ) );
    BOOST_REQUIRE_NO_THROW( writePcadBoardWithSignalLayerCount( expanded.Path(), 33 ) );

    PCB_IO_PCAD            plugin;
    std::unique_ptr<BOARD> board;
    BOOST_REQUIRE_NO_THROW( board = plugin.LoadBoard( expanded.WxPath() ) );

    BOOST_CHECK_EQUAL( board->GetCopperLayerCount(), 34 );

    size_t bottomDrawingCount = 0;

    for( BOARD_ITEM* item : board->Drawings() )
    {
        if( item->GetLayer() == B_Cu )
            bottomDrawingCount++;
    }

    // The real fixture imports four drawing objects on P-CAD layer 2 (Bottom).
    BOOST_CHECK_EQUAL( bottomDrawingCount, 4 );
}


BOOST_AUTO_TEST_SUITE_END()
