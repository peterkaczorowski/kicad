/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2026 KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file mdw_event_publisher.h
 * @brief The backend-side event channel client.
 *
 * A backend pushes workspace events to KiCad over a REQ socket that KiCad
 * answers with a per-message acknowledgement. Delivery is reliable rather than
 * lossy because lock and lifecycle events are authoritative tracker inputs. An
 * event that fails to send or is not acknowledged is retried; the framework
 * dedups duplicate delivery by sequence number.
 */

#ifndef MDW_EVENT_PUBLISHER_H
#define MDW_EVENT_PUBLISHER_H

#include <api/workspace/workspace_types.pb.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class MDW_EVENT_PUBLISHER
{
public:
    MDW_EVENT_PUBLISHER( const std::string& aChannelToken );

    ~MDW_EVENT_PUBLISHER();

    bool Start( const std::string& aEventsUrl );

    void Stop();

    bool Running() const;

    /// Queue an event for reliable delivery. Returns false if not running.
    bool Publish( const kiapi::workspace::EventEnvelope& aEvent );

private:
    void publishLoop();

    struct IMPL;

    std::string           m_channelToken;
    std::unique_ptr<IMPL> m_impl;
};

#endif // MDW_EVENT_PUBLISHER_H
