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
 * @file mdw_notification_bridge.h
 * @brief Turns workspace events by other users into desktop notifications.
 *
 * A user cares when someone else locks or checks in an item they share. The
 * bridge subscribes to a session event bus and, for events raised by a
 * different client, posts a notification through a supplied sink. Events this
 * client raised are ignored so a user is not notified of their own actions.
 */

#ifndef MDW_NOTIFICATION_BRIDGE_H
#define MDW_NOTIFICATION_BRIDGE_H

#include <kicommon.h>
#include <mdw/mdw_events.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

#include <wx/string.h>

class MDW_EVENT_BUS;


class KICOMMON_API MDW_NOTIFICATION_BRIDGE
{
public:
    /// Delivers one notification identified by a stable key so a later update
    /// replaces rather than stacks.
    using NOTIFY_FN =
            std::function<void( const wxString& aKey, const wxString& aTitle, const wxString& aBody )>;

    /**
     * Render an event as a notification. Returns false to skip, either because
     * the event is this client's own or because it carries no user-facing
     * message. Pure and static so the mapping is unit-testable.
     */
    static bool Describe( const MDW_EVENT& aEvent, const wxString& aSelfIdentity, wxString& aKey,
                          wxString& aTitle, wxString& aBody );

    MDW_NOTIFICATION_BRIDGE( MDW_EVENT_BUS* aBus, const wxString& aSelfIdentity, NOTIFY_FN aNotify );

    ~MDW_NOTIFICATION_BRIDGE();

private:
    void unsubscribe();

    MDW_EVENT_BUS* m_bus;
    std::uint64_t  m_subscription;
    wxString       m_selfIdentity;
    NOTIFY_FN      m_notify;

    // The handler may run on the bus transport thread, so it captures this token
    // by value and posts to the main loop rather than touching the bridge.
    std::shared_ptr<std::atomic<bool>> m_alive;
};

#endif // MDW_NOTIFICATION_BRIDGE_H
