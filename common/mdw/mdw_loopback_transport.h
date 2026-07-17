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
 * @file mdw_loopback_transport.h
 * @brief An in-process transport that dispatches to a handler, for tests.
 *
 * Requests are queued and completed on a worker thread, so the async and
 * cancellation behaviour the proxy relies on is exercised without spawning a
 * process. An optional latency lets a test open a cancellation window.
 */

#ifndef MDW_LOOPBACK_TRANSPORT_H
#define MDW_LOOPBACK_TRANSPORT_H

#include <kicommon.h>

#include "mdw_transport.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <thread>

class KICOMMON_API MDW_LOOPBACK_TRANSPORT : public MDW_TRANSPORT
{
public:
    using DISPATCH = std::function<void( const kiapi::mdw::MdwRequest&, kiapi::mdw::MdwResponse& )>;

    MDW_LOOPBACK_TRANSPORT( DISPATCH aDispatch, int aLatencyMs = 0 );

    ~MDW_LOOPBACK_TRANSPORT() override;

    bool Start() override;

    void Stop() override;

    std::uint64_t SendAsync( const kiapi::mdw::MdwRequest& aRequest, int aTimeoutMs,
                             CALLBACK aOnComplete ) override;

    MDW_TRANSPORT_RESULT SendSync( const kiapi::mdw::MdwRequest& aRequest, int aTimeoutMs ) override;

    void Cancel( std::uint64_t aHandle ) override;

private:
    struct PENDING
    {
        std::uint64_t         id = 0;
        kiapi::mdw::MdwRequest request;
        CALLBACK              callback;
    };

    void workerLoop();

    DISPATCH                   m_dispatch;
    int                        m_latencyMs;
    std::atomic<bool>          m_running{ false };
    std::atomic<std::uint64_t> m_nextId{ 0 };
    std::mutex                 m_mutex;
    std::condition_variable    m_cv;
    std::deque<PENDING>        m_queue;
    std::set<std::uint64_t>    m_cancelled;
    std::thread                m_worker;
};

#endif // MDW_LOOPBACK_TRANSPORT_H
