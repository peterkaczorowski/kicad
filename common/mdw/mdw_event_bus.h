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
 * @file mdw_event_bus.h
 * @brief The in-process fan-out for workspace events.
 *
 * One bus is fed from two sources: local optimistic events the framework emits
 * the moment it performs an operation, and the authoritative stream the backend
 * pushes. The bus dedups so an operation is delivered exactly once: a backend
 * event that echoes a just-performed local operation is suppressed, and a
 * backend re-delivery of an already-seen sequence number is dropped.
 *
 * Handlers run on whatever thread calls a publish method. The backend ingest
 * path is expected to marshal to the main thread before publishing, and UI
 * subscribers must marshal any wx work to the main thread themselves. Unsubscribe
 * does not wait for a dispatch already in flight, so subscribe and unsubscribe
 * from the same thread that publishes.
 */

#ifndef MDW_EVENT_BUS_H
#define MDW_EVENT_BUS_H

#include <kicommon.h>
#include "mdw_events.h"

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <tuple>

class KICOMMON_API MDW_EVENT_BUS
{
public:
    using HANDLER = std::function<void( const MDW_EVENT& )>;

    MDW_EVENT_BUS() = default;

    /// Register a handler. Returns a token for Unsubscribe.
    std::uint64_t Subscribe( HANDLER aHandler );

    void Unsubscribe( std::uint64_t aToken );

    /**
     * Emit a local optimistic event now. When aExpectEcho is true the event is
     * remembered so the backend's echo of the same operation is suppressed once;
     * pass false when the backend does not advertise events, so no echo will ever
     * arrive and no fingerprint should linger.
     */
    void PublishLocal( const MDW_EVENT& aEvent, bool aExpectEcho = true );

    /// Ingest an event from the backend stream, deduped against a matching local
    /// event and against an already-seen sequence number for its workspace.
    void PublishFromBackend( const MDW_EVENT& aEvent );

private:
    using FINGERPRINT = std::tuple<int, wxString, wxString>;

    void dispatch( const MDW_EVENT& aEvent );

    static FINGERPRINT fingerprint( const MDW_EVENT& aEvent );

    std::mutex                        m_mutex;
    std::map<std::uint64_t, HANDLER>  m_handlers;
    std::uint64_t                     m_nextToken = 0;
    std::multiset<FINGERPRINT>        m_pendingEchoes;
    std::map<wxString, std::uint64_t> m_lastSeq;
};

#endif // MDW_EVENT_BUS_H
