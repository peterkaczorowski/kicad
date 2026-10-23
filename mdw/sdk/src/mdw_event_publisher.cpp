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

#include <mdw_sdk/mdw_event_publisher.h>

#include <api/mdw/mdw_envelope.pb.h>

#include <nng/nng.h>
#include <nng/protocol/reqrep0/req.h>

#include <chrono>

namespace
{
/// Free whatever message an aio still holds and clear the reference. The aio is
/// reused across attempts, so a stale pointer must not outlive the message.
void releaseMsg( nng_aio* aAio )
{
    if( nng_msg* msg = nng_aio_get_msg( aAio ) )
        nng_msg_free( msg );

    nng_aio_set_msg( aAio, nullptr );
}
}


struct MDW_EVENT_PUBLISHER::IMPL
{
    nng_socket                                    socket = NNG_SOCKET_INITIALIZER;
    std::atomic<bool>                             running{ false };
    std::mutex                                    mutex;
    std::condition_variable                       cv;
    std::deque<kiapi::workspace::EventEnvelope>   queue;
    std::thread                                   worker;
};


MDW_EVENT_PUBLISHER::MDW_EVENT_PUBLISHER( const std::string& aChannelToken ) :
        m_channelToken( aChannelToken ),
        m_impl( std::make_unique<IMPL>() )
{
}


MDW_EVENT_PUBLISHER::~MDW_EVENT_PUBLISHER()
{
    Stop();
}


bool MDW_EVENT_PUBLISHER::Running() const
{
    return m_impl->running.load();
}


bool MDW_EVENT_PUBLISHER::Start( const std::string& aEventsUrl )
{
    if( m_impl->running.load() )
        return true;

    if( nng_req0_open( &m_impl->socket ) != 0 )
        return false;

    // Non-blocking dial tolerates KiCad's event server binding slightly later.
    if( nng_dial( m_impl->socket, aEventsUrl.c_str(), nullptr, NNG_FLAG_NONBLOCK ) != 0 )
    {
        nng_close( m_impl->socket );
        return false;
    }

    m_impl->running.store( true );
    m_impl->worker = std::thread( [this]() { publishLoop(); } );

    return true;
}


void MDW_EVENT_PUBLISHER::Stop()
{
    if( !m_impl->running.exchange( false ) )
        return;

    // Close the socket before joining so a worker blocked in nng_aio_wait aborts
    // at once rather than waiting out the per-message timeout.
    nng_close( m_impl->socket );
    m_impl->cv.notify_all();

    if( m_impl->worker.joinable() )
        m_impl->worker.join();
}


bool MDW_EVENT_PUBLISHER::Publish( const kiapi::workspace::EventEnvelope& aEvent )
{
    if( !m_impl->running.load() )
        return false;

    {
        std::lock_guard<std::mutex> lock( m_impl->mutex );
        m_impl->queue.push_back( aEvent );
    }

    m_impl->cv.notify_all();

    return true;
}


void MDW_EVENT_PUBLISHER::publishLoop()
{
    // One aio serves the whole thread; nng reuses it freely once nng_aio_wait
    // has returned, and allocating per attempt would churn on every retry.
    nng_aio* aio = nullptr;

    if( nng_aio_alloc( &aio, nullptr, nullptr ) != 0 )
        return;

    while( true )
    {
        kiapi::workspace::EventEnvelope event;

        {
            std::unique_lock<std::mutex> lock( m_impl->mutex );
            m_impl->cv.wait( lock, [this]()
                             { return !m_impl->running.load() || !m_impl->queue.empty(); } );

            if( !m_impl->running.load() && m_impl->queue.empty() )
                break;

            if( m_impl->queue.empty() )
                continue;

            event = m_impl->queue.front();
            m_impl->queue.pop_front();
        }

        std::string out;

        try
        {
            kiapi::mdw::PublishEvent publish;
            publish.set_channel_token( m_channelToken );
            *publish.mutable_event() = event;
            publish.SerializeToString( &out );
        }
        catch( ... )
        {
            // Drop an event we cannot serialize rather than crash the backend.
            continue;
        }

        bool delivered = false;

        // Retry the event until acknowledged or shutdown; the framework dedups
        // any duplicate delivery by sequence number.
        while( m_impl->running.load() && !delivered )
        {
            nng_msg* msg = nullptr;

            if( nng_msg_alloc( &msg, 0 ) != 0 )
                break;

            nng_msg_append( msg, out.data(), out.size() );

            nng_aio_set_timeout( aio, 2000 );
            nng_aio_set_msg( aio, msg );
            nng_send_aio( m_impl->socket, aio );
            nng_aio_wait( aio );

            // A failed send hands the message back; a successful exchange leaves
            // the acknowledgement. Either way the message is ours to release.
            if( nng_aio_result( aio ) != 0 )
            {
                releaseMsg( aio );
            }
            else
            {
                nng_recv_aio( m_impl->socket, aio );
                nng_aio_wait( aio );

                if( nng_aio_result( aio ) == 0 )
                {
                    releaseMsg( aio );
                    delivered = true;
                }
            }

            if( !delivered && m_impl->running.load() )
                std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
        }
    }

    nng_aio_free( aio );
}
