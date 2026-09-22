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
 * @file mdw_transport.h
 * @brief The control-channel transport seam between the framework and a backend.
 *
 * The MDW_BACKEND proxy talks to a backend only through this interface, so the
 * async and cancellation logic can be unit-tested against an in-process loopback
 * without spawning a process. The nng implementation carries requests to a real
 * backend; the loopback implementation dispatches them to an in-process handler.
 */

#ifndef MDW_TRANSPORT_H
#define MDW_TRANSPORT_H

#include <api/mdw/mdw_envelope.pb.h>

#include <cstdint>
#include <functional>

/// How one transport exchange ended. These are mutually exclusive.
enum class MDW_TRANSPORT_STATUS
{
    OK,           ///< A response envelope was received and parsed
    TIMED_OUT,    ///< The request timed out
    CANCELLED,    ///< Cancel or shutdown aborted it
    BACKEND_LOST  ///< Transport failed, or the reply did not parse
};


struct MDW_TRANSPORT_RESULT
{
    MDW_TRANSPORT_STATUS    status = MDW_TRANSPORT_STATUS::BACKEND_LOST;
    kiapi::mdw::MdwResponse response;  ///< Valid when status is OK
};


/**
 * An asynchronous request/response channel to a single backend. Completions are
 * delivered on a transport-owned thread; callers marshal them to their own.
 */
class MDW_TRANSPORT
{
public:
    using CALLBACK = std::function<void( const MDW_TRANSPORT_RESULT& )>;

    virtual ~MDW_TRANSPORT() = default;

    virtual bool Start() = 0;

    virtual void Stop() = 0;

    /**
     * Send a request asynchronously. Returns a handle usable with Cancel, or 0
     * if the transport is not running. aTimeoutMs of 0 or less disables the
     * timeout. aOnComplete runs exactly once.
     */
    virtual std::uint64_t SendAsync( const kiapi::mdw::MdwRequest& aRequest, int aTimeoutMs,
                                     CALLBACK aOnComplete ) = 0;

    /// Blocking send for conformance and CLI callers; must not run on the UI thread.
    virtual MDW_TRANSPORT_RESULT SendSync( const kiapi::mdw::MdwRequest& aRequest,
                                           int aTimeoutMs ) = 0;

    virtual void Cancel( std::uint64_t aHandle ) = 0;
};

#endif // MDW_TRANSPORT_H
