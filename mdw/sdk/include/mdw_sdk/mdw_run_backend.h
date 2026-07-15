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
 * @file mdw_run_backend.h
 * @brief The MDW backend main scaffold.
 *
 * A backend author configures a server with handlers, describes its identity,
 * capabilities and schema, and hands both to MDW_RunBackend. The scaffold picks
 * up the control and event socket urls and the channel token, registers the
 * built-in handshake and liveness handlers, starts serving, watches for parent
 * death, and runs until a Bye request or a termination signal.
 */

#ifndef MDW_RUN_BACKEND_H
#define MDW_RUN_BACKEND_H

#include <mdw_sdk/mdw_backend_server.h>

#include <api/workspace/workspace_types.pb.h>

#include <string>

/// The protocol version this SDK speaks. Major must match KiCad; minor is additive.
constexpr unsigned MDW_PROTOCOL_VERSION_MAJOR = 1;
constexpr unsigned MDW_PROTOCOL_VERSION_MINOR = 0;

class MDW_EVENT_PUBLISHER;

/// Backend identity, capability advertisement, and item schema.
struct MDW_BACKEND_DESCRIPTOR
{
    kiapi::workspace::BackendInfo  info;
    kiapi::workspace::Capabilities capabilities;
    kiapi::workspace::Schema       schema;
};

/// Parsed launch parameters. Populated from argv and the environment.
struct MDW_BACKEND_LAUNCH
{
    std::string control_url;
    std::string events_url;
    std::string channel_token;
    long        parent_pid = 0;
    bool        valid = false;
};

/// Parse the launch parameters from argv and the KICAD_MDW_TOKEN environment.
MDW_BACKEND_LAUNCH MDW_ParseLaunch( int argc, char** argv );

/**
 * Run a backend to completion. Registers the built-in Hello/GetBackendInfo/
 * GetCapabilities/GetSchema/Ping/Bye handlers on aServer, starts it on the
 * control url, optionally starts aPublisher on the event url, and blocks until
 * a Bye request, a termination signal, or parent death. Returns a process exit
 * code.
 */
int MDW_RunBackend( const MDW_BACKEND_LAUNCH& aLaunch, const MDW_BACKEND_DESCRIPTOR& aDescriptor,
                    MDW_BACKEND_SERVER& aServer, MDW_EVENT_PUBLISHER* aPublisher );

#endif // MDW_RUN_BACKEND_H
