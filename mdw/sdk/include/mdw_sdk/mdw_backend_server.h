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
 * @file mdw_backend_server.h
 * @brief The MDW backend request server.
 *
 * This is the API_HANDLER dispatch model with wxWidgets removed. A backend
 * registers a typed handler per request message; the server decodes the
 * MdwRequest envelope, validates the channel token, routes the payload to the
 * matching handler by protobuf type name, and returns the handler's result in
 * the MdwResponse envelope. A request with no registered handler answers
 * WE_NOT_SUPPORTED, which is the capability-honesty guarantee.
 *
 * The MDW backend SDK links kiapi and nng only. It must never include kicommon
 * or wxWidgets; that constraint is what keeps wxString out of the protocol.
 */

#ifndef MDW_BACKEND_SERVER_H
#define MDW_BACKEND_SERVER_H

#include <api/mdw/mdw_envelope.pb.h>
#include <api/workspace/workspace_types.pb.h>

#include <google/protobuf/any.pb.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>

/// The success result every handler returns when it has nothing to report.
inline kiapi::workspace::WorkspaceError MdwOk()
{
    kiapi::workspace::WorkspaceError error;
    error.set_code( kiapi::workspace::WE_OK );

    return error;
}


/// A failure result carrying a code and a message for the user.
inline kiapi::workspace::WorkspaceError MdwFail( kiapi::workspace::WorkspaceErrorCode aCode,
                                                 const std::string&                   aMessage )
{
    kiapi::workspace::WorkspaceError error;
    error.set_code( aCode );
    error.set_message( aMessage );

    return error;
}


class MDW_BACKEND_SERVER
{
public:
    /// A raw handler decodes the request payload and fills the response envelope.
    using RAW_HANDLER = std::function<void( const google::protobuf::Any& aRequest,
                                            kiapi::mdw::MdwResponse&      aResponse )>;

    MDW_BACKEND_SERVER();

    ~MDW_BACKEND_SERVER();

    void SetChannelToken( const std::string& aToken ) { m_channelToken = aToken; }

    /**
     * Register a typed handler for request message Req producing response Resp.
     * The handler returns a WorkspaceError; on WE_OK the filled Resp is packed
     * into the response payload, otherwise the error rides the envelope. All
     * handlers must be registered before Start; the handler map is not guarded
     * against concurrent registration once the worker threads are serving.
     */
    template <typename Req, typename Resp>
    void RegisterHandler( std::function<kiapi::workspace::WorkspaceError( const Req&, Resp& )> aFn )
    {
        RAW_HANDLER raw = [aFn]( const google::protobuf::Any& aRequest,
                                 kiapi::mdw::MdwResponse&      aResponse )
        {
            Req req;

            if( !aRequest.UnpackTo( &req ) )
            {
                aResponse.mutable_error()->set_code( kiapi::workspace::WE_PROTOCOL );
                aResponse.mutable_error()->set_message( "malformed request payload" );
                return;
            }

            Resp                            resp;
            kiapi::workspace::WorkspaceError error = aFn( req, resp );
            *aResponse.mutable_error() = error;

            if( error.code() == kiapi::workspace::WE_OK )
                aResponse.mutable_payload()->PackFrom( resp );
        };

        m_handlers[Req::descriptor()->full_name()] = std::move( raw );
    }

    /// Bind the control socket and spawn aWorkers blocking worker threads.
    bool Start( const std::string& aControlUrl, int aWorkers );

    void Stop();

    bool Running() const;

    /// True once a Bye request has been dispatched, so the scaffold can exit.
    bool ByeReceived() const { return m_bye.load(); }

    void SignalBye() { m_bye.store( true ); }

private:
    struct IMPL;

    void workerLoop();

    void dispatch( const kiapi::mdw::MdwRequest& aRequest, kiapi::mdw::MdwResponse& aResponse );

    std::string                        m_channelToken;
    std::map<std::string, RAW_HANDLER> m_handlers;
    std::atomic<bool>                  m_bye{ false };
    std::unique_ptr<IMPL>              m_impl;
};

#endif // MDW_BACKEND_SERVER_H
