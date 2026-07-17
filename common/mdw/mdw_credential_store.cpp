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

#include "mdw_credential_store.h"

#include <kiplatform/secrets.h>


namespace
{

// Keychain service namespace keeping MDW secrets clear of other KiCad stores.
const wxString SERVICE_NAME = wxS( "kicad-mdw" );

// Well-known field under which the backend refresh token is filed.
const wxString REFRESH_TOKEN_FIELD = wxS( "refresh_token" );

// Transient key used only by the availability round trip.
const wxString PROBE_KEY = wxS( "__mdw_probe__" );


// The keychain is a flat namespace, so the workspace binding is folded into the
// key. Ids are opaque and case-sensitive, so they are never normalized.
wxString makeKey( const wxString& aBackendId, const wxString& aWorkspaceId, const wxString& aField )
{
    return aBackendId + wxS( ":" ) + aWorkspaceId + wxS( ":" ) + aField;
}

} // anonymous namespace


bool MDW_CREDENTIAL_STORE::IsAvailable() const
{
    const wxString probeValue = wxS( "1" );

    if( !KIPLATFORM::SECRETS::StoreSecret( SERVICE_NAME, PROBE_KEY, probeValue ) )
        return false;

    wxString readBack;
    bool     ok = KIPLATFORM::SECRETS::GetSecret( SERVICE_NAME, PROBE_KEY, readBack )
                  && readBack == probeValue;

    KIPLATFORM::SECRETS::DeleteSecret( SERVICE_NAME, PROBE_KEY );

    return ok;
}


wxString MDW_CREDENTIAL_STORE::GetBackendName() const
{
#if defined( __WXGTK__ )
    return wxS( "GNOME Keyring / KWallet (libsecret)" );
#elif defined( __WXMSW__ )
    return wxS( "Windows Credential Manager" );
#elif defined( __WXMAC__ )
    return wxS( "macOS Keychain" );
#else
    return wxS( "Platform Secrets" );
#endif
}


MDW_RESULT MDW_CREDENTIAL_STORE::StoreRefreshToken( const wxString& aBackendId,
                                                    const wxString& aWorkspaceId,
                                                    const wxString& aRefreshToken )
{
    return Store( aBackendId, aWorkspaceId, REFRESH_TOKEN_FIELD, aRefreshToken );
}


MDW_RESULT MDW_CREDENTIAL_STORE::LoadRefreshToken( const wxString& aBackendId,
                                                   const wxString& aWorkspaceId,
                                                   wxString&       aRefreshToken ) const
{
    return Load( aBackendId, aWorkspaceId, REFRESH_TOKEN_FIELD, aRefreshToken );
}


MDW_RESULT MDW_CREDENTIAL_STORE::EraseRefreshToken( const wxString& aBackendId,
                                                    const wxString& aWorkspaceId )
{
    return Erase( aBackendId, aWorkspaceId, REFRESH_TOKEN_FIELD );
}


bool MDW_CREDENTIAL_STORE::HasRefreshToken( const wxString& aBackendId,
                                            const wxString& aWorkspaceId ) const
{
    return Has( aBackendId, aWorkspaceId, REFRESH_TOKEN_FIELD );
}


MDW_RESULT MDW_CREDENTIAL_STORE::Store( const wxString& aBackendId, const wxString& aWorkspaceId,
                                        const wxString& aField, const wxString& aValue )
{
    if( aBackendId.IsEmpty() || aField.IsEmpty() )
    {
        return MDW_RESULT::Fail( MDW_ERROR_CODE::INVALID_PARAMETER,
                                 wxS( "Credential store requires a backend id and field name" ) );
    }

    if( KIPLATFORM::SECRETS::StoreSecret( SERVICE_NAME, makeKey( aBackendId, aWorkspaceId, aField ),
                                          aValue ) )
    {
        return MDW_RESULT::Ok();
    }

    return MDW_RESULT::Fail( MDW_ERROR_CODE::INTERNAL,
                             wxS( "Failed to write secret to the platform keychain" ) );
}


MDW_RESULT MDW_CREDENTIAL_STORE::Load( const wxString& aBackendId, const wxString& aWorkspaceId,
                                       const wxString& aField, wxString& aValue ) const
{
    if( aBackendId.IsEmpty() || aField.IsEmpty() )
    {
        return MDW_RESULT::Fail( MDW_ERROR_CODE::INVALID_PARAMETER,
                                 wxS( "Credential store requires a backend id and field name" ) );
    }

    wxString value;

    if( KIPLATFORM::SECRETS::GetSecret( SERVICE_NAME, makeKey( aBackendId, aWorkspaceId, aField ),
                                        value ) )
    {
        aValue = value;
        return MDW_RESULT::Ok();
    }

    return MDW_RESULT::Fail( MDW_ERROR_CODE::ITEM_NOT_FOUND,
                             wxS( "No stored credential for this workspace binding" ) );
}


MDW_RESULT MDW_CREDENTIAL_STORE::Erase( const wxString& aBackendId, const wxString& aWorkspaceId,
                                        const wxString& aField )
{
    if( aBackendId.IsEmpty() || aField.IsEmpty() )
    {
        return MDW_RESULT::Fail( MDW_ERROR_CODE::INVALID_PARAMETER,
                                 wxS( "Credential store requires a backend id and field name" ) );
    }

    // DeleteSecret is idempotent, so a missing entry still leaves the caller in
    // the intended erased state.
    KIPLATFORM::SECRETS::DeleteSecret( SERVICE_NAME, makeKey( aBackendId, aWorkspaceId, aField ) );

    return MDW_RESULT::Ok();
}


bool MDW_CREDENTIAL_STORE::Has( const wxString& aBackendId, const wxString& aWorkspaceId,
                                const wxString& aField ) const
{
    wxString value;

    return KIPLATFORM::SECRETS::GetSecret( SERVICE_NAME, makeKey( aBackendId, aWorkspaceId, aField ),
                                           value )
           && !value.IsEmpty();
}
