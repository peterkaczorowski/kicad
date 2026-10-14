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

#include <mdw_sdk/mdw_backend_server.h>

#include <nng/nng.h>
#include <nng/protocol/reqrep0/rep.h>

#include <chrono>
#include <thread>
#include <vector>

struct MDW_BACKEND_SERVER::IMPL
{
    nng_socket               socket = NNG_SOCKET_INITIALIZER;
    std::atomic<bool>        running{ false };
    std::vector<std::thread> workers;
};


MDW_BACKEND_SERVER::MDW_BACKEND_SERVER() :
        m_impl( std::make_unique<IMPL>() )
{
}


MDW_BACKEND_SERVER::~MDW_BACKEND_SERVER()
{
    Stop();
}



bool MDW_BACKEND_SERVER::Running() const
{
    return m_impl->running.load();
}


bool MDW_BACKEND_SERVER::Start( const std::string& aControlUrl, int aWorkers )
{
    if( m_impl->running.load() )
        return true;

    if( nng_rep0_open( &m_impl->socket ) != 0 )
        return false;

    if( nng_listen( m_impl->socket, aControlUrl.c_str(), nullptr, 0 ) != 0 )
    {
        nng_close( m_impl->socket );
        return false;
    }

    m_impl->running.store( true );

    if( aWorkers < 1 )
        aWorkers = 1;

    for( int i = 0; i < aWorkers; ++i )
        m_impl->workers.emplace_back( [this]() { workerLoop(); } );

    return true;
}


void MDW_BACKEND_SERVER::Stop()
{
    if( !m_impl->running.exchange( false ) )
        return;

    // Closing the socket aborts each worker's blocking receive with NNG_ECLOSED
    // so the worker loops exit.
    nng_close( m_impl->socket );

    for( std::thread& worker : m_impl->workers )
    {
        if( worker.joinable() )
            worker.join();
    }

    m_impl->workers.clear();
}


void MDW_BACKEND_SERVER::dispatch( const kiapi::mdw::MdwRequest& aRequest,
                                   kiapi::mdw::MdwResponse&      aResponse )
{
    aResponse.set_channel_token( m_channelToken );
    aResponse.set_request_id( aRequest.request_id() );

    if( !m_channelToken.empty() && aRequest.channel_token() != m_channelToken )
    {
        aResponse.mutable_error()->set_code( kiapi::workspace::WE_PROTOCOL );
        aResponse.mutable_error()->set_message( "channel token mismatch" );
        return;
    }

    const std::string& typeUrl = aRequest.payload().type_url();
    std::string::size_type slash = typeUrl.find_last_of( '/' );
    std::string typeName = slash == std::string::npos ? typeUrl : typeUrl.substr( slash + 1 );

    auto it = m_handlers.find( typeName );

    if( it == m_handlers.end() )
    {
        aResponse.mutable_error()->set_code( kiapi::workspace::WE_NOT_SUPPORTED );
        aResponse.mutable_error()->set_message( "no handler for " + typeName );
        return;
    }

    it->second( aRequest.payload(), aResponse );
}


void MDW_BACKEND_SERVER::workerLoop()
{
    nng_ctx  ctx;
    nng_aio* aio = nullptr;

    if( nng_ctx_open( &ctx, m_impl->socket ) != 0 )
        return;

    if( nng_aio_alloc( &aio, nullptr, nullptr ) != 0 )
    {
        nng_ctx_close( ctx );
        return;
    }

    while( m_impl->running.load() )
    {
        nng_ctx_recv( ctx, aio );
        nng_aio_wait( aio );

        if( nng_aio_result( aio ) != 0 )
        {
            // NNG_ECLOSED on shutdown; a persistent error otherwise must not spin
            // this worker at full core, so back off before retrying.
            if( !m_impl->running.load() )
                break;

            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
            continue;
        }

        nng_msg* msg = nng_aio_get_msg( aio );

        kiapi::mdw::MdwRequest  requestEnvelope;
        kiapi::mdw::MdwResponse responseEnvelope;

        bool parsed = requestEnvelope.ParseFromArray( nng_msg_body( msg ),
                                                      static_cast<int>( nng_msg_len( msg ) ) );
        nng_msg_free( msg );

        try
        {
            if( parsed )
            {
                dispatch( requestEnvelope, responseEnvelope );
            }
            else
            {
                responseEnvelope.set_channel_token( m_channelToken );
                responseEnvelope.mutable_error()->set_code( kiapi::workspace::WE_PROTOCOL );
                responseEnvelope.mutable_error()->set_message( "malformed request envelope" );
            }
        }
        catch( ... )
        {
            // A handler must never take down the process; answer with an error.
            responseEnvelope.Clear();
            responseEnvelope.set_channel_token( m_channelToken );
            responseEnvelope.set_request_id( requestEnvelope.request_id() );
            responseEnvelope.mutable_error()->set_code( kiapi::workspace::WE_INTERNAL );
            responseEnvelope.mutable_error()->set_message( "handler raised an exception" );
        }

        std::string out;
        responseEnvelope.SerializeToString( &out );

        // The REP context owes a reply once a request was received; never skip
        // the send, or the next receive fails NNG_ESTATE and the worker spins.
        nng_msg* reply = nullptr;

        while( nng_msg_alloc( &reply, 0 ) != 0 )
        {
            if( !m_impl->running.load() )
                break;

            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }

        if( !reply )
            break;

        nng_msg_append( reply, out.data(), out.size() );
        nng_aio_set_msg( aio, reply );
        nng_ctx_send( ctx, aio );
        nng_aio_wait( aio );

        if( nng_aio_result( aio ) != 0 )
        {
            if( nng_msg* unsent = nng_aio_get_msg( aio ) )
                nng_msg_free( unsent );
        }
    }

    nng_aio_free( aio );
    nng_ctx_close( ctx );
}
