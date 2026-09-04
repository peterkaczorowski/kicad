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

#include <mdw/mdw_workspace_service.h>

#include <api/mdw/backend_base.pb.h>
#include <api/mdw/backend_locking.pb.h>
#include <api/mdw/backend_revisions.pb.h>
#include <api/mdw/backend_attributes.pb.h>
#include <api/mdw/backend_lifecycle.pb.h>

#include <kiid.h>

namespace
{
constexpr int OPERATION_TIMEOUT_MS = 60000;


MDW_LOCK_INFO toStruct( const kiapi::workspace::LockInfo& aProto )
{
    MDW_LOCK_INFO info;
    info.is_locked = aProto.is_locked();
    info.lock_owner = wxString::FromUTF8( aProto.lock_owner() );
    info.locked_by_me = aProto.locked_by_me();

    if( aProto.lock_timestamp_unix() != 0 )
        info.lock_timestamp = wxDateTime( static_cast<time_t>( aProto.lock_timestamp_unix() ) );

    return info;
}


MDW_REVISION toStruct( const kiapi::workspace::Revision& aProto )
{
    MDW_REVISION revision;
    revision.revision_id = wxString::FromUTF8( aProto.revision_id() );
    revision.author = wxString::FromUTF8( aProto.author() );
    revision.comment = wxString::FromUTF8( aProto.comment() );

    if( aProto.timestamp_unix() != 0 )
        revision.timestamp = wxDateTime( static_cast<time_t>( aProto.timestamp_unix() ) );

    for( const std::string& parent : aProto.parent_revision_ids() )
        revision.parent_revision_ids.push_back( wxString::FromUTF8( parent ) );

    return revision;
}


MDW_ITEM_INFO toStruct( const kiapi::workspace::ItemInfo& aProto )
{
    MDW_ITEM_INFO info;
    info.item_id = wxString::FromUTF8( aProto.item_id() );
    info.display_name = wxString::FromUTF8( aProto.display_name() );
    info.folder_id = wxString::FromUTF8( aProto.folder_id() );
    info.is_folder = aProto.is_folder();

    if( aProto.modified_unix() != 0 )
        info.modified = wxDateTime( static_cast<time_t>( aProto.modified_unix() ) );

    info.revision_id = wxString::FromUTF8( aProto.revision_id() );
    info.category = static_cast<MDW_LIFECYCLE_CATEGORY>( aProto.category() );
    info.custom_state_id = wxString::FromUTF8( aProto.custom_state_id() );
    info.is_locked = aProto.is_locked();
    info.lock_owner = wxString::FromUTF8( aProto.lock_owner() );

    return info;
}


MDW_FOLDER toStruct( const kiapi::workspace::Folder& aProto )
{
    MDW_FOLDER folder;
    folder.folder_id = wxString::FromUTF8( aProto.folder_id() );
    folder.display_name = wxString::FromUTF8( aProto.display_name() );
    folder.parent_folder_id = wxString::FromUTF8( aProto.parent_folder_id() );
    folder.has_children = aProto.has_children();

    return folder;
}


MDW_ATTRIBUTE_SET toStruct( const kiapi::workspace::AttributeSet& aProto )
{
    MDW_ATTRIBUTE_SET set;

    for( const kiapi::workspace::AttributeValue& value : aProto.values() )
        set.values.push_back( { wxString::FromUTF8( value.attr_id() ),
                                wxString::FromUTF8( value.value() ) } );

    return set;
}
}


MDW_WORKSPACE_SERVICE::MDW_WORKSPACE_SERVICE( MDW_BACKEND* aBackend, MDW_EVENT_BUS* aBus,
                                             MDW_LOCK_TRACKER* aLocks, MDW_DIRTY_TRACKER* aDirty,
                                             const wxString& aWorkspaceId,
                                             const wxString& aClientIdentity ) :
        m_backend( aBackend ),
        m_bus( aBus ),
        m_locks( aLocks ),
        m_dirty( aDirty ),
        m_workspaceId( aWorkspaceId ),
        m_clientIdentity( aClientIdentity )
{
}


bool MDW_WORKSPACE_SERVICE::Supports( MDW_CAPABILITY_GROUP aGroup ) const
{
    return m_backend && m_backend->Has( aGroup );
}


void MDW_WORKSPACE_SERVICE::applyLock( const wxString& aItemId, const MDW_LOCK_INFO& aLock,
                                       MDW_EVENT_TYPE aType )
{
    m_locks->Set( aItemId, aLock );

    MDW_EVENT event;
    event.type = aType;
    event.workspace_id = m_workspaceId;
    event.item_id = aItemId;
    event.client_identity = m_clientIdentity;

    m_bus->PublishLocal( event, Supports( MDW_CAPABILITY_GROUP::EVENTS ) );
}


void MDW_WORKSPACE_SERVICE::Lock(
        const wxString& aItemId,
        std::function<void( MDW_ERROR_CODE, const MDW_LOCK_INFO& )> aOnComplete )
{
    if( !Supports( MDW_CAPABILITY_GROUP::LOCKING ) )
    {
        aOnComplete( MDW_ERROR_CODE::NOT_SUPPORTED, MDW_LOCK_INFO{} );
        return;
    }

    kiapi::mdw::LockRequest request;
    request.set_item_id( std::string( aItemId.utf8_string() ) );
    request.set_idempotency_id( std::string( KIID().AsString().utf8_string() ) );

    m_backend->CallAsync<kiapi::mdw::LockRequest, kiapi::mdw::LockReply>(
            request, OPERATION_TIMEOUT_MS,
            [this, aItemId, aOnComplete]( MDW_ERROR_CODE aCode, const kiapi::mdw::LockReply& aReply )
            {
                MDW_LOCK_INFO info = toStruct( aReply.lock() );

                if( aCode == MDW_ERROR_CODE::SUCCESS )
                    applyLock( aItemId, info, MDW_EVENT_TYPE::ITEM_LOCKED );

                aOnComplete( aCode, info );
            } );
}


void MDW_WORKSPACE_SERVICE::Unlock(
        const wxString& aItemId,
        std::function<void( MDW_ERROR_CODE, const MDW_LOCK_INFO& )> aOnComplete )
{
    if( !Supports( MDW_CAPABILITY_GROUP::LOCKING ) )
    {
        aOnComplete( MDW_ERROR_CODE::NOT_SUPPORTED, MDW_LOCK_INFO{} );
        return;
    }

    kiapi::mdw::UnlockRequest request;
    request.set_item_id( std::string( aItemId.utf8_string() ) );
    request.set_idempotency_id( std::string( KIID().AsString().utf8_string() ) );

    m_backend->CallAsync<kiapi::mdw::UnlockRequest, kiapi::mdw::LockReply>(
            request, OPERATION_TIMEOUT_MS,
            [this, aItemId, aOnComplete]( MDW_ERROR_CODE aCode, const kiapi::mdw::LockReply& aReply )
            {
                MDW_LOCK_INFO info = toStruct( aReply.lock() );

                if( aCode == MDW_ERROR_CODE::SUCCESS )
                    applyLock( aItemId, info, MDW_EVENT_TYPE::ITEM_UNLOCKED );

                aOnComplete( aCode, info );
            } );
}


void MDW_WORKSPACE_SERVICE::Checkin(
        const MDW_CHECKIN_REQUEST& aRequest,
        std::function<void( MDW_ERROR_CODE, const MDW_CHECKIN_RESULT& )> aOnComplete )
{
    // Checkout and checkin are mandatory, not a capability group, so unlike Lock
    // and Unlock there is nothing to gate here.
    kiapi::mdw::CheckinRequest request;
    request.set_item_id( std::string( aRequest.item_id.utf8_string() ) );
    request.set_local_path( std::string( aRequest.local_path.utf8_string() ) );
    request.set_comment( std::string( aRequest.comment.utf8_string() ) );
    request.set_idempotency_id( std::string( KIID().AsString().utf8_string() ) );

    for( const MDW_ATTRIBUTE_VALUE& value : aRequest.attributes.values )
    {
        kiapi::workspace::AttributeValue* out = request.mutable_attributes()->add_values();
        out->set_attr_id( std::string( value.attr_id.utf8_string() ) );
        out->set_value( std::string( value.value.utf8_string() ) );
    }

    wxString itemId = aRequest.item_id;

    m_backend->CallAsync<kiapi::mdw::CheckinRequest, kiapi::mdw::CheckinReply>(
            request, OPERATION_TIMEOUT_MS,
            [this, itemId, aOnComplete]( MDW_ERROR_CODE aCode, const kiapi::mdw::CheckinReply& aReply )
            {
                MDW_CHECKIN_RESULT result;
                result.success = aCode == MDW_ERROR_CODE::SUCCESS;
                result.error = aCode;
                result.revision = toStruct( aReply.revision() );

                if( aCode == MDW_ERROR_CODE::SUCCESS )
                {
                    m_dirty->Clear( itemId );

                    MDW_EVENT event;
                    event.type = MDW_EVENT_TYPE::REVISION_CHECKED_IN;
                    event.workspace_id = m_workspaceId;
                    event.item_id = itemId;
                    event.client_identity = m_clientIdentity;
                    m_bus->PublishLocal( event, Supports( MDW_CAPABILITY_GROUP::EVENTS ) );
                }

                aOnComplete( aCode, result );
            } );
}


MDW_ERROR_CODE MDW_WORKSPACE_SERVICE::ListItems( const wxString& aFolderId,
                                                 std::vector<MDW_ITEM_INFO>& aOut )
{
    kiapi::mdw::ListItemsRequest request;
    request.set_folder_id( std::string( aFolderId.utf8_string() ) );

    kiapi::mdw::ListItemsReply reply;
    MDW_ERROR_CODE             code = m_backend->CallSync( request, reply, OPERATION_TIMEOUT_MS );

    if( code == MDW_ERROR_CODE::SUCCESS )
    {
        for( const kiapi::workspace::ItemInfo& item : reply.items() )
            aOut.push_back( toStruct( item ) );
    }

    return code;
}


MDW_ERROR_CODE MDW_WORKSPACE_SERVICE::GetItemInfo( const wxString& aItemId, MDW_ITEM_INFO& aOut )
{
    kiapi::mdw::GetItemInfoRequest request;
    request.set_item_id( std::string( aItemId.utf8_string() ) );

    kiapi::mdw::GetItemInfoReply reply;
    MDW_ERROR_CODE               code = m_backend->CallSync( request, reply, OPERATION_TIMEOUT_MS );

    if( code == MDW_ERROR_CODE::SUCCESS )
        aOut = toStruct( reply.item() );

    return code;
}


MDW_ERROR_CODE MDW_WORKSPACE_SERVICE::GetRootFolders( std::vector<MDW_FOLDER>& aOut )
{
    kiapi::mdw::GetRootFoldersRequest request;
    kiapi::mdw::GetRootFoldersReply   reply;
    MDW_ERROR_CODE code = m_backend->CallSync( request, reply, OPERATION_TIMEOUT_MS );

    if( code == MDW_ERROR_CODE::SUCCESS )
    {
        for( const kiapi::workspace::Folder& folder : reply.folders() )
            aOut.push_back( toStruct( folder ) );
    }

    return code;
}


MDW_ERROR_CODE MDW_WORKSPACE_SERVICE::GetChildFolders( const wxString& aFolderId,
                                                       std::vector<MDW_FOLDER>& aOut )
{
    kiapi::mdw::GetChildFoldersRequest request;
    request.set_folder_id( std::string( aFolderId.utf8_string() ) );

    kiapi::mdw::GetChildFoldersReply reply;
    MDW_ERROR_CODE code = m_backend->CallSync( request, reply, OPERATION_TIMEOUT_MS );

    if( code == MDW_ERROR_CODE::SUCCESS )
    {
        for( const kiapi::workspace::Folder& folder : reply.folders() )
            aOut.push_back( toStruct( folder ) );
    }

    return code;
}


MDW_ERROR_CODE MDW_WORKSPACE_SERVICE::Checkout( const MDW_CHECKOUT_REQUEST& aRequest,
                                                MDW_CHECKOUT_RESULT& aOut )
{
    kiapi::mdw::CheckoutRequest request;
    request.set_item_id( std::string( aRequest.item_id.utf8_string() ) );
    request.set_local_path( std::string( aRequest.local_path.utf8_string() ) );
    request.set_lock( aRequest.lock );
    request.set_idempotency_id( std::string( KIID().AsString().utf8_string() ) );

    kiapi::mdw::CheckoutReply reply;
    MDW_ERROR_CODE            code = m_backend->CallSync( request, reply, OPERATION_TIMEOUT_MS );

    aOut.error = code;
    aOut.success = code == MDW_ERROR_CODE::SUCCESS;

    if( code == MDW_ERROR_CODE::SUCCESS )
    {
        aOut.item = toStruct( reply.item() );
        aOut.local_path = wxString::FromUTF8( reply.local_path() );

        if( aOut.item.is_locked )
        {
            MDW_LOCK_INFO lock;
            lock.is_locked = true;
            lock.lock_owner = aOut.item.lock_owner;
            lock.locked_by_me = aRequest.lock;
            applyLock( aOut.item.item_id, lock, MDW_EVENT_TYPE::ITEM_LOCKED );
        }
    }

    return code;
}


MDW_ERROR_CODE MDW_WORKSPACE_SERVICE::CheckinSync( const MDW_CHECKIN_REQUEST& aRequest,
                                                   MDW_CHECKIN_RESULT& aOut )
{
    kiapi::mdw::CheckinRequest request;
    request.set_item_id( std::string( aRequest.item_id.utf8_string() ) );
    request.set_local_path( std::string( aRequest.local_path.utf8_string() ) );
    request.set_comment( std::string( aRequest.comment.utf8_string() ) );
    request.set_idempotency_id( std::string( KIID().AsString().utf8_string() ) );

    for( const MDW_ATTRIBUTE_VALUE& value : aRequest.attributes.values )
    {
        kiapi::workspace::AttributeValue* out = request.mutable_attributes()->add_values();
        out->set_attr_id( std::string( value.attr_id.utf8_string() ) );
        out->set_value( std::string( value.value.utf8_string() ) );
    }

    kiapi::mdw::CheckinReply reply;
    MDW_ERROR_CODE           code = m_backend->CallSync( request, reply, OPERATION_TIMEOUT_MS );

    aOut.error = code;
    aOut.success = code == MDW_ERROR_CODE::SUCCESS;

    if( code == MDW_ERROR_CODE::SUCCESS )
    {
        aOut.revision = toStruct( reply.revision() );
        m_dirty->Clear( aRequest.item_id );

        MDW_EVENT event;
        event.type = MDW_EVENT_TYPE::REVISION_CHECKED_IN;
        event.workspace_id = m_workspaceId;
        event.item_id = aRequest.item_id;
        event.client_identity = m_clientIdentity;
        m_bus->PublishLocal( event, Supports( MDW_CAPABILITY_GROUP::EVENTS ) );
    }

    return code;
}


MDW_ERROR_CODE MDW_WORKSPACE_SERVICE::CancelCheckout( const wxString& aItemId,
                                                      const wxString& aLocalPath )
{
    kiapi::mdw::CancelCheckoutRequest request;
    request.set_item_id( std::string( aItemId.utf8_string() ) );
    request.set_local_path( std::string( aLocalPath.utf8_string() ) );

    kiapi::mdw::CancelCheckoutReply reply;
    MDW_ERROR_CODE code = m_backend->CallSync( request, reply, OPERATION_TIMEOUT_MS );

    if( code == MDW_ERROR_CODE::SUCCESS )
    {
        bool wasLocked = m_locks->IsLocked( aItemId );

        m_locks->Remove( aItemId );
        m_dirty->Clear( aItemId );

        if( wasLocked )
        {
            MDW_EVENT event;
            event.type = MDW_EVENT_TYPE::ITEM_UNLOCKED;
            event.workspace_id = m_workspaceId;
            event.item_id = aItemId;
            event.client_identity = m_clientIdentity;
            m_bus->PublishLocal( event, Supports( MDW_CAPABILITY_GROUP::EVENTS ) );
        }
    }

    return code;
}


MDW_ERROR_CODE MDW_WORKSPACE_SERVICE::LockSync( const wxString& aItemId, MDW_LOCK_INFO& aOut )
{
    if( !Supports( MDW_CAPABILITY_GROUP::LOCKING ) )
        return MDW_ERROR_CODE::NOT_SUPPORTED;

    kiapi::mdw::LockRequest request;
    request.set_item_id( std::string( aItemId.utf8_string() ) );
    request.set_idempotency_id( std::string( KIID().AsString().utf8_string() ) );

    kiapi::mdw::LockReply reply;
    MDW_ERROR_CODE        code = m_backend->CallSync( request, reply, OPERATION_TIMEOUT_MS );

    if( code == MDW_ERROR_CODE::SUCCESS )
    {
        aOut = toStruct( reply.lock() );
        applyLock( aItemId, aOut, MDW_EVENT_TYPE::ITEM_LOCKED );
    }

    return code;
}


MDW_ERROR_CODE MDW_WORKSPACE_SERVICE::UnlockSync( const wxString& aItemId, MDW_LOCK_INFO& aOut )
{
    if( !Supports( MDW_CAPABILITY_GROUP::LOCKING ) )
        return MDW_ERROR_CODE::NOT_SUPPORTED;

    kiapi::mdw::UnlockRequest request;
    request.set_item_id( std::string( aItemId.utf8_string() ) );
    request.set_idempotency_id( std::string( KIID().AsString().utf8_string() ) );

    kiapi::mdw::LockReply reply;
    MDW_ERROR_CODE        code = m_backend->CallSync( request, reply, OPERATION_TIMEOUT_MS );

    if( code == MDW_ERROR_CODE::SUCCESS )
    {
        aOut = toStruct( reply.lock() );
        applyLock( aItemId, aOut, MDW_EVENT_TYPE::ITEM_UNLOCKED );
    }

    return code;
}


MDW_ERROR_CODE MDW_WORKSPACE_SERVICE::GetLockInfo( const wxString& aItemId, MDW_LOCK_INFO& aOut )
{
    if( !Supports( MDW_CAPABILITY_GROUP::LOCKING ) )
        return MDW_ERROR_CODE::NOT_SUPPORTED;

    kiapi::mdw::GetLockInfoRequest request;
    request.set_item_id( std::string( aItemId.utf8_string() ) );

    kiapi::mdw::GetLockInfoReply reply;
    MDW_ERROR_CODE code = m_backend->CallSync( request, reply, OPERATION_TIMEOUT_MS );

    if( code == MDW_ERROR_CODE::SUCCESS )
        aOut = toStruct( reply.lock() );

    return code;
}


MDW_ERROR_CODE MDW_WORKSPACE_SERVICE::GetRevisionHistory( const wxString& aItemId,
                                                          std::vector<MDW_REVISION>& aOut )
{
    if( !Supports( MDW_CAPABILITY_GROUP::REVISIONS ) )
        return MDW_ERROR_CODE::NOT_SUPPORTED;

    kiapi::mdw::GetRevisionHistoryRequest request;
    request.set_item_id( std::string( aItemId.utf8_string() ) );

    kiapi::mdw::GetRevisionHistoryReply reply;
    MDW_ERROR_CODE code = m_backend->CallSync( request, reply, OPERATION_TIMEOUT_MS );

    if( code == MDW_ERROR_CODE::SUCCESS )
    {
        for( const kiapi::workspace::Revision& revision : reply.revisions() )
            aOut.push_back( toStruct( revision ) );
    }

    return code;
}


MDW_ERROR_CODE MDW_WORKSPACE_SERVICE::DownloadRevision( const wxString& aItemId,
                                                        const wxString& aRevisionId,
                                                        const wxString& aLocalPath )
{
    if( !Supports( MDW_CAPABILITY_GROUP::REVISIONS ) )
        return MDW_ERROR_CODE::NOT_SUPPORTED;

    kiapi::mdw::DownloadRevisionRequest request;
    request.set_item_id( std::string( aItemId.utf8_string() ) );
    request.set_revision_id( std::string( aRevisionId.utf8_string() ) );
    request.set_local_path( std::string( aLocalPath.utf8_string() ) );

    kiapi::mdw::DownloadRevisionReply reply;

    return m_backend->CallSync( request, reply, OPERATION_TIMEOUT_MS );
}


MDW_ERROR_CODE MDW_WORKSPACE_SERVICE::GetAttributes( const wxString& aItemId,
                                                     MDW_ATTRIBUTE_SET& aOut )
{
    if( !Supports( MDW_CAPABILITY_GROUP::ATTRIBUTES ) )
        return MDW_ERROR_CODE::NOT_SUPPORTED;

    kiapi::mdw::GetAttributesRequest request;
    request.set_item_id( std::string( aItemId.utf8_string() ) );

    kiapi::mdw::GetAttributesReply reply;
    MDW_ERROR_CODE code = m_backend->CallSync( request, reply, OPERATION_TIMEOUT_MS );

    if( code == MDW_ERROR_CODE::SUCCESS )
        aOut = toStruct( reply.attributes() );

    return code;
}


MDW_ERROR_CODE MDW_WORKSPACE_SERVICE::SetAttributes( const wxString& aItemId,
                                                     const MDW_ATTRIBUTE_SET& aAttributes )
{
    if( !Supports( MDW_CAPABILITY_GROUP::ATTRIBUTES ) )
        return MDW_ERROR_CODE::NOT_SUPPORTED;

    kiapi::mdw::SetAttributesRequest request;
    request.set_item_id( std::string( aItemId.utf8_string() ) );
    request.set_idempotency_id( std::string( KIID().AsString().utf8_string() ) );

    for( const MDW_ATTRIBUTE_VALUE& value : aAttributes.values )
    {
        kiapi::workspace::AttributeValue* out = request.mutable_attributes()->add_values();
        out->set_attr_id( std::string( value.attr_id.utf8_string() ) );
        out->set_value( std::string( value.value.utf8_string() ) );
    }

    kiapi::mdw::SetAttributesReply reply;

    return m_backend->CallSync( request, reply, OPERATION_TIMEOUT_MS );
}


MDW_ERROR_CODE MDW_WORKSPACE_SERVICE::GetLifecycleCategory( const wxString& aItemId,
                                                            MDW_LIFECYCLE_CATEGORY& aOutCategory,
                                                            wxString& aOutCustomStateId )
{
    if( !Supports( MDW_CAPABILITY_GROUP::LIFECYCLE ) )
        return MDW_ERROR_CODE::NOT_SUPPORTED;

    // The category and custom state ride along on the item metadata; the
    // backend exposes no separate lifecycle getter.
    kiapi::mdw::GetItemInfoRequest request;
    request.set_item_id( std::string( aItemId.utf8_string() ) );

    kiapi::mdw::GetItemInfoReply reply;
    MDW_ERROR_CODE code = m_backend->CallSync( request, reply, OPERATION_TIMEOUT_MS );

    if( code == MDW_ERROR_CODE::SUCCESS )
    {
        aOutCategory = static_cast<MDW_LIFECYCLE_CATEGORY>( reply.item().category() );
        aOutCustomStateId = wxString::FromUTF8( reply.item().custom_state_id() );
    }

    return code;
}


MDW_ERROR_CODE MDW_WORKSPACE_SERVICE::TransitionLifecycle( const wxString& aItemId,
                                                           const wxString& aTargetStateId,
                                                           const wxString& aComment,
                                                           MDW_LIFECYCLE_CATEGORY& aOutCategory,
                                                           wxString& aOutStateId )
{
    if( !Supports( MDW_CAPABILITY_GROUP::LIFECYCLE ) )
        return MDW_ERROR_CODE::NOT_SUPPORTED;

    kiapi::mdw::TransitionLifecycleRequest request;
    request.set_item_id( std::string( aItemId.utf8_string() ) );
    request.set_target_state_id( std::string( aTargetStateId.utf8_string() ) );
    request.set_comment( std::string( aComment.utf8_string() ) );
    request.set_idempotency_id( std::string( KIID().AsString().utf8_string() ) );

    kiapi::mdw::TransitionLifecycleReply reply;
    MDW_ERROR_CODE code = m_backend->CallSync( request, reply, OPERATION_TIMEOUT_MS );

    if( code == MDW_ERROR_CODE::SUCCESS )
    {
        aOutCategory = static_cast<MDW_LIFECYCLE_CATEGORY>( reply.new_category() );
        aOutStateId = wxString::FromUTF8( reply.new_state_id() );

        MDW_EVENT event;
        event.type = MDW_EVENT_TYPE::LIFECYCLE_CATEGORY_CHANGED;
        event.workspace_id = m_workspaceId;
        event.item_id = aItemId;
        event.client_identity = m_clientIdentity;
        m_bus->PublishLocal( event, Supports( MDW_CAPABILITY_GROUP::EVENTS ) );
    }

    return code;
}
