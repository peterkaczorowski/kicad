/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2021 CERN
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * @author Wayne Stambaugh <stambaughw@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
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

#include "spice_settings.h"
#include <settings/parameters.h>


const int spiceSettingsSchemaVersion = 0;


SPICE_SETTINGS::SPICE_SETTINGS( JSON_SETTINGS* aParent, const std::string& aPath ) :
        NESTED_SETTINGS( "simulator", spiceSettingsSchemaVersion, aParent, aPath ),
        m_fixIncludePaths( true )
{
    m_params.emplace_back( new PARAM<wxString>( "workbook_filename", &m_workbookFilename, "" ) );
    m_params.emplace_back( new PARAM<bool>( "fix_include_paths", &m_fixIncludePaths, true ) );
}


bool SPICE_SETTINGS::operator==( const SPICE_SETTINGS&aRhs ) const
{
    return m_workbookFilename == aRhs.m_workbookFilename
            && m_fixIncludePaths == aRhs.m_fixIncludePaths;
}


NGSPICE_SETTINGS::NGSPICE_SETTINGS( JSON_SETTINGS* aParent, const std::string& aPath ) :
        SPICE_SETTINGS( aParent, aPath ),
        m_compatibilityMode( NGSPICE_COMPATIBILITY_MODE::LT_PSPICE )
{
    m_params.emplace_back( new PARAM_ENUM<NGSPICE_COMPATIBILITY_MODE>( "model_mode",
            &m_compatibilityMode, NGSPICE_COMPATIBILITY_MODE::LT_PSPICE,
            NGSPICE_COMPATIBILITY_MODE::USER_CONFIG, NGSPICE_COMPATIBILITY_MODE::HSPICE ) );

    for( const std::string& option : SupportedOptions() )
        m_params.emplace_back( new PARAM<wxString>( "options." + option, &m_options[option], "" ) );
}


bool NGSPICE_SETTINGS::operator==( const SPICE_SETTINGS& aRhs ) const
{
    const NGSPICE_SETTINGS* settings = dynamic_cast<const NGSPICE_SETTINGS*>( &aRhs );

    wxCHECK( settings, false );

    return SPICE_SETTINGS::operator==( aRhs ) && m_compatibilityMode == settings->m_compatibilityMode
           && m_options == settings->m_options;
}


wxString NGSPICE_SETTINGS::GetOption( const std::string& aName ) const
{
    auto it = m_options.find( aName );

    return it != m_options.end() ? it->second : wxString();
}


void NGSPICE_SETTINGS::SetOption( const std::string& aName, const wxString& aValue )
{
    m_options[aName] = aValue;
}


wxString NGSPICE_SETTINGS::GetOptionsDirective() const
{
    wxString options;

    for( const std::string& name : SupportedOptions() )
    {
        wxString value = GetOption( name );
        value.Trim( true ).Trim( false );

        if( !value.IsEmpty() )
            options += wxString::Format( wxT( " %s=%s" ), wxString::FromUTF8( name ), value );
    }

    if( options.IsEmpty() )
        return wxEmptyString;

    return wxT( ".options" ) + options;
}


const std::vector<std::string>& NGSPICE_SETTINGS::SupportedOptions()
{
    static const std::vector<std::string> options = { "method", "reltol", "abstol", "vntol", "chgtol", "gmin", "gshunt",
                                                      "rshunt", "itl1",   "itl4",   "trtol", "temp",   "tnom" };

    return options;
}
