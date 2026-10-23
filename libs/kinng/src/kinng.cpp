/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2023 Jon Evans <jon@craftyjon.com>
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
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

#include <kinng.h>
#include <nng/nng.h>
#include <nng/protocol/reqrep0/rep.h>
#include <nng/protocol/reqrep0/req.h>
#include <nng/protocol/pubsub0/pub.h>
#include <wx/log.h>

#include <future>
#include <utility>


/**
 * Trace nng server debug output
 * @ingroup trace_env_vars
 */
static const wxChar TraceNng[] = wxT( "KINNG" );


KINNG_REQUEST_SERVER::KINNG_REQUEST_SERVER( const std::string& aSocketUrl ) :
        m_socketUrl( aSocketUrl ),
        m_callback()
{
}


KINNG_REQUEST_SERVER::~KINNG_REQUEST_SERVER()
{
    Stop();
}


bool KINNG_REQUEST_SERVER::Running() const
{
    return m_thread.joinable();
}


bool KINNG_REQUEST_SERVER::Start()
{
    if( m_thread.joinable() )
        return true;

    m_shutdown.store( false );
    m_pendingReply.clear();
    m_thread = std::thread( [&]() { listenThread(); } );
    return true;
}


void KINNG_REQUEST_SERVER::Stop()
{
    if( !m_thread.joinable() )
        return;

    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_replyReady.notify_all();
    }

    m_shutdown.store( true );
    m_thread.join();
}


void KINNG_REQUEST_SERVER::Reply( const std::string& aReply )
{
    std::lock_guard<std::mutex> lock( m_mutex );
    m_pendingReply = aReply;
    m_replyReady.notify_all();
}


void KINNG_REQUEST_SERVER::listenThread()
{
    nng_socket   socket;
    nng_listener listener;
    int          retCode = 0;

    wxLogTrace( TraceNng, wxS( "KINNG_REQUEST_SERVER starting" ) );

    retCode = nng_rep0_open( &socket );

    if( retCode != 0 )
    {
        wxLogTrace( TraceNng,
                    wxString::Format( wxS( "Got error code %d from nng_rep0_open!" ), retCode ) );
        return;
    }

    retCode = nng_listener_create( &listener, socket, m_socketUrl.c_str() );

    if( retCode != 0 )
    {
        wxLogTrace( TraceNng,
                    wxString::Format( wxS( "Got error code %d from nng_listener_create!" ),
                                      retCode ) );
        return;
    }

    nng_socket_set_ms( socket, NNG_OPT_RECVTIMEO, 500 );

    retCode = nng_listener_start( listener, 0 );

    if( retCode != 0 )
    {
        wxLogTrace( TraceNng,
                    wxString::Format( wxS( "Got error code %d from nng_listener_start!" ),
                                      retCode ) );
        nng_close( socket );
        return;
    }

    wxLogTrace( TraceNng, wxS( "KINNG_REQUEST_SERVER listener has started" ) );

    while( !m_shutdown.load() )
    {
        char*    buf = nullptr;
        size_t   sz = 0;

        retCode = nng_recv( socket, &buf, &sz, NNG_FLAG_ALLOC );

        if( retCode == NNG_ETIMEDOUT )
            continue;

        if( retCode != 0 )
        {
            if( buf )
                nng_free( buf, sz );

            wxLogTrace( TraceNng,
                        wxString::Format( wxS( "Got error code %d from nngc_recv!" ), retCode ) );
            break;
        }

        m_sharedMessage.assign( buf, sz );
        nng_free( buf, sz );
        buf = nullptr;

        if( m_callback )
            m_callback( &m_sharedMessage );

        std::unique_lock<std::mutex> lock( m_mutex );
        m_replyReady.wait( lock, [&]() { return m_shutdown.load() || !m_pendingReply.empty(); } );

        if( m_shutdown.load() )
            break;

        retCode = nng_send( socket, const_cast<std::string::value_type*>( m_pendingReply.c_str() ),
                            m_pendingReply.length(), 0 );

        if( retCode != 0 )
        {
            wxLogTrace( TraceNng,
                        wxString::Format( wxS( "Got error code %d from nng_send!" ), retCode ) );
        }
        m_pendingReply.clear();
    }

    wxLogTrace( TraceNng, wxS( "KINNG_REQUEST_SERVER shutting down" ) );

    nng_close( socket );
}


static KINNG_REQUEST_CLIENT::RESULT makeErrorResult( int aNngError )
{
    KINNG_REQUEST_CLIENT::RESULT result;

    if( aNngError == NNG_ETIMEDOUT )
        result.timed_out = true;
    else if( aNngError == NNG_ECANCELED || aNngError == NNG_ECLOSED )
        result.cancelled = true;
    else
        result.nng_error = aNngError;

    return result;
}


struct KINNG_REQUEST_CLIENT::REQUEST
{
    KINNG_REQUEST_CLIENT* client = nullptr;
    std::uint64_t         id = 0;
    nng_ctx               ctx = NNG_CTX_INITIALIZER;
    nng_aio*              aio = nullptr;
    CALLBACK              callback;
    bool                  sending = true;   ///< true while in the send phase, false while receiving
    RESULT                result;
};


struct KINNG_REQUEST_CLIENT::IMPL
{
    nng_socket                        socket = NNG_SOCKET_INITIALIZER;
    std::atomic<bool>                 running{ false };
    std::atomic<std::uint64_t>        nextId{ 0 };
    std::mutex                        lifecycle;   ///< Serializes Start and Stop against each other
    std::mutex                        mutex;       ///< Guards outstanding, completed, and the cv
    std::condition_variable           cv;
    std::map<std::uint64_t, REQUEST*> outstanding;
    std::vector<REQUEST*>             completed;
    std::thread                       reaper;
};


KINNG_REQUEST_CLIENT::KINNG_REQUEST_CLIENT( const std::string& aSocketUrl ) :
        m_socketUrl( aSocketUrl ),
        m_impl( std::make_unique<IMPL>() )
{
}


KINNG_REQUEST_CLIENT::~KINNG_REQUEST_CLIENT()
{
    Stop();
}


bool KINNG_REQUEST_CLIENT::Running() const
{
    return m_impl->running.load();
}


bool KINNG_REQUEST_CLIENT::Start()
{
    std::lock_guard<std::mutex> lifecycle( m_impl->lifecycle );

    if( m_impl->running.load() )
        return true;

    int retCode = nng_req0_open( &m_impl->socket );

    if( retCode != 0 )
    {
        wxLogTrace( TraceNng,
                    wxString::Format( wxS( "Got error code %d from nng_req0_open!" ), retCode ) );
        return false;
    }

    // Dial non-blocking so the client tolerates a peer that binds slightly later,
    // as when KiCad spawns a backend and dials before it has bound its socket.
    retCode = nng_dial( m_impl->socket, m_socketUrl.c_str(), nullptr, NNG_FLAG_NONBLOCK );

    if( retCode != 0 )
    {
        wxLogTrace( TraceNng,
                    wxString::Format( wxS( "Got error code %d from nng_dial!" ), retCode ) );
        nng_close( m_impl->socket );
        return false;
    }

    m_impl->running.store( true );
    m_impl->reaper = std::thread( [this]() { reaperThread(); } );

    return true;
}


void KINNG_REQUEST_CLIENT::Stop()
{
    std::lock_guard<std::mutex> lifecycle( m_impl->lifecycle );

    if( !m_impl->running.exchange( false ) )
        return;

    // Closing the socket terminates every in-flight send and receive with
    // NNG_ECLOSED, including a receive rescheduled during a send->receive
    // transition, so their callbacks all run and hand their requests to the
    // reaper. The reaper frees the aios, which nng forbids from a callback.
    nng_close( m_impl->socket );
    m_impl->cv.notify_all();

    if( m_impl->reaper.joinable() )
        m_impl->reaper.join();
}


std::uint64_t KINNG_REQUEST_CLIENT::Request( const std::string& aPayload, int aTimeoutMs,
                                             CALLBACK aOnComplete )
{
    if( !m_impl->running.load() )
        return 0;

    REQUEST* req = new REQUEST();
    req->client = this;
    req->callback = std::move( aOnComplete );

    int retCode = nng_ctx_open( &req->ctx, m_impl->socket );

    if( retCode != 0 )
    {
        delete req;
        return 0;
    }

    // The request reaches its callback as a void*, so nothing but a raw pointer
    // can own it once nng takes over. This guard owns it up to that handoff and
    // unwinds whichever pieces were built if the handoff never happens.
    struct CLEANUP
    {
        ~CLEANUP()
        {
            if( !request )
                return;

            if( request->aio )
            {
                if( nng_msg* msg = nng_aio_get_msg( request->aio ) )
                    nng_msg_free( msg );

                nng_aio_free( request->aio );
            }

            nng_ctx_close( request->ctx );
            delete request;
        }

        void Release() { request = nullptr; }

        REQUEST* request;
    } cleanup{ req };

    retCode = nng_aio_alloc( &req->aio, &KINNG_REQUEST_CLIENT::aioCallback, req );

    if( retCode != 0 )
        return 0;

    if( aTimeoutMs > 0 )
        nng_aio_set_timeout( req->aio, aTimeoutMs );

    nng_msg* msg = nullptr;
    retCode = nng_msg_alloc( &msg, 0 );

    if( retCode != 0 )
        return 0;

    if( !aPayload.empty() )
        nng_msg_append( msg, aPayload.data(), aPayload.size() );

    nng_aio_set_msg( req->aio, msg );

    // Register the request under the lock, re-checking running in the same
    // critical section the reaper evaluates its exit under. A request that
    // registers here is therefore always visible to a concurrent Stop, and one
    // that arrives after Stop is rejected rather than orphaned.
    std::uint64_t id = 0;

    {
        std::lock_guard<std::mutex> lock( m_impl->mutex );

        if( m_impl->running.load() )
        {
            id = ++m_impl->nextId;
            req->id = id;
            m_impl->outstanding[id] = req;
        }
    }

    if( id == 0 )
        return 0;

    // The callback can run, and free the request, before nng_ctx_send returns.
    cleanup.Release();
    nng_ctx_send( req->ctx, req->aio );

    return id;
}


void KINNG_REQUEST_CLIENT::Cancel( std::uint64_t aRequestId )
{
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    auto                        it = m_impl->outstanding.find( aRequestId );

    if( it != m_impl->outstanding.end() )
        nng_aio_cancel( it->second->aio );
}


KINNG_REQUEST_CLIENT::RESULT KINNG_REQUEST_CLIENT::RequestSync( const std::string& aPayload,
                                                               int aTimeoutMs )
{
    std::promise<RESULT> promise;
    std::future<RESULT>  future = promise.get_future();

    std::uint64_t id = Request( aPayload, aTimeoutMs,
                                [&promise]( const RESULT& aResult ) { promise.set_value( aResult ); } );

    if( id == 0 )
        return makeErrorResult( NNG_ECLOSED );

    return future.get();
}


void KINNG_REQUEST_CLIENT::aioCallback( void* aArg )
{
    REQUEST* req = static_cast<REQUEST*>( aArg );
    int      retCode = nng_aio_result( req->aio );

    if( req->sending )
    {
        if( retCode != 0 )
        {
            // The send never left; reclaim the message we attached to the aio.
            if( nng_msg* msg = nng_aio_get_msg( req->aio ) )
                nng_msg_free( msg );

            req->result = makeErrorResult( retCode );
            req->client->finishRequest( req );
            return;
        }

        req->sending = false;
        nng_ctx_recv( req->ctx, req->aio );
        return;
    }

    if( retCode != 0 )
    {
        req->result = makeErrorResult( retCode );
        req->client->finishRequest( req );
        return;
    }

    if( nng_msg* msg = nng_aio_get_msg( req->aio ) )
    {
        req->result.received = true;
        req->result.reply.assign( static_cast<const char*>( nng_msg_body( msg ) ),
                                  nng_msg_len( msg ) );
        nng_msg_free( msg );
    }
    else
    {
        // A successful receive with no message is an nng anomaly; report it as
        // an error rather than an all-false result a caller cannot interpret.
        req->result = makeErrorResult( NNG_EINTERNAL );
    }

    req->client->finishRequest( req );
}


void KINNG_REQUEST_CLIENT::finishRequest( REQUEST* aRequest )
{
    {
        std::lock_guard<std::mutex> lock( m_impl->mutex );
        m_impl->outstanding.erase( aRequest->id );
        m_impl->completed.push_back( aRequest );
    }

    m_impl->cv.notify_all();
}


void KINNG_REQUEST_CLIENT::reaperThread()
{
    std::unique_lock<std::mutex> lock( m_impl->mutex );

    while( true )
    {
        m_impl->cv.wait( lock, [this]()
                         {
                             return !m_impl->completed.empty()
                                    || ( !m_impl->running.load() && m_impl->outstanding.empty() );
                         } );

        // Take the whole batch in one go so callbacks fire in completion order
        // rather than reversed, and so the lock cycles once per batch.
        std::vector<REQUEST*> batch = std::exchange( m_impl->completed, {} );

        if( !batch.empty() )
        {
            lock.unlock();

            for( REQUEST* req : batch )
            {
                // Freeing the aio here, not inside its own callback, avoids the
                // self-join deadlock nng_aio_free would hit within the callback.
                nng_aio_free( req->aio );
                nng_ctx_close( req->ctx );

                if( req->callback )
                    req->callback( req->result );

                delete req;
            }

            lock.lock();
        }

        if( !m_impl->running.load() && m_impl->outstanding.empty() && m_impl->completed.empty() )
            break;
    }
}


struct KINNG_PUB_SERVER::IMPL
{
    nng_socket        socket = NNG_SOCKET_INITIALIZER;
    std::atomic<bool> running{ false };
};


KINNG_PUB_SERVER::KINNG_PUB_SERVER( const std::string& aSocketUrl ) :
        m_socketUrl( aSocketUrl ),
        m_impl( std::make_unique<IMPL>() )
{
}


KINNG_PUB_SERVER::~KINNG_PUB_SERVER()
{
    Stop();
}


bool KINNG_PUB_SERVER::Running() const
{
    return m_impl->running.load();
}


bool KINNG_PUB_SERVER::Start()
{
    if( m_impl->running.load() )
        return true;

    int retCode = nng_pub0_open( &m_impl->socket );

    if( retCode != 0 )
    {
        wxLogTrace( TraceNng,
                    wxString::Format( wxS( "Got error code %d from nng_pub0_open!" ), retCode ) );
        return false;
    }

    retCode = nng_listen( m_impl->socket, m_socketUrl.c_str(), nullptr, 0 );

    if( retCode != 0 )
    {
        wxLogTrace( TraceNng,
                    wxString::Format( wxS( "Got error code %d from nng_listen!" ), retCode ) );
        nng_close( m_impl->socket );
        return false;
    }

    m_impl->running.store( true );

    return true;
}


void KINNG_PUB_SERVER::Stop()
{
    if( !m_impl->running.exchange( false ) )
        return;

    nng_close( m_impl->socket );
}


bool KINNG_PUB_SERVER::Publish( const std::string& aTopic, const std::string& aPayload )
{
    if( !m_impl->running.load() )
        return false;

    std::string framed;
    framed.reserve( aTopic.size() + 1 + aPayload.size() );
    framed.append( aTopic );
    framed.push_back( TOPIC_SEPARATOR );
    framed.append( aPayload );

    int retCode = nng_send( m_impl->socket, framed.data(), framed.size(), 0 );

    if( retCode != 0 )
    {
        wxLogTrace( TraceNng,
                    wxString::Format( wxS( "Got error code %d from nng_send!" ), retCode ) );
        return false;
    }

    return true;
}
