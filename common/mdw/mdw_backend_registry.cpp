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
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <mdw/mdw_backend_registry.h>

#include <paths.h>

#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/log.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <map>

#define MDW_TRACE wxT( "KICAD_MDW" )

namespace
{
const std::map<std::string, MDW_CAPABILITY_GROUP>& capabilityNames()
{
    static const std::map<std::string, MDW_CAPABILITY_GROUP> names = {
        { "LOCKING", MDW_CAPABILITY_GROUP::LOCKING },
        { "REVISIONS", MDW_CAPABILITY_GROUP::REVISIONS },
        { "ATTRIBUTES", MDW_CAPABILITY_GROUP::ATTRIBUTES },
        { "LIFECYCLE", MDW_CAPABILITY_GROUP::LIFECYCLE },
        { "EVENTS", MDW_CAPABILITY_GROUP::EVENTS },
        { "SEARCH", MDW_CAPABILITY_GROUP::SEARCH },
        { "DIFF", MDW_CAPABILITY_GROUP::DIFF },
        { "FORGE", MDW_CAPABILITY_GROUP::FORGE },
        { "SHARED_LIBRARIES", MDW_CAPABILITY_GROUP::SHARED_LIBRARIES },
        { "BOM", MDW_CAPABILITY_GROUP::BOM },
        { "ECO", MDW_CAPABILITY_GROUP::ECO },
        { "REVIEW", MDW_CAPABILITY_GROUP::REVIEW },
        { "PARTS", MDW_CAPABILITY_GROUP::PARTS },
        { "PERMISSIONS", MDW_CAPABILITY_GROUP::PERMISSIONS }
    };

    return names;
}


/**
 * Parse one manifest file into aManifest. Returns false and leaves valid == false
 * when required fields are missing; the caller is responsible for logging.
 */
bool parseManifest( const wxString& aFile, MDW_BACKEND_MANIFEST& aManifest )
{
    wxFileName jsonPath( aFile );
    std::ifstream stream( jsonPath.GetFullPath().fn_str() );

    if( !stream.is_open() )
        return false;

    nlohmann::json root = nlohmann::json::parse( stream );

    std::string id = root.value( "id", std::string() );
    std::string executable = root.value( "executable", std::string() );

    if( id.empty() || executable.empty() )
        return false;

    aManifest.backend_id = wxString::FromUTF8( id );
    aManifest.display_name = wxString::FromUTF8( root.value( "name", std::string() ) );
    aManifest.version = wxString::FromUTF8( root.value( "version", std::string() ) );

    if( root.contains( "protocol_version" ) && root["protocol_version"].is_object() )
    {
        const nlohmann::json& proto = root["protocol_version"];
        aManifest.protocol_major = proto.value( "major", 0u );
        aManifest.protocol_minor = proto.value( "minor", 0u );
    }

    wxFileName exePath( wxString::FromUTF8( executable ) );
    exePath.MakeAbsolute( jsonPath.GetPath() );

#ifdef __WXMSW__
    exePath.SetExt( wxT( "exe" ) );
#endif

    aManifest.executable_path = exePath.GetFullPath();

    if( root.contains( "schemes" ) && root["schemes"].is_array() )
    {
        for( const nlohmann::json& scheme : root["schemes"] )
        {
            if( scheme.is_string() )
                aManifest.schemes.emplace_back( wxString::FromUTF8( scheme.get<std::string>() ) );
        }
    }

    if( root.contains( "capability_hints" ) && root["capability_hints"].is_array() )
    {
        for( const nlohmann::json& hint : root["capability_hints"] )
        {
            if( !hint.is_string() )
                continue;

            auto it = capabilityNames().find( hint.get<std::string>() );

            if( it != capabilityNames().end() )
                aManifest.capability_hints.insert( it->second );
        }
    }

    aManifest.valid = true;
    return true;
}
} // namespace


MDW_BACKEND_REGISTRY::MDW_BACKEND_REGISTRY()
{
}


void MDW_BACKEND_REGISTRY::Discover()
{
    m_manifests.clear();

    scanDirectory( PATHS::GetStockPluginsPath() + wxT( "/mdw" ) );
    scanDirectory( PATHS::GetUserPluginsPath() + wxT( "/mdw" ) );

    // TODO scan the PCM-installed plugins tree once a stable accessor for it exists.
}


const MDW_BACKEND_MANIFEST* MDW_BACKEND_REGISTRY::Find( const wxString& aBackendId ) const
{
    for( const MDW_BACKEND_MANIFEST& manifest : m_manifests )
    {
        if( manifest.backend_id == aBackendId )
            return &manifest;
    }

    return nullptr;
}


void MDW_BACKEND_REGISTRY::scanDirectory( const wxString& aDir )
{
    if( !wxDir::Exists( aDir ) )
    {
        wxLogTrace( MDW_TRACE, wxT( "MDW registry skipping missing directory %s" ), aDir );
        return;
    }

    wxDir dir( aDir );

    if( !dir.IsOpened() )
    {
        wxLogTrace( MDW_TRACE, wxT( "MDW registry cannot open directory %s" ), aDir );
        return;
    }

    wxString filename;
    bool     more = dir.GetFirst( &filename, wxT( "*.kicad-mdw.json" ), wxDIR_FILES );

    while( more )
    {
        wxFileName manifestFile( aDir, filename );
        MDW_BACKEND_MANIFEST manifest;

        try
        {
            if( parseManifest( manifestFile.GetFullPath(), manifest ) && manifest.valid )
            {
                m_manifests.push_back( std::move( manifest ) );
            }
            else
            {
                wxLogTrace( MDW_TRACE, wxT( "MDW manifest %s missing required fields" ),
                            manifestFile.GetFullPath() );
            }
        }
        catch( const nlohmann::json::exception& e )
        {
            wxLogTrace( MDW_TRACE, wxT( "MDW manifest %s failed to parse: %s" ),
                        manifestFile.GetFullPath(), wxString::FromUTF8( e.what() ) );
        }

        more = dir.GetNext( &filename );
    }
}
