/* Copyright The KiCad Developers. SPDX-License-Identifier: GPL-3.0-or-later */
#include <api/api_handler_sch.h>
#include <api/api_sch_net_rename.h>
#include <sch_edit_frame.h>
#include <sch_commit.h>
#include <sch_commit_flags.h>
#include <sch_label.h>
#include <sch_screen.h>
#include <tool/tool_manager.h>

using namespace kiapi::schematic::commands;
namespace
{
ApiResponseStatus renameError( const std::string& message )
{
    ApiResponseStatus error;
    error.set_status( ApiStatusCode::AS_BAD_REQUEST );
    error.set_error_message( message );
    return error;
}
}

void API_HANDLER_SCH::registerNetRenameHandlers()
{
    registerHandler<PlanNetRename, PlanNetRenameResponse>( &API_HANDLER_SCH::handlePlanNetRename );
    registerHandler<RenameNet, RenameNetResponse>( &API_HANDLER_SCH::handleRenameNet );
}

HANDLER_RESULT<PlanNetRenameResponse> API_HANDLER_SCH::handlePlanNetRename(
        const HANDLER_CONTEXT<PlanNetRename>& ctx )
{
    auto snapshot = connectivitySnapshot( ctx.Request.document() );
    if( !snapshot ) return tl::unexpected( snapshot.error() );
    if( ctx.Request.session_id().empty() || !ctx.Request.revision()
        || ctx.Request.session_id() != snapshot->session_id() || ctx.Request.revision() != snapshot->revision() )
        return tl::unexpected( renameError( "Stale or missing connectivity snapshot; read the net again before planning" ) );
    auto* frame = static_cast<SCH_EDIT_FRAME*>( m_frame );
    auto analysis = PlanSchematicNetRename( frame->Schematic(), *snapshot, ctx.Request.net_id(), ctx.Request.new_name() );
    // A blocked or subsequent preview invalidates an older plan for this client.
    m_netRenamePlans.erase( ctx.ClientName );
    if( analysis.plan.can_apply() )
    {
        analysis.plan.set_id( KIID().AsStdString() );
        if( m_netRenamePlans.size() >= 32 ) m_netRenamePlans.clear();
        m_netRenamePlans[ctx.ClientName] = analysis.plan;
    }
    PlanNetRenameResponse response;
    *response.mutable_plan() = std::move( analysis.plan );
    return response;
}

HANDLER_RESULT<RenameNetResponse> API_HANDLER_SCH::handleRenameNet( const HANDLER_CONTEXT<RenameNet>& ctx )
{
    auto snapshot = connectivitySnapshot( ctx.Request.document() );
    if( !snapshot ) return tl::unexpected( snapshot.error() );
    auto found = m_netRenamePlans.find( ctx.ClientName );
    if( found == m_netRenamePlans.end() || ctx.Request.plan_id().empty()
        || found->second.id() != ctx.Request.plan_id() )
        return tl::unexpected( renameError( "Rename plan not found for this client; request a new preview" ) );
    const auto plan = found->second;
    if( plan.document().SerializeAsString() != snapshot->document().SerializeAsString()
        || plan.session_id() != snapshot->session_id() || plan.revision() != snapshot->revision() )
        return tl::unexpected( renameError( "Stale rename plan; request a preview for the current snapshot" ) );
    if( !m_commits.empty() )
        return tl::unexpected( renameError( "Finish pending API transactions first; RenameNet creates one independent Undo step" ) );
    auto* frame = static_cast<SCH_EDIT_FRAME*>( m_frame );
    auto analysis = PlanSchematicNetRename( frame->Schematic(), *snapshot, plan.net_id(), plan.new_name() );
    analysis.plan.set_id( plan.id() );
    if( !analysis.plan.can_apply() || analysis.plan.SerializeAsString() != plan.SerializeAsString() )
        return tl::unexpected( renameError( "Rename plan changed or target name already exists; request a new preview" ) );

    SCH_COMMIT commit( frame );
    try
    {
        for( const auto& edit : analysis.edits ) commit.Modify( edit.item, edit.screen );
        for( const auto& edit : analysis.edits ) edit.item->SetText( edit.after );
        // Normal commit ownership handles sheet pins at their parent sheet level.
        // No cleanup: this operation changes only names, never wire geometry.
        commit.Push( _( "Renamed net via API" ), SCH_COMMIT_NO_CLEANUP );
        for( const auto& edit : analysis.edits ) edit.screen->SetContentModified();
    }
    catch( ... )
    {
        commit.Revert();
        throw;
    }
    m_netRenamePlans.clear();
    auto after = connectivitySnapshot( ctx.Request.document() );
    if( !after ) return tl::unexpected( after.error() );
    RenameNetResponse response;
    *response.mutable_plan() = plan;
    *response.mutable_snapshot() = std::move( *after );
    return response;
}
