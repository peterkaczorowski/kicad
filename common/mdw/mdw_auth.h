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
 * @file mdw_auth.h
 * @brief Backend identity, version, credentials, and authentication result.
 *
 * KiCad brokers the platform keychain; a backend never touches it. Credentials
 * cross the local socket only inside an Authenticate call. A backend-minted
 * refresh token is returned here and persisted through MDW_CREDENTIAL_STORE.
 */

#ifndef MDW_AUTH_H
#define MDW_AUTH_H

#include "mdw_types.h"

#include <wx/string.h>
#include <map>

/**
 * Semantic version triple. Protocol and backend versions both use this. Major
 * must match for load; minor is additive-only.
 */
struct MDW_VERSION
{
    unsigned major = 0;
    unsigned minor = 0;
    unsigned patch = 0;
    wxString full;

    bool operator==( const MDW_VERSION& aOther ) const
    {
        return major == aOther.major && minor == aOther.minor && patch == aOther.patch;
    }

    bool operator<( const MDW_VERSION& aOther ) const
    {
        if( major != aOther.major )
            return major < aOther.major;

        if( minor != aOther.minor )
            return minor < aOther.minor;

        return patch < aOther.patch;
    }
};


/**
 * Identifying metadata a backend advertises at handshake.
 */
struct MDW_BACKEND_INFO
{
    wxString    backend_id;
    wxString    display_name;
    wxString    description;
    wxString    vendor;
    wxString    vendor_url;
    wxString    support_url;
    wxString    icon_name;
    MDW_VERSION version;
};


/**
 * Credentials passed to a backend for authentication. method names the scheme
 * ("password", "token", "oauth"); fields carries scheme-specific material.
 * refresh_token is a previously persisted backend token, if any.
 */
struct MDW_CREDENTIALS
{
    wxString                    method;
    std::map<wxString, wxString> fields;
    wxString                    refresh_token;
};


/**
 * Result of an authentication attempt. Inherited success means the user is
 * authenticated; a rejected credential reports success == false with error
 * MDW_ERROR_CODE::AUTH_FAILED. refresh_token, when set, is a new or rotated
 * token the framework persists in the platform keychain.
 */
struct MDW_AUTH_RESULT : public MDW_RESULT
{
    wxString user_id;
    wxString display_name;
    wxString refresh_token;
};


#endif // MDW_AUTH_H
