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
 * @file mdw_framework.h
 * @brief The application-level owner of the Managed Design Workspace runtime.
 *
 * The framework is constructed once in the project-manager process, beside the
 * API server, so backend processes survive editor frame recreation. It owns the
 * backend registry, the credential store, and one session per bound project.
 */

#ifndef MDW_FRAMEWORK_H
#define MDW_FRAMEWORK_H

#include <kicommon.h>
#include "mdw_backend_registry.h"
#include "mdw_context_action_registry.h"
#include "mdw_credential_store.h"
#include "mdw_session.h"

#include <map>
#include <memory>
#include <mutex>

#include <wx/string.h>

class KICOMMON_API MDW_FRAMEWORK
{
public:
    MDW_FRAMEWORK();

    ~MDW_FRAMEWORK();

    MDW_BACKEND_REGISTRY& Registry() { return m_registry; }

    MDW_CREDENTIAL_STORE& CredentialStore() { return m_credentialStore; }

    MDW_CONTEXT_ACTION_REGISTRY& ContextActions() { return m_contextActions; }

    /**
     * Spawn a backend for a bound project and open its session. Blocking; must
     * run off the UI thread. Returns the session, or nullptr if the backend id
     * is unknown or the launch or handshake fails.
     */
    MDW_SESSION* OpenSession( const wxString& aProjectPath, const MDW_PROJECT_BINDING& aBinding,
                              const wxString& aKicadVersion, int aTimeoutMs );

    MDW_SESSION* Session( const wxString& aProjectPath ) const;

    void CloseSession( const wxString& aProjectPath );

    void Shutdown();

private:
    MDW_BACKEND_REGISTRY                              m_registry;
    MDW_CREDENTIAL_STORE                             m_credentialStore;
    MDW_CONTEXT_ACTION_REGISTRY                      m_contextActions;
    std::map<wxString, std::unique_ptr<MDW_SESSION>> m_sessions;
    mutable std::mutex                               m_mutex;
};

#endif // MDW_FRAMEWORK_H
