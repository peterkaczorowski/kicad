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
 * @file mdw_workspace_service.h
 * @brief The single dispatch path shared by the dialogs and the IPC handler.
 *
 * Every workspace operation flows through here so capability gating, tracker
 * updates, event emission and client identity are applied in exactly one place.
 * The service gates each operation on the advertised capability, calls the
 * backend proxy, and on success updates the lock and dirty trackers and emits a
 * local event so the pill and other frames react before the backend echoes it.
 *
 * Operation completions run on the transport thread, so the service is a
 * long-lived framework component (owned by the session, not a dialog) and must
 * outlive any in-flight call. The trackers are mutex-guarded; UI subscribers to
 * the event bus marshal their own work to the main thread.
 */

#ifndef MDW_WORKSPACE_SERVICE_H
#define MDW_WORKSPACE_SERVICE_H

#include <kicommon.h>
#include "mdw_backend.h"
#include "mdw_event_bus.h"
#include "mdw_lock_tracker.h"
#include "mdw_dirty_tracker.h"
#include "mdw_item.h"
#include "mdw_lock.h"

#include <functional>

#include <wx/string.h>

class KICOMMON_API MDW_WORKSPACE_SERVICE
{
public:
    MDW_WORKSPACE_SERVICE( MDW_BACKEND* aBackend, MDW_EVENT_BUS* aBus, MDW_LOCK_TRACKER* aLocks,
                           MDW_DIRTY_TRACKER* aDirty, const wxString& aWorkspaceId,
                           const wxString& aClientIdentity );

    /// True when the bound backend advertises the group an operation needs.
    bool Supports( MDW_CAPABILITY_GROUP aGroup ) const;

    void Lock( const wxString& aItemId,
               std::function<void( MDW_ERROR_CODE, const MDW_LOCK_INFO& )> aOnComplete );

    void Unlock( const wxString& aItemId,
                 std::function<void( MDW_ERROR_CODE, const MDW_LOCK_INFO& )> aOnComplete );

    void Checkin( const MDW_CHECKIN_REQUEST& aRequest,
                  std::function<void( MDW_ERROR_CODE, const MDW_CHECKIN_RESULT& )> aOnComplete );

private:
    /// Emit a local event and update the lock tracker for a lock change.
    void applyLock( const wxString& aItemId, const MDW_LOCK_INFO& aLock, MDW_EVENT_TYPE aType );

    MDW_BACKEND*       m_backend;
    MDW_EVENT_BUS*     m_bus;
    MDW_LOCK_TRACKER*  m_locks;
    MDW_DIRTY_TRACKER* m_dirty;
    wxString           m_workspaceId;
    wxString           m_clientIdentity;
};

#endif // MDW_WORKSPACE_SERVICE_H
