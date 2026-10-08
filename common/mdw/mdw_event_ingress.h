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
 * @file mdw_event_ingress.h
 * @brief Receives a backend's pushed workspace events and puts them on the bus.
 *
 * A backend does not publish events lossily; it pushes each one over a REQ
 * socket and waits for KiCad to acknowledge it, because lock and lifecycle
 * events are authoritative tracker inputs rather than hints. This is the other
 * end of that channel: a REP server on the URL the supervisor handed the backend
 * at spawn, answering every message and handing the payload to the event bus.
 *
 * Messages are delivered on the server's own thread, so the bus dispatches off
 * the main thread from here. Every UI subscriber must marshal its own work, and
 * the bus must outlive this object.
 */

#ifndef MDW_EVENT_INGRESS_H
#define MDW_EVENT_INGRESS_H

#include <kicommon.h>

#include <memory>
#include <string>

#include <wx/string.h>

class KINNG_REQUEST_SERVER;
class MDW_EVENT_BUS;


class KICOMMON_API MDW_EVENT_INGRESS
{
public:
    /**
     * Bind aUrl and forward acknowledged events to aBus. aChannelToken is the
     * per-session secret the supervisor gave the backend; a message that does not
     * carry it is acknowledged and dropped, so a stray sender cannot inject
     * workspace state.
     */
    MDW_EVENT_INGRESS( MDW_EVENT_BUS* aBus, const wxString& aChannelToken );

    ~MDW_EVENT_INGRESS();

    /// Bind the socket. Returns false when the address is unavailable.
    bool Start( const wxString& aUrl );

    void Stop();

    bool Running() const;

private:
    void onMessage( std::string* aMessage );

    MDW_EVENT_BUS*                       m_bus;
    std::string                          m_channelToken;
    std::unique_ptr<KINNG_REQUEST_SERVER> m_server;
};

#endif // MDW_EVENT_INGRESS_H
