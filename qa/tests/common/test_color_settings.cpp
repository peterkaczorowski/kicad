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
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

/**
 * @file test_color_settings.cpp
 * Tests for loading color themes via SETTINGS_MANAGER::GetColorSettings().
 */

#include <qa_utils/wx_utils/unit_test_utils.h>
#include <settings/color_settings.h>
#include <settings/settings_manager.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class COLOR_SETTINGS_TEST_FIXTURE
{
public:
    COLOR_SETTINGS_TEST_FIXTURE()
    {
        m_tempDir = fs::temp_directory_path() / "kicad_color_settings_test";
        fs::remove_all( m_tempDir );
        fs::create_directories( m_tempDir );

        fs::path      themePath = m_tempDir / "mytheme.json";
        std::ofstream themeFile( themePath );
        themeFile << R"({ "meta": { "name": "My Test Theme" } })";
        themeFile.close();

        m_themePathWithExt = wxString( themePath.string() );
        m_themePathWithoutExt = wxString( ( m_tempDir / "mytheme" ).string() );
        m_nonExistentPath = wxString( ( m_tempDir / "nosuchtheme" ).string() );
        m_nonExistentName = wxString( "nosuchthemename" );
    }

    ~COLOR_SETTINGS_TEST_FIXTURE() { fs::remove_all( m_tempDir ); }

    fs::path m_tempDir;
    wxString m_themePathWithExt;
    wxString m_themePathWithoutExt;
    wxString m_nonExistentPath;
    wxString m_nonExistentName;
};


BOOST_FIXTURE_TEST_SUITE( ColorSettings, COLOR_SETTINGS_TEST_FIXTURE )


/**
 * A theme file can be loaded by passing the full path including the .json extension.
 */
BOOST_AUTO_TEST_CASE( LoadByAbsolutePathWithExtension )
{
    SETTINGS_MANAGER manager;

    COLOR_SETTINGS* settings = manager.GetColorSettings( m_themePathWithExt );

    BOOST_REQUIRE( settings != nullptr );
    BOOST_CHECK_EQUAL( settings->GetName(), wxS( "My Test Theme" ) );
    BOOST_CHECK( settings->IsReadOnly() );
}


/**
 * A theme file can be loaded by passing the full path without the .json extension.
 */
BOOST_AUTO_TEST_CASE( LoadByAbsolutePathWithoutExtension )
{
    SETTINGS_MANAGER manager;

    COLOR_SETTINGS* settings = manager.GetColorSettings( m_themePathWithoutExt );

    BOOST_REQUIRE( settings != nullptr );
    BOOST_CHECK_EQUAL( settings->GetName(), wxS( "My Test Theme" ) );
    BOOST_CHECK( settings->IsReadOnly() );
}


/**
 * Given a non-existent path or non-existent theme name, we fall back to an
 * in-memory copy of the built-in default theme, i.e., GetColorSettings()
 * guarantees to return a valid pointer.
 */
BOOST_AUTO_TEST_CASE( NonExistentPathFallsBackToDefault )
{
    SETTINGS_MANAGER manager;

    COLOR_SETTINGS* settings = manager.GetColorSettings( m_nonExistentPath );

    BOOST_REQUIRE( settings != nullptr );
    // fallback copies built-in default in memory, which is safe to modify
    BOOST_CHECK( !settings->IsReadOnly() );
    BOOST_CHECK_EQUAL( settings->GetFilename(), DEFAULT_THEME );

    settings = manager.GetColorSettings( m_nonExistentName );
    BOOST_REQUIRE( settings != nullptr );
    BOOST_CHECK( !settings->IsReadOnly() );
    BOOST_CHECK_EQUAL( settings->GetFilename(), DEFAULT_THEME );
}


BOOST_AUTO_TEST_SUITE_END()
