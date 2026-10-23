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

#ifndef KICAD_KINNG_H
#define KICAD_KINNG_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>


class KINNG_REQUEST_SERVER
{
public:
    KINNG_REQUEST_SERVER( const std::string& aSocketUrl );

    ~KINNG_REQUEST_SERVER();

    bool Start();

    void Stop();

    bool Running() const;

    void SetCallback( std::function<void(std::string*)> aFunc ) { m_callback = aFunc; }

    void Reply( const std::string& aReply );

    const std::string& SocketPath() const { return m_socketUrl; }

private:
    void listenThread();

    std::thread m_thread;

    std::atomic<bool> m_shutdown;

    std::string m_socketUrl;

    std::function<void(std::string*)> m_callback;

    std::string m_sharedMessage;

    std::string m_pendingReply;

    std::condition_variable m_replyReady;

    std::mutex m_mutex;
};


/**
 * An asynchronous nng REQ client. Each in-flight request runs on its own
 * nng_ctx so a slow request never blocks a fast one on the same socket, so a
 * short Ping is not queued behind a long Checkin. Completions are delivered on
 * a private reaper thread; callers marshal them to their own thread. Requests
 * carry per-request timeouts and are individually cancellable.
 */
class KINNG_REQUEST_CLIENT
{
public:
    struct RESULT
    {
        bool        received = false;   ///< A reply was received
        bool        timed_out = false;  ///< The per-request timeout elapsed
        bool        cancelled = false;  ///< Cancel() or Stop() aborted the request
        int         nng_error = 0;      ///< Raw nng error when the failure is none of the above
        std::string reply;              ///< Reply bytes when received is true
    };

    using CALLBACK = std::function<void( const RESULT& )>;

    KINNG_REQUEST_CLIENT( const std::string& aSocketUrl );

    ~KINNG_REQUEST_CLIENT();

    /// Open the REQ socket, dial the peer, and start the reaper thread.
    bool Start();

    /// Cancel every in-flight request, drain completions, and close the socket.
    void Stop();

    bool Running() const;

    /**
     * Enqueue an asynchronous request. Returns a request id usable with Cancel,
     * or 0 if the client is not running. aTimeoutMs of 0 or less disables the
     * timeout. aOnComplete runs on the reaper thread once, always exactly once.
     */
    std::uint64_t Request( const std::string& aPayload, int aTimeoutMs, CALLBACK aOnComplete );

    /// Best-effort abort of an in-flight request.
    void Cancel( std::uint64_t aRequestId );

    /**
     * Blocking request for conformance and CLI callers. Must not run on the UI
     * thread; it blocks until the reply, timeout, or cancellation.
     */
    RESULT RequestSync( const std::string& aPayload, int aTimeoutMs );

    const std::string& SocketPath() const { return m_socketUrl; }

private:
    struct REQUEST;
    struct IMPL;

    static void aioCallback( void* aArg );

    /// Move a finished request to the completion queue and wake the reaper.
    void finishRequest( REQUEST* aRequest );

    void reaperThread();

    std::string           m_socketUrl;
    std::unique_ptr<IMPL> m_impl;
};


/**
 * An nng PUB server. KiCad publishes workspace events to external IPC clients
 * over this socket. Messages are framed as a topic prefix, a NUL separator, and
 * the payload so nng's prefix subscription matches on the topic.
 *
 * The owner drives the lifecycle from one thread; Start and Stop must not
 * overlap each other or a Publish. KINNG_REQUEST_CLIENT serializes its own Start
 * and Stop, but only because its Stop joins the reaper thread. The same lock
 * here would buy nothing, since Stop closes the socket out from under a
 * concurrent Publish either way.
 */
class KINNG_PUB_SERVER
{
public:
    KINNG_PUB_SERVER( const std::string& aSocketUrl );

    ~KINNG_PUB_SERVER();

    bool Start();

    void Stop();

    bool Running() const;

    /// Publish a payload under a topic. Returns false if not running or on error.
    bool Publish( const std::string& aTopic, const std::string& aPayload );

    const std::string& SocketPath() const { return m_socketUrl; }

    /// The framing separator between topic and payload.
    static constexpr char TOPIC_SEPARATOR = '\0';

private:
    struct IMPL;

    std::string           m_socketUrl;
    std::unique_ptr<IMPL> m_impl;
};

#endif //KICAD_KINNG_H
