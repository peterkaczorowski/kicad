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
 * @file mdw_backend.h
 * @brief The in-process proxy for a single out-of-process backend.
 *
 * The proxy is the whole framework's view of a backend. It owns the transport,
 * performs the handshake, caches the advertised identity, capabilities and
 * schema, and turns typed request/response protobufs into envelope exchanges.
 * Callers above the proxy see wxString value structs and capability-group
 * queries, never a protobuf and never a socket.
 */

#ifndef MDW_BACKEND_H
#define MDW_BACKEND_H

#include <kicommon.h>

#include "mdw_transport.h"
#include "mdw_auth.h"
#include "mdw_capabilities.h"
#include "mdw_schema.h"
#include "mdw_types.h"

#include <api/mdw/mdw_envelope.pb.h>

#include <atomic>
#include <functional>
#include <memory>

#include <wx/string.h>

class KICOMMON_API MDW_BACKEND
{
public:
    MDW_BACKEND( std::unique_ptr<MDW_TRANSPORT> aTransport, const wxString& aChannelToken );

    ~MDW_BACKEND();

    bool Start();

    void Stop();

    /**
     * Perform the Hello handshake and fetch the schema. Blocking; run it on a
     * worker thread at spawn time. Returns false on version mismatch, timeout,
     * or transport failure.
     */
    bool Handshake( const wxString& aKicadVersion, const wxString& aEventsUrl, int aTimeoutMs );

    bool Handshaked() const { return m_handshaked; }

    const MDW_BACKEND_INFO& Info() const { return m_info; }

    const MDW_CAPABILITIES& Capabilities() const { return m_capabilities; }

    const MDW_SCHEMA& Schema() const { return m_schema; }

    bool Has( MDW_CAPABILITY_GROUP aGroup ) const { return m_capabilities.Has( aGroup ); }

    /**
     * Send a typed request asynchronously. aOnComplete receives the framework
     * error code and, on success, the decoded response. Returns a cancellation
     * handle, or 0 if not running.
     */
    template <typename Req, typename Resp>
    std::uint64_t CallAsync( const Req& aRequest, int aTimeoutMs,
                             std::function<void( MDW_ERROR_CODE, const Resp& )> aOnComplete )
    {
        kiapi::mdw::MdwRequest envelope = makeEnvelope( aRequest );

        return m_transport->SendAsync(
                envelope, aTimeoutMs,
                [aOnComplete]( const MDW_TRANSPORT_RESULT& aResult )
                {
                    Resp           resp;
                    MDW_ERROR_CODE code = decode( aResult, resp );
                    aOnComplete( code, resp );
                } );
    }

    /// Blocking typed request for conformance and CLI callers.
    template <typename Req, typename Resp>
    MDW_ERROR_CODE CallSync( const Req& aRequest, Resp& aResponse, int aTimeoutMs )
    {
        kiapi::mdw::MdwRequest envelope = makeEnvelope( aRequest );
        MDW_TRANSPORT_RESULT   result = m_transport->SendSync( envelope, aTimeoutMs );

        return decode( result, aResponse );
    }

    void Cancel( std::uint64_t aHandle ) { m_transport->Cancel( aHandle ); }

    bool Ping( int aTimeoutMs );

    void Bye( int aTimeoutMs );

private:
    kiapi::mdw::MdwRequest makeEnvelope( const google::protobuf::Message& aPayload );

    /// Map a transport result plus its envelope error onto a framework code and
    /// unpack the payload into aResponse when the call succeeded.
    template <typename Resp>
    static MDW_ERROR_CODE decode( const MDW_TRANSPORT_RESULT& aResult, Resp& aResponse )
    {
        if( aResult.timed_out )
            return MDW_ERROR_CODE::TIMEOUT;

        if( aResult.cancelled )
            return MDW_ERROR_CODE::CANCELLED;

        if( aResult.backend_lost || !aResult.ok )
            return MDW_ERROR_CODE::BACKEND_LOST;

        MDW_ERROR_CODE code = static_cast<MDW_ERROR_CODE>( aResult.response.error().code() );

        if( code == MDW_ERROR_CODE::SUCCESS && aResult.response.has_payload() )
        {
            if( !aResult.response.payload().UnpackTo( &aResponse ) )
                return MDW_ERROR_CODE::PROTOCOL;
        }

        return code;
    }

    std::unique_ptr<MDW_TRANSPORT> m_transport;
    wxString                       m_channelToken;
    bool                           m_handshaked = false;
    MDW_BACKEND_INFO               m_info;
    MDW_CAPABILITIES               m_capabilities;
    MDW_SCHEMA                     m_schema;
    std::atomic<std::uint64_t>     m_requestSeq{ 0 };
};

#endif // MDW_BACKEND_H
