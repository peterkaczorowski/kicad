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
 * @file mdw_session.h
 * @brief One bound project's workspace session.
 *
 * A session ties a project path to its binding and the supervisor that owns the
 * spawned backend process. One process runs per (backend_id, project_path); a
 * second editor opening the same project spawns its own, indistinguishable from
 * a second user to the backend.
 */

#ifndef MDW_SESSION_H
#define MDW_SESSION_H

#include <kicommon.h>
#include "mdw_backend_supervisor.h"
#include "mdw_workspace_service.h"
#include "mdw_event_bus.h"
#include "mdw_lock_tracker.h"
#include "mdw_dirty_tracker.h"

#include <cstdint>
#include <memory>

#include <wx/string.h>

class KINNG_PUB_SERVER;
class MDW_CREDENTIAL_STORE;
class MDW_EVENT_PUB_BRIDGE;

/// The (workspace ...) binding stored in a project's .kicad_pro file.
struct MDW_PROJECT_BINDING
{
    wxString backend_id;
    wxString workspace_id;
    wxString binding;                                   ///< Opaque backend reconstitution blob

    bool IsBound() const { return !backend_id.IsEmpty(); }
};


class KICOMMON_API MDW_SESSION
{
public:
    MDW_SESSION( const wxString& aProjectPath, const MDW_PROJECT_BINDING& aBinding,
                 std::unique_ptr<MDW_BACKEND_SUPERVISOR> aSupervisor );

    ~MDW_SESSION();

    const wxString& ProjectPath() const { return m_projectPath; }

    const MDW_PROJECT_BINDING& Binding() const { return m_binding; }

    MDW_BACKEND_SUPERVISOR* Supervisor() const { return m_supervisor.get(); }

    MDW_BACKEND* Backend() const;

    bool Online() const;

    void SetAuthenticated( bool aAuthenticated ) { m_authenticated = aAuthenticated; }

    bool Authenticated() const { return m_authenticated; }

    /**
     * Authenticate this session's backend with the credentials KiCad holds for
     * its binding. A backend never touches the keychain, so the framework reads
     * the stored fields and sends them, along with any refresh token from a
     * previous session, inside the one call the protocol allows them on.
     *
     * A rotated token in the reply replaces the stored one. Returns true when the
     * backend reports the user authenticated; also true, without a call, when the
     * backend advertises no authentication requirement.
     *
     * Blocking; must not run on the UI thread.
     */
    bool Authenticate( MDW_CREDENTIAL_STORE& aStore, int aTimeoutMs );

    /**
     * The shared dispatch path for this session's workspace operations. Created
     * lazily the first time the backend is available and owned for the session's
     * lifetime so it outlives any in-flight call. Returns nullptr when the
     * backend never came up.
     */
    MDW_WORKSPACE_SERVICE* Service();

    MDW_EVENT_BUS& EventBus() { return m_eventBus; }

    MDW_LOCK_TRACKER& LockTracker() { return m_lockTracker; }

    MDW_DIRTY_TRACKER& DirtyTracker() { return m_dirtyTracker; }

    /**
     * Expose this session's event bus to external IPC clients over a PUB socket,
     * standing up the server and bus bridge on first use and reusing them after.
     * On success fills the socket URL, the workspace topic prefix and the current
     * resync sequence and returns true. Returns false when the backend runtime
     * directory is not available or the server cannot bind.
     *
     * Call on the dispatch (main) thread; the first-use construction is not
     * synchronized, matching the single-threaded access the rest of the session
     * assumes.
     */
    bool SubscribeEvents( wxString& aUrl, wxString& aTopicPrefix, std::uint64_t& aSeq );

private:
    wxString                                m_projectPath;
    MDW_PROJECT_BINDING                     m_binding;
    std::unique_ptr<MDW_BACKEND_SUPERVISOR> m_supervisor;
    bool                                    m_authenticated = false;

    MDW_EVENT_BUS                           m_eventBus;
    MDW_LOCK_TRACKER                        m_lockTracker;
    MDW_DIRTY_TRACKER                       m_dirtyTracker;

    // The bridge unsubscribes from the bus and stops touching the pub server on
    // destruction, so both must outlive it; declared before it for that reason.
    std::unique_ptr<KINNG_PUB_SERVER>       m_pubServer;
    std::unique_ptr<MDW_EVENT_PUB_BRIDGE>   m_pubBridge;

    std::unique_ptr<MDW_WORKSPACE_SERVICE>  m_service;
};

#endif // MDW_SESSION_H
