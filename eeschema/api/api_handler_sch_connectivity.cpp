/* Copyright The KiCad Developers. SPDX-License-Identifier: GPL-3.0-or-later */
#include <api/api_handler_sch.h>
#include <api/api_sch_connectivity.h>
#include <sch_edit_frame.h>
#include <connection_graph.h>
#include <sch_symbol.h>
#include <sch_screen.h>
#include <sch_pin.h>
#include <sch_sheet_pin.h>
#include <sch_label.h>
#include <tools/sch_selection_tool.h>
#include <tools/sch_actions.h>
#include <tool/tool_manager.h>
#include <set>
#include <map>

using namespace kiapi::schematic::commands;
using namespace kiapi::schematic::types;

namespace
{
ApiResponseStatus badRequest( const std::string& message )
{
    ApiResponseStatus error;
    error.set_status( ApiStatusCode::AS_BAD_REQUEST );
    error.set_error_message( message );
    return error;
}

bool sameSheet( const DocumentSpecifier& a, const DocumentSpecifier& b )
{
    return a.sheet_path().SerializeAsString() == b.sheet_path().SerializeAsString();
}

void filterNets( ConnectivitySnapshot& snapshot, std::set<std::string> ids )
{
    // A bus request includes each leaf net; no arbitrary choice of one signal.
    for( const auto& net : snapshot.nets() )
        if( ids.count( net.id() ) )
            ids.insert( net.bus_member_ids().begin(), net.bus_member_ids().end() );
    auto nets = snapshot.nets();
    snapshot.clear_nets();
    for( const auto& net : nets )
        if( ids.count( net.id() ) ) *snapshot.add_nets() = net;
}

const SchematicNet* findNet( const ConnectivitySnapshot& snapshot, const std::string& id,
                             const std::string& name )
{
    const SchematicNet* result = nullptr;
    for( const auto& net : snapshot.nets() )
    {
        if( ( !id.empty() && net.id() == id ) || ( id.empty() && net.name() == name ) )
        {
            if( result ) return nullptr; // Reject ambiguous bus/net names.
            result = &net;
        }
    }
    return result;
}

bool stale( const ConnectivitySnapshot& snapshot, const std::string& session, uint64_t revision )
{
    return ( !session.empty() && session != snapshot.session_id() )
           || ( revision && revision != snapshot.revision() );
}
}

void API_HANDLER_SCH::registerConnectivityHandlers()
{
    registerHandler<GetSchematicNetlist, NetQueryResponse>( &API_HANDLER_SCH::handleGetSchematicNetlist );
}

HANDLER_RESULT<ConnectivitySnapshot> API_HANDLER_SCH::connectivitySnapshot( const DocumentSpecifier& document )
{
    auto path = resolveDocument( document );
    if( !path )
    {
        ApiResponseStatus error;
        error.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( error );
    }
    if( auto busy = checkForBusy() ) return tl::unexpected( *busy );
    if( !document.has_sheet_path() || document.project().path().empty() )
        return tl::unexpected( badRequest( "Connectivity queries require an explicit project path and sheet UUID path" ) );

    auto* frame = static_cast<SCH_EDIT_FRAME*>( m_frame );
    // NO_CLEANUP updates caches and rule areas, without changing schematic
    // geometry, writing files, creating commits, or running ERC/adding markers.
    // No last-change list: force the full hierarchy path, not incremental undo-based updates.
    frame->Schematic().RecalculateConnections( nullptr, NO_CLEANUP, frame->GetToolManager() );
    auto snapshot = BuildSchematicConnectivity( frame->Schematic() );
    *snapshot.mutable_document() = describeDocument( frame->Schematic().Hierarchy().at( 0 ) );
    std::string content = snapshot.SerializeAsString();
    for( const auto& sheet : frame->Schematic().Hierarchy() )
    {
        content += describeDocument( sheet ).SerializeAsString();
        content += sheet.LastScreen()->GetFileName().ToStdString();
    }
    if( m_connectivityRevision == 0 || content != m_connectivityContent )
    {
        m_connectivityContent = std::move( content );
        ++m_connectivityRevision;
    }
    StampSchematicConnectivity( snapshot, m_connectivitySession, m_connectivityRevision );
    *snapshot.mutable_document() = describeDocument( *path );
    return snapshot;
}

HANDLER_RESULT<NetQueryResponse> API_HANDLER_SCH::handleGetSchematicNetlist(
        const HANDLER_CONTEXT<GetSchematicNetlist>& ctx )
{
    auto snapshot = connectivitySnapshot( ctx.Request.document() );
    if( !snapshot ) return tl::unexpected( snapshot.error() );
    NetQueryResponse response;
    *response.mutable_snapshot() = std::move( *snapshot );
    return response;
}
