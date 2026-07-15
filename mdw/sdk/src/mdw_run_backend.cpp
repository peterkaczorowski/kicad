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

#include <mdw_sdk/mdw_run_backend.h>
#include <mdw_sdk/mdw_event_publisher.h>

#include <api/mdw/backend_base.pb.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <string>
#include <thread>

#if defined( __linux__ )
#include <sys/prctl.h>
#include <unistd.h>
#endif

namespace
{
std::atomic<bool> g_terminate{ false };


void onSignal( int )
{
    g_terminate.store( true );
}


kiapi::workspace::WorkspaceError okResult()
{
    kiapi::workspace::WorkspaceError error;
    error.set_code( kiapi::workspace::WE_OK );
    return error;
}


kiapi::workspace::WorkspaceError failResult( kiapi::workspace::WorkspaceErrorCode aCode,
                                             const std::string&                   aMessage )
{
    kiapi::workspace::WorkspaceError error;
    error.set_code( aCode );
    error.set_message( aMessage );
    return error;
}
}


MDW_BACKEND_LAUNCH MDW_ParseLaunch( int argc, char** argv )
{
    MDW_BACKEND_LAUNCH launch;

    for( int i = 1; i < argc; ++i )
    {
        std::string arg = argv[i];

        if( arg == "--control" && i + 1 < argc )
            launch.control_url = argv[++i];
        else if( arg == "--events" && i + 1 < argc )
            launch.events_url = argv[++i];
        else if( arg == "--parent-pid" && i + 1 < argc )
            launch.parent_pid = std::strtol( argv[++i], nullptr, 10 );
    }

    if( const char* token = std::getenv( "KICAD_MDW_TOKEN" ) )
        launch.channel_token = token;

    launch.valid = !launch.control_url.empty();

    return launch;
}


static void registerBuiltins( const MDW_BACKEND_DESCRIPTOR& aDescriptor, MDW_BACKEND_SERVER& aServer )
{
    aServer.RegisterHandler<kiapi::mdw::Hello, kiapi::mdw::HelloReply>(
            [aDescriptor]( const kiapi::mdw::Hello& aReq, kiapi::mdw::HelloReply& aReply )
            {
                if( aReq.protocol_version().major() != MDW_PROTOCOL_VERSION_MAJOR )
                    return failResult( kiapi::workspace::WE_PROTOCOL, "protocol major mismatch" );

                aReply.mutable_protocol_version()->set_major( MDW_PROTOCOL_VERSION_MAJOR );
                aReply.mutable_protocol_version()->set_minor( MDW_PROTOCOL_VERSION_MINOR );
                *aReply.mutable_backend_info() = aDescriptor.info;
                *aReply.mutable_capabilities() = aDescriptor.capabilities;

                return okResult();
            } );

    aServer.RegisterHandler<kiapi::mdw::GetBackendInfoRequest, kiapi::mdw::GetBackendInfoReply>(
            [aDescriptor]( const kiapi::mdw::GetBackendInfoRequest&, kiapi::mdw::GetBackendInfoReply& aReply )
            {
                *aReply.mutable_info() = aDescriptor.info;
                return okResult();
            } );

    aServer.RegisterHandler<kiapi::mdw::GetCapabilitiesRequest, kiapi::mdw::GetCapabilitiesReply>(
            [aDescriptor]( const kiapi::mdw::GetCapabilitiesRequest&, kiapi::mdw::GetCapabilitiesReply& aReply )
            {
                *aReply.mutable_capabilities() = aDescriptor.capabilities;
                return okResult();
            } );

    aServer.RegisterHandler<kiapi::mdw::GetSchemaRequest, kiapi::mdw::GetSchemaReply>(
            [aDescriptor]( const kiapi::mdw::GetSchemaRequest&, kiapi::mdw::GetSchemaReply& aReply )
            {
                *aReply.mutable_schema() = aDescriptor.schema;
                return okResult();
            } );

    aServer.RegisterHandler<kiapi::mdw::PingRequest, kiapi::mdw::PingReply>(
            []( const kiapi::mdw::PingRequest&, kiapi::mdw::PingReply& ) { return okResult(); } );

    aServer.RegisterHandler<kiapi::mdw::ByeRequest, kiapi::mdw::ByeReply>(
            [&aServer]( const kiapi::mdw::ByeRequest&, kiapi::mdw::ByeReply& )
            {
                aServer.SignalBye();
                return okResult();
            } );
}


int MDW_RunBackend( const MDW_BACKEND_LAUNCH& aLaunch, const MDW_BACKEND_DESCRIPTOR& aDescriptor,
                    MDW_BACKEND_SERVER& aServer, MDW_EVENT_PUBLISHER* aPublisher )
{
    if( !aLaunch.valid )
        return 2;

    std::signal( SIGTERM, onSignal );
    std::signal( SIGINT, onSignal );

#if defined( __linux__ )
    // Ask the kernel to signal us if KiCad, our parent, dies, so we never orphan.
    prctl( PR_SET_PDEATHSIG, SIGTERM );

    // If KiCad died in the window before that call, the death signal will never
    // arrive, so detect the reparent and exit now.
    if( aLaunch.parent_pid > 0 && getppid() != aLaunch.parent_pid )
        return 0;
#endif

    aServer.SetChannelToken( aLaunch.channel_token );
    registerBuiltins( aDescriptor, aServer );

    if( !aServer.Start( aLaunch.control_url, 4 ) )
        return 3;

    if( aPublisher && !aLaunch.events_url.empty() )
        aPublisher->Start( aLaunch.events_url );

    while( aServer.Running() && !aServer.ByeReceived() && !g_terminate.load() )
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    if( aPublisher )
        aPublisher->Stop();

    aServer.Stop();

    return 0;
}
