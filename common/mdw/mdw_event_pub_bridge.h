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
 * @file mdw_event_pub_bridge.h
 * @brief Republishes the internal event bus to external IPC clients.
 *
 * The bridge subscribes once to the in-process event bus and republishes each
 * event over the PUB socket as a serialized EventEnvelope, so every internal
 * event produces exactly one external notification. The topic is
 * workspace/<workspace_id>/<event_type>, letting a client subscribe by prefix.
 */

#ifndef MDW_EVENT_PUB_BRIDGE_H
#define MDW_EVENT_PUB_BRIDGE_H

#include <kicommon.h>
#include "mdw_events.h"

#include <atomic>
#include <cstdint>
#include <string>

class MDW_EVENT_BUS;
class KINNG_PUB_SERVER;

class KICOMMON_API MDW_EVENT_PUB_BRIDGE
{
public:
    MDW_EVENT_PUB_BRIDGE( MDW_EVENT_BUS* aBus, KINNG_PUB_SERVER* aPub );

    ~MDW_EVENT_PUB_BRIDGE();

    /// The topic an event is published under, exposed so a subscriber's filter
    /// can be built the same way.
    static std::string TopicFor( const MDW_EVENT& aEvent );

    /// The topic prefix covering every event for one workspace, so a client
    /// subscribes to workspace/<id>/ and receives all of its notifications.
    static std::string TopicPrefixFor( const wxString& aWorkspaceId );

    /// The number of events republished so far. A subscriber takes this as its
    /// resync baseline and detects a gap if the next envelope's seq skips ahead.
    std::uint64_t CurrentSeq() const { return m_seq.load(); }

private:
    void onEvent( const MDW_EVENT& aEvent );

    MDW_EVENT_BUS*             m_bus;
    KINNG_PUB_SERVER*          m_pub;
    std::uint64_t              m_subscription;
    std::atomic<std::uint64_t> m_seq;
};

#endif // MDW_EVENT_PUB_BRIDGE_H
