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
 * @file mock_odoo_server.h
 * @brief A tiny in-process HTTP/1.1 JSON-RPC server that impersonates Odoo.
 *
 * It listens on 127.0.0.1 on a kernel-assigned port (bind to port 0), reads one
 * JSON-RPC request per connection, and returns canned {"result": ...} bodies for
 * the login and execute_kw methods the Odoo backend drives. POSIX-only.
 */

#ifndef MOCK_ODOO_SERVER_H
#define MOCK_ODOO_SERVER_H

#include <atomic>
#include <thread>

class MOCK_ODOO_SERVER
{
public:
    MOCK_ODOO_SERVER();

    ~MOCK_ODOO_SERVER();

    /// Bind, listen and spawn the accept thread. Returns false on any socket error.
    bool Start();

    void Stop();

    /// The kernel-assigned listening port, valid after a successful Start.
    int GetPort() const { return m_port; }

private:
    void run();

    std::atomic<int>  m_listenFd{ -1 };
    int               m_port = 0;
    std::atomic<bool> m_running{ false };
    std::thread       m_thread;
};

#endif // MOCK_ODOO_SERVER_H
