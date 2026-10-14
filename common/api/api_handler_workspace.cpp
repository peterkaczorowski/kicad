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

#include <cstdint>

using namespace kiapi::workspace;


namespace
{
constexpr int BIND_TIMEOUT_MS = 8000;


void setError( WorkspaceError* aError, MDW_ERROR_CODE aCode )
{
    // MDW_ERROR_CODE and WorkspaceErrorCode share their numbering by contract.
    aError->set_code( static_cast<WorkspaceErrorCode>( aCode ) );

    // An IPC client sees only what rides this envelope, and the field was left
    // empty on every command. The backend's own wording does not reach here:
    // MDW_BACKEND::decode keeps the code from MdwResponse.error and drops the
    // message, and recovering it means threading a string back through all
    // nineteen service operations. The shared text at least tells a client what
    // happened rather than nothing.
    aError->set_message( std::string( MdwErrorMessage( aCode ).utf8_string() ) );
}


void packVersion( Version* aOut, const MDW_VERSION& aVersion )
{
    aOut->set_major( aVersion.major );
    aOut->set_minor( aVersion.minor );
    aOut->set_patch( aVersion.patch );
    aOut->set_full( std::string( aVersion.full.utf8_string() ) );
}


void packBackendInfo( BackendInfo* aOut, const MDW_BACKEND_INFO& aInfo )
{
    aOut->set_backend_id( std::string( aInfo.backend_id.utf8_string() ) );
    aOut->set_display_name( std::string( aInfo.display_name.utf8_string() ) );
    aOut->set_description( std::string( aInfo.description.utf8_string() ) );
    aOut->set_vendor( std::string( aInfo.vendor.utf8_string() ) );
    aOut->set_vendor_url( std::string( aInfo.vendor_url.utf8_string() ) );
    aOut->set_support_url( std::string( aInfo.support_url.utf8_string() ) );
    aOut->set_icon_name( std::string( aInfo.icon_name.utf8_string() ) );
    packVersion( aOut->mutable_version(), aInfo.version );
}


void packCapabilities( Capabilities* aOut, const MDW_CAPABILITIES& aCaps )
{
    for( MDW_CAPABILITY_GROUP group : aCaps.groups )
        aOut->add_groups( static_cast<CapabilityGroup>( group ) );

    aOut->set_supports_offline( aCaps.supports_offline );

    for( const MDW_CUSTOM_STATE& state : aCaps.custom_states )
    {
        CustomState* out = aOut->add_custom_states();
        out->set_state_id( std::string( state.state_id.utf8_string() ) );
        out->set_display_name( std::string( state.display_name.utf8_string() ) );
        out->set_description( std::string( state.description.utf8_string() ) );
        out->set_category( static_cast<LifecycleCategory>( state.category ) );
        out->set_icon_name( std::string( state.icon_name.utf8_string() ) );
        out->set_sort_order( state.sort_order );
    }
}


void packItemInfo( ItemInfo* aOut, const MDW_ITEM_INFO& aInfo )
{
    aOut->set_item_id( std::string( aInfo.item_id.utf8_string() ) );
    aOut->set_display_name( std::string( aInfo.display_name.utf8_string() ) );
    aOut->set_folder_id( std::string( aInfo.folder_id.utf8_string() ) );
    aOut->set_is_folder( aInfo.is_folder );

    if( aInfo.modified.IsValid() )
        aOut->set_modified_unix( aInfo.modified.GetTicks() );

    aOut->set_revision_id( std::string( aInfo.revision_id.utf8_string() ) );
    aOut->set_category( static_cast<LifecycleCategory>( aInfo.category ) );
    aOut->set_custom_state_id( std::string( aInfo.custom_state_id.utf8_string() ) );
    aOut->set_is_locked( aInfo.is_locked );
    aOut->set_lock_owner( std::string( aInfo.lock_owner.utf8_string() ) );
}


void packLockInfo( LockInfo* aOut, const MDW_LOCK_INFO& aLock )
{
    aOut->set_is_locked( aLock.is_locked );
    aOut->set_lock_owner( std::string( aLock.lock_owner.utf8_string() ) );

    if( aLock.lock_timestamp.IsValid() )
        aOut->set_lock_timestamp_unix( aLock.lock_timestamp.GetTicks() );

    aOut->set_locked_by_me( aLock.locked_by_me );
}


void packRevision( Revision* aOut, const MDW_REVISION& aRev )
{
    aOut->set_revision_id( std::string( aRev.revision_id.utf8_string() ) );

    for( const wxString& parent : aRev.parent_revision_ids )
        aOut->add_parent_revision_ids( std::string( parent.utf8_string() ) );

    aOut->set_author( std::string( aRev.author.utf8_string() ) );

    if( aRev.timestamp.IsValid() )
        aOut->set_timestamp_unix( aRev.timestamp.GetTicks() );

    aOut->set_comment( std::string( aRev.comment.utf8_string() ) );
}


void packAttributeSet( AttributeSet* aOut, const MDW_ATTRIBUTE_SET& aSet )
{
    for( const MDW_ATTRIBUTE_VALUE& value : aSet.values )
    {
        AttributeValue* out = aOut->add_values();
        out->set_attr_id( std::string( value.attr_id.utf8_string() ) );
        out->set_value( std::string( value.value.utf8_string() ) );
    }
}


MDW_ATTRIBUTE_SET toStruct( const AttributeSet& aProto )
{
    MDW_ATTRIBUTE_SET set;

    for( const AttributeValue& value : aProto.values() )
        set.values.push_back( { wxString::FromUTF8( value.attr_id() ),
                                wxString::FromUTF8( value.value() ) } );

    return set;
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
    std::vector<std::string> types;

    for( const auto& [typeName, handler] : m_handlers )
        types.push_back( typeName );

    return types;
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
    reply.set_workspace_id( std::string( session->Binding().workspace_id.utf8_string() ) );
    packBackendInfo( reply.mutable_backend_info(), session->Backend()->Info() );
    packCapabilities( reply.mutable_capabilities(), session->Backend()->Capabilities() );

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
    binding.backend_id = wxString::FromUTF8( aCtx.Request.backend_id() );
    binding.workspace_id = wxString::FromUTF8( aCtx.Request.workspace_id() );
    binding.binding = wxString::FromUTF8( aCtx.Request.binding() );

    MDW_SESSION* session =
            fw->OpenSession( activeProjectPath(), binding, GetBuildVersion(), BIND_TIMEOUT_MS );

    if( !session || !session->Backend() )
    {
        setError( reply.mutable_error(), MDW_ERROR_CODE::BACKEND_LOST );
        return reply;
    }

    packBackendInfo( reply.mutable_backend_info(), session->Backend()->Info() );
    packCapabilities( reply.mutable_capabilities(), session->Backend()->Capabilities() );

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
    MDW_ERROR_CODE code = service->ListItems( wxString::FromUTF8( aCtx.Request.folder_id() ), items );

    if( code == MDW_ERROR_CODE::SUCCESS )
    {
        for( const MDW_ITEM_INFO& item : items )
            packItemInfo( reply.add_items(), item );
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
    MDW_ERROR_CODE code = service->GetItemInfo( wxString::FromUTF8( aCtx.Request.item_id() ), item );

    if( code == MDW_ERROR_CODE::SUCCESS )
        packItemInfo( reply.mutable_item(), item );

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
    request.item_id = wxString::FromUTF8( aCtx.Request.item_id() );
    request.local_path = wxString::FromUTF8( aCtx.Request.local_path() );
    request.lock = aCtx.Request.lock();

    MDW_CHECKOUT_RESULT result;
    MDW_ERROR_CODE      code = service->Checkout( request, result );

    if( code == MDW_ERROR_CODE::SUCCESS )
    {
        packItemInfo( reply.mutable_item(), result.item );
        reply.set_local_path( std::string( result.local_path.utf8_string() ) );
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
    request.item_id = wxString::FromUTF8( aCtx.Request.item_id() );
    request.local_path = wxString::FromUTF8( aCtx.Request.local_path() );
    request.comment = wxString::FromUTF8( aCtx.Request.comment() );
    request.attributes = toStruct( aCtx.Request.attributes() );

    MDW_CHECKIN_RESULT result;
    MDW_ERROR_CODE     code = service->CheckinSync( request, result );

    if( code == MDW_ERROR_CODE::SUCCESS )
        packRevision( reply.mutable_revision(), result.revision );

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

    MDW_ERROR_CODE code = service->CancelCheckout( wxString::FromUTF8( aCtx.Request.item_id() ),
                                                   wxString::FromUTF8( aCtx.Request.local_path() ) );
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
    MDW_ERROR_CODE code = service->LockSync( wxString::FromUTF8( aCtx.Request.item_id() ), lock );

    if( code == MDW_ERROR_CODE::SUCCESS )
        packLockInfo( reply.mutable_lock(), lock );

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
    MDW_ERROR_CODE code = service->UnlockSync( wxString::FromUTF8( aCtx.Request.item_id() ), lock );

    if( code == MDW_ERROR_CODE::SUCCESS )
        packLockInfo( reply.mutable_lock(), lock );

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
    MDW_ERROR_CODE code = service->GetLockInfo( wxString::FromUTF8( aCtx.Request.item_id() ), lock );

    if( code == MDW_ERROR_CODE::SUCCESS )
        packLockInfo( reply.mutable_lock(), lock );

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
    MDW_ERROR_CODE code = service->GetAttributes( wxString::FromUTF8( aCtx.Request.item_id() ),
                                                  attributes );

    if( code == MDW_ERROR_CODE::SUCCESS )
        packAttributeSet( reply.mutable_attributes(), attributes );

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

    MDW_ERROR_CODE code = service->SetAttributes( wxString::FromUTF8( aCtx.Request.item_id() ),
                                                  toStruct( aCtx.Request.attributes() ) );
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
    MDW_ERROR_CODE code = service->GetRevisionHistory( wxString::FromUTF8( aCtx.Request.item_id() ),
                                                       revisions );

    if( code == MDW_ERROR_CODE::SUCCESS )
    {
        for( const MDW_REVISION& revision : revisions )
            packRevision( reply.add_revisions(), revision );
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

    MDW_ERROR_CODE code = service->DownloadRevision( wxString::FromUTF8( aCtx.Request.item_id() ),
                                                     wxString::FromUTF8( aCtx.Request.revision_id() ),
                                                     wxString::FromUTF8( aCtx.Request.local_path() ) );
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
    MDW_ERROR_CODE code = service->GetLifecycleCategory( wxString::FromUTF8( aCtx.Request.item_id() ),
                                                         category, customStateId );

    if( code == MDW_ERROR_CODE::SUCCESS )
    {
        reply.set_category( static_cast<LifecycleCategory>( category ) );
        reply.set_custom_state_id( std::string( customStateId.utf8_string() ) );
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
            wxString::FromUTF8( aCtx.Request.item_id() ),
            wxString::FromUTF8( aCtx.Request.target_state_id() ),
            wxString::FromUTF8( aCtx.Request.comment() ), category, stateId );

    if( code == MDW_ERROR_CODE::SUCCESS )
    {
        reply.set_new_category( static_cast<LifecycleCategory>( category ) );
        reply.set_new_state_id( std::string( stateId.utf8_string() ) );
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

    reply.set_event_socket_url( std::string( url.utf8_string() ) );
    reply.set_topic_prefix( std::string( topicPrefix.utf8_string() ) );
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
    action.client_identity = wxString::FromUTF8( aCtx.ClientName );
    action.item_type_predicate = wxString::FromUTF8( spec.item_type_predicate() );
    action.label = wxString::FromUTF8( spec.label() );
    action.target_method = wxString::FromUTF8( spec.target_method() );

    if( !fw->ContextActions().Register( action ) )
    {
        setError( reply.mutable_error(), MDW_ERROR_CODE::PERMISSION_DENIED );
        return reply;
    }

    reply.set_action_id( std::string( action.action_id.utf8_string() ) );

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

    if( !fw->ContextActions().Unregister( wxString::FromUTF8( aCtx.Request.action_id() ),
                                          wxString::FromUTF8( aCtx.ClientName ) ) )
    {
        setError( reply.mutable_error(), MDW_ERROR_CODE::ITEM_NOT_FOUND );
    }

    return reply;
}
