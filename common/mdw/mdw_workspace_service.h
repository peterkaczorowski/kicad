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
 *
 * The backend pointer is captured at construction and assumed stable for the
 * service's life. A backend relaunch replaces the supervisor's backend, so the
 * session must discard and rebuild its service after a restart rather than let
 * this pointer dangle.
 *
 * Lock ownership is recorded from the caller's intent, not the reply, because
 * the protocol reports only is_locked and lock_owner, with no per-caller owned
 * flag and no user identity to compare against. Checking out an item the user
 * already locked in a prior session without re-requesting the lock therefore
 * records it as not owned locally until the next authoritative lock query.
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
#include "mdw_navigation.h"
#include "mdw_revision.h"
#include "mdw_schema.h"

#include <functional>
#include <vector>

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

    /*
     * Synchronous operations for the IPC handler and the dialogs. Each blocks on
     * the backend proxy until the call completes, so it must run off the UI
     * thread. Each capability-gates where noted, returns the framework error
     * code, and on success updates the trackers and emits the local event just
     * like the async operations above.
     */

    /// List the items in a folder. Mandatory surface; not gated.
    MDW_ERROR_CODE ListItems( const wxString& aFolderId, std::vector<MDW_ITEM_INFO>& aOut );

    /// Fetch one item's metadata. Mandatory surface; not gated.
    MDW_ERROR_CODE GetItemInfo( const wxString& aItemId, MDW_ITEM_INFO& aOut );

    /// The workspace root folders. Mandatory surface; not gated.
    MDW_ERROR_CODE GetRootFolders( std::vector<MDW_FOLDER>& aOut );

    /// The child folders of a folder. Mandatory surface; not gated.
    MDW_ERROR_CODE GetChildFolders( const wxString& aFolderId, std::vector<MDW_FOLDER>& aOut );

    /// Check out a working copy. On success sets the lock tracker when the item
    /// came back locked. Mandatory surface; not gated.
    MDW_ERROR_CODE Checkout( const MDW_CHECKOUT_REQUEST& aRequest, MDW_CHECKOUT_RESULT& aOut );

    /// Check in local modifications. Clears the dirty flag and emits
    /// REVISION_CHECKED_IN on success. Mandatory surface; not gated.
    MDW_ERROR_CODE CheckinSync( const MDW_CHECKIN_REQUEST& aRequest, MDW_CHECKIN_RESULT& aOut );

    /// Abandon a checkout. Clears the lock and dirty trackers on success.
    /// Mandatory surface; not gated.
    MDW_ERROR_CODE CancelCheckout( const wxString& aItemId, const wxString& aLocalPath );

    /// Take a lock. Gated on CG_LOCKING.
    MDW_ERROR_CODE LockSync( const wxString& aItemId, MDW_LOCK_INFO& aOut );

    /// Release a lock. Gated on CG_LOCKING.
    MDW_ERROR_CODE UnlockSync( const wxString& aItemId, MDW_LOCK_INFO& aOut );

    /// Read the last-known lock state. Gated on CG_LOCKING.
    MDW_ERROR_CODE GetLockInfo( const wxString& aItemId, MDW_LOCK_INFO& aOut );

    /// The revision history of an item, newest first. Gated on CG_REVISIONS.
    MDW_ERROR_CODE GetRevisionHistory( const wxString& aItemId, std::vector<MDW_REVISION>& aOut );

    /// Download one revision into a local path. Gated on CG_REVISIONS.
    MDW_ERROR_CODE DownloadRevision( const wxString& aItemId, const wxString& aRevisionId,
                                     const wxString& aLocalPath );

    /// Read the typed attribute values of an item. Gated on CG_ATTRIBUTES.
    MDW_ERROR_CODE GetAttributes( const wxString& aItemId, MDW_ATTRIBUTE_SET& aOut );

    /// Write typed attribute values on an item. Gated on CG_ATTRIBUTES.
    MDW_ERROR_CODE SetAttributes( const wxString& aItemId, const MDW_ATTRIBUTE_SET& aAttributes );

    /// The universal lifecycle category and backend custom-state id of an item.
    /// Gated on CG_LIFECYCLE.
    MDW_ERROR_CODE GetLifecycleCategory( const wxString& aItemId, MDW_LIFECYCLE_CATEGORY& aOutCategory,
                                         wxString& aOutCustomStateId );

    /// Transition an item to a backend state. Emits LIFECYCLE_CATEGORY_CHANGED on
    /// success and reports the resulting category and state id. Gated on
    /// CG_LIFECYCLE.
    MDW_ERROR_CODE TransitionLifecycle( const wxString& aItemId, const wxString& aTargetStateId,
                                        const wxString& aComment, MDW_LIFECYCLE_CATEGORY& aOutCategory,
                                        wxString& aOutStateId );

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
