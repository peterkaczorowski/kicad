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
 * @file mdw_backend_supervisor.h
 * @brief Owns the lifecycle of one spawned backend process.
 *
 * KiCad spawns a backend executable LSP-style and owns it end to end. The
 * supervisor generates the per-spawn socket urls and channel token, launches the
 * process, drives the handshake through an MDW_BACKEND proxy, exposes a liveness
 * ping, notices a process that died on its own, and tears the process down
 * cleanly on shutdown. Restart backoff is delegated to MDW_RESTART_POLICY.
 *
 * A supervisor is driven from a single control thread; its methods are not
 * internally synchronized against one another.
 */

#ifndef MDW_BACKEND_SUPERVISOR_H
#define MDW_BACKEND_SUPERVISOR_H

#include <kicommon.h>
#include "mdw_backend.h"
#include "mdw_backend_registry.h"
#include "mdw_restart_policy.h"

#include <atomic>
#include <memory>

#include <wx/string.h>

class KICOMMON_API MDW_BACKEND_SUPERVISOR
{
public:
    MDW_BACKEND_SUPERVISOR( const MDW_BACKEND_MANIFEST& aManifest );

    ~MDW_BACKEND_SUPERVISOR();

    /**
     * Spawn the backend and complete the handshake. Blocking; must not run on the
     * UI thread. Returns true and makes Backend() available on success.
     */
    bool Launch( const wxString& aKicadVersion, int aHandshakeTimeoutMs );

    /// Send Bye then terminate the process, escalating SIGTERM to SIGKILL.
    void Shutdown();

    /**
     * Reap the spawned process if it has exited and report whether that exit was
     * unexpected. Shutdown() is the only exit this supervisor asks for, so a
     * process gone without one has crashed and the owner should either Restart()
     * or treat the session as offline. Non-blocking, and cheap enough to poll.
     */
    bool CrashDetected();

    /**
     * Respawn after CrashDetected(), waiting out the backoff MDW_RESTART_POLICY
     * schedules for this attempt. Returns false when the policy has given up on
     * the backend or the fresh handshake fails, either of which leaves the
     * supervisor offline.
     *
     * A relaunch replaces MDW_BACKEND and moves the runtime directory, so on
     * success every object the owner derived from Backend(), RuntimeDir() or
     * ChannelToken() is stale and must be rebuilt. Blocking; must not run on the
     * UI thread.
     */
    bool Restart( const wxString& aKicadVersion, int aHandshakeTimeoutMs );

    /// True once the restart policy refused a further attempt, which is
    /// permanent for this supervisor.
    bool GaveUp() const { return m_gaveUp; }

    /// One health probe. Returns true if the backend answered a ping.
    bool Ping( int aTimeoutMs );

    MDW_BACKEND* Backend() const { return m_backend.get(); }

    bool Online() const { return m_online.load(); }

    long ProcessId() const { return m_pid; }

    const wxString& ControlUrl() const { return m_controlUrl; }

    const wxString& ChannelToken() const { return m_channelToken; }

    /// The 0700 per-session runtime directory, empty until Launch() creates it.
    const wxString& RuntimeDir() const { return m_runtimeDir; }

private:
    bool spawnProcess();

    void terminateProcess();

    /// Collect the child if it has already exited, clearing m_pid so nothing
    /// waits on it twice. False while it is still running.
    bool reapIfExited();

    MDW_BACKEND_MANIFEST          m_manifest;
    wxString                      m_controlUrl;
    wxString                      m_channelToken;
    wxString                      m_runtimeDir;
    long                          m_pid = 0;
    std::atomic<bool>             m_online{ false };
    std::unique_ptr<MDW_BACKEND>  m_backend;
    MDW_RESTART_POLICY            m_restartPolicy;
    bool                          m_shutdownRequested = false;
    bool                          m_gaveUp = false;
};

#endif // MDW_BACKEND_SUPERVISOR_H
