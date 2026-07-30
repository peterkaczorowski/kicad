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
