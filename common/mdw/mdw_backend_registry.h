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

/**
 * @file mdw_backend_registry.h
 * @brief Discovery of out-of-process MDW backend executables via manifest files.
 *
 * A backend ships a manifest named <name>.kicad-mdw.json beside its executable.
 * The registry scans the stock and user plugin trees, parses each manifest, and
 * exposes the valid ones so the framework can launch the matching backend.
 */

#ifndef MDW_BACKEND_REGISTRY_H
#define MDW_BACKEND_REGISTRY_H

#include <kicommon.h>
#include "mdw/mdw_capabilities.h"

#include <wx/string.h>

#include <set>
#include <vector>

/**
 * One parsed backend manifest. Only entries with valid == true reach the public
 * vector; the flag exists so parsing helpers can signal rejection in place.
 */
struct MDW_BACKEND_MANIFEST
{
    wxString                       backend_id;
    wxString                       display_name;
    wxString                       version;
    unsigned                       protocol_major = 0;
    unsigned                       protocol_minor = 0;
    wxString                       executable_path;   ///< Absolute, platform exe suffix appended
    std::vector<wxString>          schemes;
    std::set<MDW_CAPABILITY_GROUP> capability_hints;
    bool                           valid = false;
};


/**
 * Discovers backend manifests across the stock and user plugin directories.
 */
class KICOMMON_API MDW_BACKEND_REGISTRY
{
public:
    MDW_BACKEND_REGISTRY();

    /// Rescan the search directories, replacing the current manifest set.
    void Discover();

    const std::vector<MDW_BACKEND_MANIFEST>& Manifests() const { return m_manifests; }

    /// Return the manifest with the given id, or nullptr if none is registered.
    const MDW_BACKEND_MANIFEST* Find( const wxString& aBackendId ) const;

private:
    void scanDirectory( const wxString& aDir );

    std::vector<MDW_BACKEND_MANIFEST> m_manifests;
};

#endif // MDW_BACKEND_REGISTRY_H
