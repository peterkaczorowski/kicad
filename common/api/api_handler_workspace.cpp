/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
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

#include <api/api_handler_workspace.h>

#include <build_version.h>
#include <pgm_base.h>
#include <project.h>
#include <settings/settings_manager.h>

#include <mdw/mdw_framework.h>
#include <mdw/mdw_session.h>
#include <mdw/mdw_workspace_service.h>
#include <mdw/mdw_context_action_registry.h>
#include <mdw/mdw_backend.h>
#include <mdw/mdw_proto_convert.h>

#include <cstdint>
#include <ranges>

using namespace kiapi::workspace;


namespace
{
constexpr int BIND_TIMEOUT_MS = 8000;


void setError( WorkspaceError* aError, MDW_ERROR_CODE aCode )
{
    // MDW_ERROR_CODE and WorkspaceErrorCode share their numbering by contract.
    aError->set_code( static_cast<WorkspaceErrorCode>( aCode ) );

    // An IPC client sees only what rides this envelope, and the field was left
    // empty on every command. The backend's own wording does not reach here,
    // because MDW_BACKEND::decode keeps the code from MdwResponse.error and
    // drops the message, and recovering it means threading a string back through
    // all nineteen service operations. The shared text at least tells a client
    // what happened rather than nothing.
    aError->set_message( MdwToUtf8( MdwErrorMessage( aCode ) ) );
}


wxString activeProjectPath()
{
    return Pgm().GetSettingsManager().Prj().GetProjectFullName();
}
}


API_HANDLER_WORKSPACE::API_HANDLER_WORKSPACE() :
        API_HANDLER()
{
    registerHandler<GetWorkspaceInfo, GetWorkspaceInfoResponse>(
            &API_HANDLER_WORKSPACE::handleGetWorkspaceInfo );
    registerHandler<BindWorkspace, BindWorkspaceResponse>(
            &API_HANDLER_WORKSPACE::handleBindWorkspace );
    registerHandler<UnbindWorkspace, UnbindWorkspaceResponse>(
            &API_HANDLER_WORKSPACE::handleUnbindWorkspace );
    registerHandler<ListWorkspaceItems, ListWorkspaceItemsResponse>(
            &API_HANDLER_WORKSPACE::handleListWorkspaceItems );
    registerHandler<GetWorkspaceItemInfo, GetWorkspaceItemInfoResponse>(
            &API_HANDLER_WORKSPACE::handleGetWorkspaceItemInfo );
    registerHandler<CheckoutItem, CheckoutItemResponse>(
            &API_HANDLER_WORKSPACE::handleCheckoutItem );
    registerHandler<CheckinItem, CheckinItemResponse>(
            &API_HANDLER_WORKSPACE::handleCheckinItem );
    registerHandler<CancelCheckout, CancelCheckoutResponse>(
            &API_HANDLER_WORKSPACE::handleCancelCheckout );
    registerHandler<LockItem, LockItemResponse>( &API_HANDLER_WORKSPACE::handleLockItem );
    registerHandler<UnlockItem, LockItemResponse>( &API_HANDLER_WORKSPACE::handleUnlockItem );
    registerHandler<GetItemLockInfo, GetItemLockInfoResponse>(
            &API_HANDLER_WORKSPACE::handleGetItemLockInfo );
    registerHandler<GetItemAttributes, GetItemAttributesResponse>(
            &API_HANDLER_WORKSPACE::handleGetItemAttributes );
    registerHandler<SetItemAttributes, SetItemAttributesResponse>(
            &API_HANDLER_WORKSPACE::handleSetItemAttributes );
    registerHandler<GetRevisionHistory, GetRevisionHistoryResponse>(
            &API_HANDLER_WORKSPACE::handleGetRevisionHistory );
    registerHandler<DownloadRevision, DownloadRevisionResponse>(
            &API_HANDLER_WORKSPACE::handleDownloadRevision );
    registerHandler<GetLifecycleCategory, GetLifecycleCategoryResponse>(
            &API_HANDLER_WORKSPACE::handleGetLifecycleCategory );
    registerHandler<TransitionLifecycle, TransitionLifecycleResponse>(
            &API_HANDLER_WORKSPACE::handleTransitionLifecycle );
    registerHandler<SubscribeWorkspaceEvents, SubscribeWorkspaceEventsResponse>(
            &API_HANDLER_WORKSPACE::handleSubscribeWorkspaceEvents );
    registerHandler<RegisterContextAction, RegisterContextActionResponse>(
            &API_HANDLER_WORKSPACE::handleRegisterContextAction );
    registerHandler<UnregisterContextAction, UnregisterContextActionResponse>(
            &API_HANDLER_WORKSPACE::handleUnregisterContextAction );
}


std::vector<std::string> API_HANDLER_WORKSPACE::HandledTypes() const
{
    auto typeNames = m_handlers | std::views::keys;

    return std::vector<std::string>( typeNames.begin(), typeNames.end() );
}


MDW_WORKSPACE_SERVICE* API_HANDLER_WORKSPACE::sessionService( WorkspaceError* aError )
{
    MDW_FRAMEWORK* fw = Pgm().MdwFrameworkOrNull();

    if( !fw )
    {
        setError( aError, MDW_ERROR_CODE::NOT_AUTHENTICATED );
        return nullptr;
    }

    MDW_SESSION* session = fw->Session( activeProjectPath() );

    if( !session )
    {
        setError( aError, MDW_ERROR_CODE::NOT_AUTHENTICATED );
        return nullptr;
    }

    MDW_WORKSPACE_SERVICE* service = session->Service();

    if( !service )
        setError( aError, MDW_ERROR_CODE::BACKEND_LOST );

    return service;
}


HANDLER_RESULT<GetWorkspaceInfoResponse> API_HANDLER_WORKSPACE::handleGetWorkspaceInfo(
        const HANDLER_CONTEXT<GetWorkspaceInfo>& aCtx )
{
    GetWorkspaceInfoResponse reply;
    MDW_FRAMEWORK*           fw = Pgm().MdwFrameworkOrNull();

    if( !fw )
    {
        reply.set_bound( false );
        return reply;
    }

    MDW_SESSION* session = fw->Session( activeProjectPath() );

    if( !session || !session->Backend() )
    {
        reply.set_bound( false );
        return reply;
    }

    reply.set_bound( true );
    reply.set_workspace_id( MdwToUtf8( session->Binding().workspace_id ) );
    MdwToProto( reply.mutable_backend_info(), session->Backend()->Info() );
    MdwToProto( reply.mutable_capabilities(), session->Backend()->Capabilities() );

    return reply;
}


HANDLER_RESULT<BindWorkspaceResponse> API_HANDLER_WORKSPACE::handleBindWorkspace(
        const HANDLER_CONTEXT<BindWorkspace>& aCtx )
{
    BindWorkspaceResponse reply;
    MDW_FRAMEWORK*        fw = Pgm().MdwFrameworkOrNull();

    if( !fw )
    {
        setError( reply.mutable_error(), MDW_ERROR_CODE::INTERNAL );
        return reply;
    }

    MDW_PROJECT_BINDING binding;
    binding.backend_id = MdwFromUtf8( aCtx.Request.backend_id() );
    binding.workspace_id = MdwFromUtf8( aCtx.Request.workspace_id() );
    binding.binding = MdwFromUtf8( aCtx.Request.binding() );

    MDW_SESSION* session =
            fw->OpenSession( activeProjectPath(), binding, GetBuildVersion(), BIND_TIMEOUT_MS );

    if( !session || !session->Backend() )
    {
        setError( reply.mutable_error(), MDW_ERROR_CODE::BACKEND_LOST );
        return reply;
    }

    MdwToProto( reply.mutable_backend_info(), session->Backend()->Info() );
    MdwToProto( reply.mutable_capabilities(), session->Backend()->Capabilities() );

    return reply;
}


HANDLER_RESULT<UnbindWorkspaceResponse> API_HANDLER_WORKSPACE::handleUnbindWorkspace(
        const HANDLER_CONTEXT<UnbindWorkspace>& aCtx )
{
    UnbindWorkspaceResponse reply;
    MDW_FRAMEWORK*          fw = Pgm().MdwFrameworkOrNull();

    if( fw )
        fw->CloseSession( activeProjectPath() );

    return reply;
}


HANDLER_RESULT<ListWorkspaceItemsResponse> API_HANDLER_WORKSPACE::handleListWorkspaceItems(
        const HANDLER_CONTEXT<ListWorkspaceItems>& aCtx )
{
    ListWorkspaceItemsResponse reply;
    MDW_WORKSPACE_SERVICE*     service = sessionService( reply.mutable_error() );

    if( !service )
        return reply;

    std::vector<MDW_ITEM_INFO> items;
    MDW_ERROR_CODE code = service->ListItems( MdwFromUtf8( aCtx.Request.folder_id() ), items );

    if( code == MDW_ERROR_CODE::SUCCESS )
    {
        for( const MDW_ITEM_INFO& item : items )
            MdwToProto( reply.add_items(), item );
    }

    setError( reply.mutable_error(), code );

    return reply;
}


HANDLER_RESULT<GetWorkspaceItemInfoResponse> API_HANDLER_WORKSPACE::handleGetWorkspaceItemInfo(
        const HANDLER_CONTEXT<GetWorkspaceItemInfo>& aCtx )
{
    GetWorkspaceItemInfoResponse reply;
    MDW_WORKSPACE_SERVICE*       service = sessionService( reply.mutable_error() );

    if( !service )
        return reply;

    MDW_ITEM_INFO  item;
    MDW_ERROR_CODE code = service->GetItemInfo( MdwFromUtf8( aCtx.Request.item_id() ), item );

    if( code == MDW_ERROR_CODE::SUCCESS )
        MdwToProto( reply.mutable_item(), item );

    setError( reply.mutable_error(), code );

    return reply;
}


HANDLER_RESULT<CheckoutItemResponse> API_HANDLER_WORKSPACE::handleCheckoutItem(
        const HANDLER_CONTEXT<CheckoutItem>& aCtx )
{
    CheckoutItemResponse   reply;
    MDW_WORKSPACE_SERVICE* service = sessionService( reply.mutable_error() );

    if( !service )
        return reply;

    MDW_CHECKOUT_REQUEST request;
    request.item_id = MdwFromUtf8( aCtx.Request.item_id() );
    request.local_path = MdwFromUtf8( aCtx.Request.local_path() );
    request.lock = aCtx.Request.lock();

    MDW_CHECKOUT_RESULT result;
    MDW_ERROR_CODE      code = service->Checkout( request, result );

    if( code == MDW_ERROR_CODE::SUCCESS )
    {
        MdwToProto( reply.mutable_item(), result.item );
        reply.set_local_path( MdwToUtf8( result.local_path ) );
    }

    setError( reply.mutable_error(), code );

    return reply;
}


HANDLER_RESULT<CheckinItemResponse> API_HANDLER_WORKSPACE::handleCheckinItem(
        const HANDLER_CONTEXT<CheckinItem>& aCtx )
{
    CheckinItemResponse    reply;
    MDW_WORKSPACE_SERVICE* service = sessionService( reply.mutable_error() );

    if( !service )
        return reply;

    MDW_CHECKIN_REQUEST request;
    request.item_id = MdwFromUtf8( aCtx.Request.item_id() );
    request.local_path = MdwFromUtf8( aCtx.Request.local_path() );
    request.comment = MdwFromUtf8( aCtx.Request.comment() );
    request.attributes = MdwToStruct( aCtx.Request.attributes() );

    MDW_CHECKIN_RESULT result;
    MDW_ERROR_CODE     code = service->CheckinSync( request, result );

    if( code == MDW_ERROR_CODE::SUCCESS )
        MdwToProto( reply.mutable_revision(), result.revision );

    setError( reply.mutable_error(), code );

    return reply;
}


HANDLER_RESULT<CancelCheckoutResponse> API_HANDLER_WORKSPACE::handleCancelCheckout(
        const HANDLER_CONTEXT<CancelCheckout>& aCtx )
{
    CancelCheckoutResponse reply;
    MDW_WORKSPACE_SERVICE* service = sessionService( reply.mutable_error() );

    if( !service )
        return reply;

    MDW_ERROR_CODE code = service->CancelCheckout( MdwFromUtf8( aCtx.Request.item_id() ),
                                                   MdwFromUtf8( aCtx.Request.local_path() ) );
    setError( reply.mutable_error(), code );

    return reply;
}


HANDLER_RESULT<LockItemResponse> API_HANDLER_WORKSPACE::handleLockItem(
        const HANDLER_CONTEXT<LockItem>& aCtx )
{
    LockItemResponse       reply;
    MDW_WORKSPACE_SERVICE* service = sessionService( reply.mutable_error() );

    if( !service )
        return reply;

    MDW_LOCK_INFO  lock;
    MDW_ERROR_CODE code = service->LockSync( MdwFromUtf8( aCtx.Request.item_id() ), lock );

    if( code == MDW_ERROR_CODE::SUCCESS )
        MdwToProto( reply.mutable_lock(), lock );

    setError( reply.mutable_error(), code );

    return reply;
}


HANDLER_RESULT<LockItemResponse> API_HANDLER_WORKSPACE::handleUnlockItem(
        const HANDLER_CONTEXT<UnlockItem>& aCtx )
{
    LockItemResponse       reply;
    MDW_WORKSPACE_SERVICE* service = sessionService( reply.mutable_error() );

    if( !service )
        return reply;

    MDW_LOCK_INFO  lock;
    MDW_ERROR_CODE code = service->UnlockSync( MdwFromUtf8( aCtx.Request.item_id() ), lock );

    if( code == MDW_ERROR_CODE::SUCCESS )
        MdwToProto( reply.mutable_lock(), lock );

    setError( reply.mutable_error(), code );

    return reply;
}


HANDLER_RESULT<GetItemLockInfoResponse> API_HANDLER_WORKSPACE::handleGetItemLockInfo(
        const HANDLER_CONTEXT<GetItemLockInfo>& aCtx )
{
    GetItemLockInfoResponse reply;
    MDW_WORKSPACE_SERVICE*  service = sessionService( reply.mutable_error() );

    if( !service )
        return reply;

    MDW_LOCK_INFO  lock;
    MDW_ERROR_CODE code = service->GetLockInfo( MdwFromUtf8( aCtx.Request.item_id() ), lock );

    if( code == MDW_ERROR_CODE::SUCCESS )
        MdwToProto( reply.mutable_lock(), lock );

    setError( reply.mutable_error(), code );

    return reply;
}


HANDLER_RESULT<GetItemAttributesResponse> API_HANDLER_WORKSPACE::handleGetItemAttributes(
        const HANDLER_CONTEXT<GetItemAttributes>& aCtx )
{
    GetItemAttributesResponse reply;
    MDW_WORKSPACE_SERVICE*    service = sessionService( reply.mutable_error() );

    if( !service )
        return reply;

    MDW_ATTRIBUTE_SET attributes;
    MDW_ERROR_CODE code = service->GetAttributes( MdwFromUtf8( aCtx.Request.item_id() ),
                                                  attributes );

    if( code == MDW_ERROR_CODE::SUCCESS )
        MdwToProto( reply.mutable_attributes(), attributes );

    setError( reply.mutable_error(), code );

    return reply;
}


HANDLER_RESULT<SetItemAttributesResponse> API_HANDLER_WORKSPACE::handleSetItemAttributes(
        const HANDLER_CONTEXT<SetItemAttributes>& aCtx )
{
    SetItemAttributesResponse reply;
    MDW_WORKSPACE_SERVICE*    service = sessionService( reply.mutable_error() );

    if( !service )
        return reply;

    MDW_ERROR_CODE code = service->SetAttributes( MdwFromUtf8( aCtx.Request.item_id() ),
                                                  MdwToStruct( aCtx.Request.attributes() ) );
    setError( reply.mutable_error(), code );

    return reply;
}


HANDLER_RESULT<GetRevisionHistoryResponse> API_HANDLER_WORKSPACE::handleGetRevisionHistory(
        const HANDLER_CONTEXT<GetRevisionHistory>& aCtx )
{
    GetRevisionHistoryResponse reply;
    MDW_WORKSPACE_SERVICE*     service = sessionService( reply.mutable_error() );

    if( !service )
        return reply;

    std::vector<MDW_REVISION> revisions;
    MDW_ERROR_CODE code = service->GetRevisionHistory( MdwFromUtf8( aCtx.Request.item_id() ),
                                                       revisions );

    if( code == MDW_ERROR_CODE::SUCCESS )
    {
        for( const MDW_REVISION& revision : revisions )
            MdwToProto( reply.add_revisions(), revision );
    }

    setError( reply.mutable_error(), code );

    return reply;
}


HANDLER_RESULT<DownloadRevisionResponse> API_HANDLER_WORKSPACE::handleDownloadRevision(
        const HANDLER_CONTEXT<DownloadRevision>& aCtx )
{
    DownloadRevisionResponse reply;
    MDW_WORKSPACE_SERVICE*   service = sessionService( reply.mutable_error() );

    if( !service )
        return reply;

    MDW_ERROR_CODE code = service->DownloadRevision( MdwFromUtf8( aCtx.Request.item_id() ),
                                                     MdwFromUtf8( aCtx.Request.revision_id() ),
                                                     MdwFromUtf8( aCtx.Request.local_path() ) );
    setError( reply.mutable_error(), code );

    return reply;
}


HANDLER_RESULT<GetLifecycleCategoryResponse> API_HANDLER_WORKSPACE::handleGetLifecycleCategory(
        const HANDLER_CONTEXT<GetLifecycleCategory>& aCtx )
{
    GetLifecycleCategoryResponse reply;
    MDW_WORKSPACE_SERVICE*       service = sessionService( reply.mutable_error() );

    if( !service )
        return reply;

    MDW_LIFECYCLE_CATEGORY category = MDW_LIFECYCLE_CATEGORY::EDITABLE;
    wxString               customStateId;
    MDW_ERROR_CODE code = service->GetLifecycleCategory( MdwFromUtf8( aCtx.Request.item_id() ),
                                                         category, customStateId );

    if( code == MDW_ERROR_CODE::SUCCESS )
    {
        reply.set_category( static_cast<LifecycleCategory>( category ) );
        reply.set_custom_state_id( MdwToUtf8( customStateId ) );
    }

    setError( reply.mutable_error(), code );

    return reply;
}


HANDLER_RESULT<TransitionLifecycleResponse> API_HANDLER_WORKSPACE::handleTransitionLifecycle(
        const HANDLER_CONTEXT<TransitionLifecycle>& aCtx )
{
    TransitionLifecycleResponse reply;
    MDW_WORKSPACE_SERVICE*      service = sessionService( reply.mutable_error() );

    if( !service )
        return reply;

    MDW_LIFECYCLE_CATEGORY category = MDW_LIFECYCLE_CATEGORY::EDITABLE;
    wxString               stateId;
    MDW_ERROR_CODE code = service->TransitionLifecycle(
            MdwFromUtf8( aCtx.Request.item_id() ),
            MdwFromUtf8( aCtx.Request.target_state_id() ),
            MdwFromUtf8( aCtx.Request.comment() ), category, stateId );

    if( code == MDW_ERROR_CODE::SUCCESS )
    {
        reply.set_new_category( static_cast<LifecycleCategory>( category ) );
        reply.set_new_state_id( MdwToUtf8( stateId ) );
    }

    setError( reply.mutable_error(), code );

    return reply;
}


HANDLER_RESULT<SubscribeWorkspaceEventsResponse>
API_HANDLER_WORKSPACE::handleSubscribeWorkspaceEvents(
        const HANDLER_CONTEXT<SubscribeWorkspaceEvents>& aCtx )
{
    SubscribeWorkspaceEventsResponse reply;
    MDW_FRAMEWORK*                   fw = Pgm().MdwFrameworkOrNull();

    if( !fw )
    {
        setError( reply.mutable_error(), MDW_ERROR_CODE::NOT_AUTHENTICATED );
        return reply;
    }

    MDW_SESSION* session = fw->Session( activeProjectPath() );

    if( !session )
    {
        setError( reply.mutable_error(), MDW_ERROR_CODE::NOT_AUTHENTICATED );
        return reply;
    }

    // A backend that does not advertise events has no stream to subscribe to.
    if( !session->Backend() || !session->Backend()->Has( MDW_CAPABILITY_GROUP::EVENTS ) )
    {
        setError( reply.mutable_error(), MDW_ERROR_CODE::NOT_SUPPORTED );
        return reply;
    }

    wxString      url;
    wxString      topicPrefix;
    std::uint64_t seq = 0;

    if( !session->SubscribeEvents( url, topicPrefix, seq ) )
    {
        setError( reply.mutable_error(), MDW_ERROR_CODE::BACKEND_LOST );
        return reply;
    }

    reply.set_event_socket_url( MdwToUtf8( url ) );
    reply.set_topic_prefix( MdwToUtf8( topicPrefix ) );
    reply.set_current_seq( seq );

    return reply;
}


HANDLER_RESULT<RegisterContextActionResponse> API_HANDLER_WORKSPACE::handleRegisterContextAction(
        const HANDLER_CONTEXT<RegisterContextAction>& aCtx )
{
    RegisterContextActionResponse reply;
    MDW_FRAMEWORK*                fw = Pgm().MdwFrameworkOrNull();

    if( !fw )
    {
        setError( reply.mutable_error(), MDW_ERROR_CODE::NOT_AUTHENTICATED );
        return reply;
    }

    const ContextActionSpec& spec = aCtx.Request.spec();

    MDW_CONTEXT_ACTION action;
    action.client_identity = MdwFromUtf8( aCtx.ClientName );
    action.item_type_predicate = MdwFromUtf8( spec.item_type_predicate() );
    action.label = MdwFromUtf8( spec.label() );
    action.target_method = MdwFromUtf8( spec.target_method() );

    if( !fw->ContextActions().Register( action ) )
    {
        setError( reply.mutable_error(), MDW_ERROR_CODE::PERMISSION_DENIED );
        return reply;
    }

    reply.set_action_id( MdwToUtf8( action.action_id ) );

    return reply;
}


HANDLER_RESULT<UnregisterContextActionResponse> API_HANDLER_WORKSPACE::handleUnregisterContextAction(
        const HANDLER_CONTEXT<UnregisterContextAction>& aCtx )
{
    UnregisterContextActionResponse reply;
    MDW_FRAMEWORK*                  fw = Pgm().MdwFrameworkOrNull();

    if( !fw )
    {
        setError( reply.mutable_error(), MDW_ERROR_CODE::NOT_AUTHENTICATED );
        return reply;
    }

    if( !fw->ContextActions().Unregister( MdwFromUtf8( aCtx.Request.action_id() ),
                                          MdwFromUtf8( aCtx.ClientName ) ) )
    {
        setError( reply.mutable_error(), MDW_ERROR_CODE::ITEM_NOT_FOUND );
    }

    return reply;
}
