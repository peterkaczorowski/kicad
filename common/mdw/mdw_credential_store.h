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
 * @file mdw_credential_store.h
 * @brief Platform-keychain persistence for MDW workspace credential material.
 *
 * KiCad is the sole broker of the platform keychain; a backend never touches it.
 * The framework persists the backend-minted refresh token and any credential
 * material it holds on the user's behalf here, keyed by the workspace binding
 * that owns it so distinct backends and workspaces never share a secret.
 */

#ifndef MDW_CREDENTIAL_STORE_H
#define MDW_CREDENTIAL_STORE_H

#include "mdw_types.h"

#include <wx/string.h>

/**
 * Stores MDW secrets in the operating system's native secure storage through
 * KIPLATFORM::SECRETS (libsecret/KWallet on Linux, Credential Manager on
 * Windows, Keychain on macOS).
 *
 * Every secret is addressed by a (backend id, workspace id, field) triple. The
 * refresh token is the primary payload and has dedicated helpers; the generic
 * Store/Load/Erase methods cover any additional credential material a backend
 * asks the framework to retain.
 */
class MDW_CREDENTIAL_STORE
{
public:
    MDW_CREDENTIAL_STORE() = default;
    ~MDW_CREDENTIAL_STORE() = default;

    MDW_CREDENTIAL_STORE( const MDW_CREDENTIAL_STORE& ) = delete;
    MDW_CREDENTIAL_STORE& operator=( const MDW_CREDENTIAL_STORE& ) = delete;

    /**
     * Probe the keychain with a self-cleaning round trip so callers can degrade
     * gracefully when secure storage is unreachable.
     *
     * @return True if a secret can be written, read back, and erased.
     */
    bool IsAvailable() const;

    /**
     * @return Human-readable name of the underlying keychain backend.
     */
    wxString GetBackendName() const;

    /**
     * Persist the backend-minted refresh token for a workspace binding,
     * replacing any token already stored for it.
     */
    MDW_RESULT StoreRefreshToken( const wxString& aBackendId, const wxString& aWorkspaceId,
                                  const wxString& aRefreshToken );

    /**
     * Retrieve the refresh token previously stored for a workspace binding.
     *
     * @param aRefreshToken Receives the token on success.
     */
    MDW_RESULT LoadRefreshToken( const wxString& aBackendId, const wxString& aWorkspaceId,
                                 wxString& aRefreshToken ) const;

    /**
     * Remove the refresh token stored for a workspace binding. Erasing an
     * absent token still succeeds.
     */
    MDW_RESULT EraseRefreshToken( const wxString& aBackendId, const wxString& aWorkspaceId );

    /**
     * @return True if a non-empty refresh token exists for the binding.
     */
    bool HasRefreshToken( const wxString& aBackendId, const wxString& aWorkspaceId ) const;

    /**
     * Persist an arbitrary named credential value for a workspace binding.
     *
     * @param aField Non-empty field name distinguishing this value within the
     *               binding.
     */
    MDW_RESULT Store( const wxString& aBackendId, const wxString& aWorkspaceId,
                      const wxString& aField, const wxString& aValue );

    /**
     * Retrieve a named credential value for a workspace binding.
     *
     * @param aValue Receives the stored value on success.
     */
    MDW_RESULT Load( const wxString& aBackendId, const wxString& aWorkspaceId,
                     const wxString& aField, wxString& aValue ) const;

    /**
     * Remove a named credential value for a workspace binding. Erasing an absent
     * value still succeeds.
     */
    MDW_RESULT Erase( const wxString& aBackendId, const wxString& aWorkspaceId,
                      const wxString& aField );

    /**
     * @return True if a non-empty value exists for the named field.
     */
    bool Has( const wxString& aBackendId, const wxString& aWorkspaceId,
              const wxString& aField ) const;
};

#endif // MDW_CREDENTIAL_STORE_H
