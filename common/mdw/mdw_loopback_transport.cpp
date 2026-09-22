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

#include <mdw/mdw_loopback_transport.h>

#include <chrono>
#include <future>


MDW_LOOPBACK_TRANSPORT::MDW_LOOPBACK_TRANSPORT( DISPATCH aDispatch, int aLatencyMs ) :
        m_dispatch( std::move( aDispatch ) ),
        m_latencyMs( aLatencyMs )
{
}


MDW_LOOPBACK_TRANSPORT::~MDW_LOOPBACK_TRANSPORT()
{
    Stop();
}


bool MDW_LOOPBACK_TRANSPORT::Start()
{
    if( m_running.load() )
        return true;

    m_running.store( true );
    m_worker = std::thread( [this]() { workerLoop(); } );

    return true;
}


void MDW_LOOPBACK_TRANSPORT::Stop()
{
    if( !m_running.exchange( false ) )
        return;

    m_cv.notify_all();

    if( m_worker.joinable() )
        m_worker.join();
}


std::uint64_t MDW_LOOPBACK_TRANSPORT::SendAsync( const kiapi::mdw::MdwRequest& aRequest, int,
                                                 CALLBACK aOnComplete )
{
    PENDING pending;
    pending.request = aRequest;
    pending.callback = std::move( aOnComplete );

    // Re-check running under the lock so a request never enqueues after the
    // worker has drained and exited, which would strand its callback and hang a
    // SendSync future forever.
    std::uint64_t id = 0;

    {
        std::lock_guard<std::mutex> lock( m_mutex );

        if( m_running.load() )
        {
            id = ++m_nextId;
            pending.id = id;
            m_queue.push_back( std::move( pending ) );
        }
    }

    if( id == 0 )
        return 0;

    m_cv.notify_all();

    return id;
}


MDW_TRANSPORT_RESULT MDW_LOOPBACK_TRANSPORT::SendSync( const kiapi::mdw::MdwRequest& aRequest,
                                                      int aTimeoutMs )
{
    std::promise<MDW_TRANSPORT_RESULT> promise;
    std::future<MDW_TRANSPORT_RESULT>  future = promise.get_future();

    std::uint64_t id = SendAsync( aRequest, aTimeoutMs,
                                  [&promise]( const MDW_TRANSPORT_RESULT& aResult )
                                  { promise.set_value( aResult ); } );

    if( id == 0 )
    {
        MDW_TRANSPORT_RESULT result;
        result.status = MDW_TRANSPORT_STATUS::BACKEND_LOST;
        return result;
    }

    return future.get();
}


void MDW_LOOPBACK_TRANSPORT::Cancel( std::uint64_t aHandle )
{
    std::lock_guard<std::mutex> lock( m_mutex );
    m_cancelled.insert( aHandle );
}


void MDW_LOOPBACK_TRANSPORT::workerLoop()
{
    while( true )
    {
        PENDING pending;

        {
            std::unique_lock<std::mutex> lock( m_mutex );
            m_cv.wait( lock, [this]() { return !m_running.load() || !m_queue.empty(); } );

            if( m_queue.empty() )
            {
                if( !m_running.load() )
                    return;

                continue;
            }

            pending = std::move( m_queue.front() );
            m_queue.pop_front();
        }

        if( m_latencyMs > 0 )
            std::this_thread::sleep_for( std::chrono::milliseconds( m_latencyMs ) );

        MDW_TRANSPORT_RESULT result;

        bool cancelled = false;

        {
            std::lock_guard<std::mutex> lock( m_mutex );
            cancelled = m_cancelled.erase( pending.id ) > 0;
        }

        if( cancelled || !m_running.load() )
        {
            result.status = MDW_TRANSPORT_STATUS::CANCELLED;
        }
        else
        {
            m_dispatch( pending.request, result.response );
            result.status = MDW_TRANSPORT_STATUS::OK;
        }

        if( pending.callback )
            pending.callback( result );
    }
}
