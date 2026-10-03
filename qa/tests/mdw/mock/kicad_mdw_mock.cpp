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

/*
 * kicad-mdw-mock
 *
 * A reference MDW backend built on the backend SDK. It advertises a
 * configurable capability set, serves a tiny in-memory workspace, and injects
 * optional latency and forced failures through the environment so the
 * conformance suite can exercise the framework against a controllable peer.
 * It links kiapi and nng only; no wxWidgets and no kicommon.
 */

#include <mdw_sdk/mdw_backend_server.h>
#include <mdw_sdk/mdw_run_backend.h>
#include <mdw_sdk/mdw_event_publisher.h>

#include <api/mdw/backend_base.pb.h>
#include <api/mdw/backend_locking.pb.h>
#include <api/mdw/backend_revisions.pb.h>
#include <api/mdw/backend_attributes.pb.h>
#include <api/mdw/backend_lifecycle.pb.h>
#include <api/workspace/workspace_types.pb.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace mdw = kiapi::mdw;
namespace ws = kiapi::workspace;

namespace
{

const std::string USER = "mockuser";

/// The in-memory workspace shared across worker threads.
struct MOCK_STATE
{
    std::mutex                              mutex;
    std::map<std::string, ws::ItemInfo>     items;
    std::map<std::string, std::string>      lockedBy;
    std::map<std::string, ws::AttributeSet> attributes;
};


MOCK_STATE& mockState()
{
    static MOCK_STATE s;
    static const bool init = []()
    {
        const char* names[] = { "board.kicad_pcb", "sch.kicad_sch", "notes.md" };

        for( const char* name : names )
        {
            ws::ItemInfo item;
            item.set_item_id( name );
            item.set_display_name( name );
            item.set_folder_id( "root" );
            item.set_is_folder( false );
            item.set_category( ws::LC_EDITABLE );
            item.set_revision_id( "r1" );
            s.items[name] = item;
        }

        return true;
    }();

    (void) init;
    return s;
}


long latencyMs()
{
    static const long ms = []()
    {
        const char* value = std::getenv( "MDW_MOCK_LATENCY_MS" );
        return value ? std::atol( value ) : 0L;
    }();

    return ms;
}


void injectLatency()
{
    const long ms = latencyMs();

    if( ms > 0 )
        std::this_thread::sleep_for( std::chrono::milliseconds( ms ) );
}


bool envSet( const char* aName )
{
    return std::getenv( aName ) != nullptr;
}


/// Whether a successful Authenticate has been seen, for MDW_MOCK_REQUIRE_AUTH.
std::atomic<bool> g_authenticated{ false };

/// The method named by the last accepted credentials, so a test can tell a token
/// replay from a password login.
std::string g_lastAuthMethod;


/// Advertised capability group names, from MDW_MOCK_CAPABILITIES or the default set.
const std::set<std::string>& advertisedGroups()
{
    static const std::set<std::string> groups = []()
    {
        std::set<std::string> result;
        const char*           value = std::getenv( "MDW_MOCK_CAPABILITIES" );

        if( !value )
            return std::set<std::string>{ "LOCKING", "REVISIONS", "ATTRIBUTES", "LIFECYCLE",
                                          "EVENTS" };

        std::string text( value );
        std::string token;

        for( char c : text )
        {
            if( c == ',' )
            {
                if( !token.empty() )
                    result.insert( token );

                token.clear();
            }
            else if( c != ' ' )
            {
                token.push_back( c );
            }
        }

        if( !token.empty() )
            result.insert( token );

        return result;
    }();

    return groups;
}


bool hasGroup( const std::string& aName )
{
    return advertisedGroups().count( aName ) > 0;
}


/// Publish one workspace event, gated on CG_EVENTS. Publish no-ops until running.
void emitEvent( MDW_EVENT_PUBLISHER* aPublisher, std::atomic<uint64_t>* aSeq, bool aEnabled,
                ws::EventType aType, const std::string& aItemId )
{
    if( !aEnabled || !aPublisher )
        return;

    ws::EventEnvelope event;
    event.set_type( aType );
    event.set_workspace_id( "mock" );
    event.set_item_id( aItemId );
    event.set_seq( aSeq->fetch_add( 1 ) + 1 );
    aPublisher->Publish( event );
}


ws::CapabilityGroup groupEnum( const std::string& aName )
{
    if( aName == "LOCKING" )
        return ws::CG_LOCKING;

    if( aName == "REVISIONS" )
        return ws::CG_REVISIONS;

    if( aName == "ATTRIBUTES" )
        return ws::CG_ATTRIBUTES;

    if( aName == "LIFECYCLE" )
        return ws::CG_LIFECYCLE;

    if( aName == "EVENTS" )
        return ws::CG_EVENTS;

    return ws::CG_UNKNOWN;
}


void buildDescriptor( MDW_BACKEND_DESCRIPTOR& aDescriptor )
{
    aDescriptor.info.set_backend_id( "org.kicad.mdw.mock" );
    aDescriptor.info.set_display_name( "Mock Workspace" );
    aDescriptor.info.mutable_version()->set_major( 1 );
    aDescriptor.info.mutable_version()->set_minor( 0 );

    for( const std::string& name : advertisedGroups() )
    {
        ws::CapabilityGroup group = groupEnum( name );

        if( group != ws::CG_UNKNOWN )
            aDescriptor.capabilities.add_groups( group );
    }

    aDescriptor.schema.set_schema_version( "1" );

    ws::AttributeSchema* owner = aDescriptor.schema.add_attributes();
    owner->set_attr_id( "owner" );
    owner->set_display_name( "Owner" );
    owner->set_type( ws::AT_STRING );

    ws::AttributeSchema* status = aDescriptor.schema.add_attributes();
    status->set_attr_id( "status" );
    status->set_display_name( "Status" );
    status->set_type( ws::AT_ENUM );
    status->add_enum_values( "draft" );
    status->add_enum_values( "review" );
    status->add_enum_values( "final" );
}


void registerBaseHandlers( MDW_BACKEND_SERVER& aServer, MDW_EVENT_PUBLISHER* aPublisher,
                           std::atomic<uint64_t>* aSeq, bool aEvents )
{
    aServer.RegisterHandler<mdw::AuthenticateRequest, mdw::AuthResult>(
            []( const mdw::AuthenticateRequest& aRequest,
                mdw::AuthResult& aReply ) -> ws::WorkspaceError
            {
                injectLatency();

                if( envSet( "MDW_MOCK_FAIL_AUTH" ) )
                {
                    aReply.set_authenticated( false );
                    return MdwFail( ws::WE_AUTH_FAILED, "denied" );
                }

                g_authenticated.store( true );
                g_lastAuthMethod = aRequest.credentials().method();

                aReply.set_authenticated( true );
                aReply.set_user_id( USER );
                aReply.set_display_name( "Mock User" );
                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::LogoutRequest, mdw::LogoutReply>(
            []( const mdw::LogoutRequest&, mdw::LogoutReply& aReply ) -> ws::WorkspaceError
            {
                injectLatency();
                aReply.set_ok( true );
                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::IsAuthenticatedRequest, mdw::IsAuthenticatedReply>(
            []( const mdw::IsAuthenticatedRequest&,
                mdw::IsAuthenticatedReply& aReply ) -> ws::WorkspaceError
            {
                injectLatency();

                // Most backends need no credentials and are authenticated from
                // the start. MDW_MOCK_REQUIRE_AUTH models one that does, so the
                // framework's Authenticate path can be driven both ways.
                aReply.set_authenticated( !envSet( "MDW_MOCK_REQUIRE_AUTH" )
                                          || g_authenticated.load() );
                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::GetRootFoldersRequest, mdw::GetRootFoldersReply>(
            []( const mdw::GetRootFoldersRequest&,
                mdw::GetRootFoldersReply& aReply ) -> ws::WorkspaceError
            {
                injectLatency();

                ws::Folder* folder = aReply.add_folders();
                folder->set_folder_id( "root" );
                folder->set_display_name( "Root" );
                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::GetChildFoldersRequest, mdw::GetChildFoldersReply>(
            []( const mdw::GetChildFoldersRequest&,
                mdw::GetChildFoldersReply& ) -> ws::WorkspaceError
            {
                injectLatency();
                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::ListItemsRequest, mdw::ListItemsReply>(
            []( const mdw::ListItemsRequest& aRequest,
                mdw::ListItemsReply& aReply ) -> ws::WorkspaceError
            {
                injectLatency();

                MOCK_STATE&                 state = mockState();
                std::lock_guard<std::mutex> lock( state.mutex );

                for( const auto& entry : state.items )
                {
                    if( entry.second.folder_id() == aRequest.folder_id() )
                        *aReply.add_items() = entry.second;
                }

                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::GetItemInfoRequest, mdw::GetItemInfoReply>(
            []( const mdw::GetItemInfoRequest& aRequest,
                mdw::GetItemInfoReply& aReply ) -> ws::WorkspaceError
            {
                injectLatency();

                MOCK_STATE&                 state = mockState();
                std::lock_guard<std::mutex> lock( state.mutex );

                auto it = state.items.find( aRequest.item_id() );

                if( it == state.items.end() )
                    return MdwFail( ws::WE_ITEM_NOT_FOUND, "no such item" );

                *aReply.mutable_item() = it->second;
                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::CheckoutRequest, mdw::CheckoutReply>(
            []( const mdw::CheckoutRequest& aRequest,
                mdw::CheckoutReply& aReply ) -> ws::WorkspaceError
            {
                injectLatency();

                if( envSet( "MDW_MOCK_FAIL_CHECKOUT" ) )
                    return MdwFail( ws::WE_INTERNAL, "checkout failed" );

                MOCK_STATE&                 state = mockState();
                std::lock_guard<std::mutex> lock( state.mutex );

                auto it = state.items.find( aRequest.item_id() );

                if( it == state.items.end() )
                    return MdwFail( ws::WE_ITEM_NOT_FOUND, "no such item" );

                if( aRequest.lock() )
                {
                    state.lockedBy[aRequest.item_id()] = USER;
                    it->second.set_is_locked( true );
                    it->second.set_lock_owner( USER );
                }

                *aReply.mutable_item() = it->second;
                aReply.set_local_path( aRequest.local_path() );
                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::CheckinRequest, mdw::CheckinReply>(
            [aPublisher, aSeq, aEvents]( const mdw::CheckinRequest& aRequest,
                                         mdw::CheckinReply& aReply ) -> ws::WorkspaceError
            {
                injectLatency();

                if( envSet( "MDW_MOCK_FAIL_CHECKIN" ) )
                    return MdwFail( ws::WE_VERSION_CONFLICT, "conflict" );

                {
                    MOCK_STATE&                 state = mockState();
                    std::lock_guard<std::mutex> lock( state.mutex );

                    auto it = state.items.find( aRequest.item_id() );

                    if( it == state.items.end() )
                        return MdwFail( ws::WE_ITEM_NOT_FOUND, "no such item" );

                    state.attributes[aRequest.item_id()] = aRequest.attributes();
                    state.lockedBy.erase( aRequest.item_id() );
                    it->second.set_is_locked( false );
                    it->second.clear_lock_owner();
                    it->second.set_revision_id( "r2" );
                }

                ws::Revision* rev = aReply.mutable_revision();
                rev->set_revision_id( "r2" );
                rev->set_author( USER );
                rev->set_comment( aRequest.comment() );
                rev->set_timestamp_unix( std::time( nullptr ) );

                emitEvent( aPublisher, aSeq, aEvents, ws::ET_REVISION_CHECKED_IN,
                           aRequest.item_id() );
                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::CancelCheckoutRequest, mdw::CancelCheckoutReply>(
            [aPublisher, aSeq, aEvents]( const mdw::CancelCheckoutRequest& aRequest,
                                         mdw::CancelCheckoutReply& ) -> ws::WorkspaceError
            {
                injectLatency();

                {
                    MOCK_STATE&                 state = mockState();
                    std::lock_guard<std::mutex> lock( state.mutex );

                    state.lockedBy.erase( aRequest.item_id() );

                    auto it = state.items.find( aRequest.item_id() );

                    if( it != state.items.end() )
                    {
                        it->second.set_is_locked( false );
                        it->second.clear_lock_owner();
                    }
                }

                emitEvent( aPublisher, aSeq, aEvents, ws::ET_ITEM_UNLOCKED, aRequest.item_id() );
                return MdwOk();
            } );
}


void registerLockingHandlers( MDW_BACKEND_SERVER& aServer, MDW_EVENT_PUBLISHER* aPublisher,
                              std::atomic<uint64_t>* aSeq, bool aEvents )
{
    aServer.RegisterHandler<mdw::LockRequest, mdw::LockReply>(
            [aPublisher, aSeq, aEvents]( const mdw::LockRequest& aRequest,
                                         mdw::LockReply& aReply ) -> ws::WorkspaceError
            {
                injectLatency();

                {
                    MOCK_STATE&                 state = mockState();
                    std::lock_guard<std::mutex> lock( state.mutex );

                    auto it = state.items.find( aRequest.item_id() );

                    if( it == state.items.end() )
                        return MdwFail( ws::WE_ITEM_NOT_FOUND, "no such item" );

                    state.lockedBy[aRequest.item_id()] = USER;
                    it->second.set_is_locked( true );
                    it->second.set_lock_owner( USER );
                }

                ws::LockInfo* info = aReply.mutable_lock();
                info->set_is_locked( true );
                info->set_lock_owner( USER );
                info->set_locked_by_me( true );
                info->set_lock_timestamp_unix( std::time( nullptr ) );

                emitEvent( aPublisher, aSeq, aEvents, ws::ET_ITEM_LOCKED, aRequest.item_id() );
                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::UnlockRequest, mdw::LockReply>(
            [aPublisher, aSeq, aEvents]( const mdw::UnlockRequest& aRequest,
                                         mdw::LockReply& aReply ) -> ws::WorkspaceError
            {
                injectLatency();

                {
                    MOCK_STATE&                 state = mockState();
                    std::lock_guard<std::mutex> lock( state.mutex );

                    state.lockedBy.erase( aRequest.item_id() );

                    auto it = state.items.find( aRequest.item_id() );

                    if( it != state.items.end() )
                    {
                        it->second.set_is_locked( false );
                        it->second.clear_lock_owner();
                    }
                }

                aReply.mutable_lock()->set_is_locked( false );

                emitEvent( aPublisher, aSeq, aEvents, ws::ET_ITEM_UNLOCKED, aRequest.item_id() );
                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::GetLockInfoRequest, mdw::GetLockInfoReply>(
            []( const mdw::GetLockInfoRequest& aRequest,
                mdw::GetLockInfoReply& aReply ) -> ws::WorkspaceError
            {
                injectLatency();

                MOCK_STATE&                 state = mockState();
                std::lock_guard<std::mutex> lock( state.mutex );

                auto          it = state.lockedBy.find( aRequest.item_id() );
                ws::LockInfo* info = aReply.mutable_lock();

                if( it != state.lockedBy.end() )
                {
                    info->set_is_locked( true );
                    info->set_lock_owner( it->second );
                    info->set_locked_by_me( it->second == USER );
                }

                return MdwOk();
            } );
}


void registerRevisionHandlers( MDW_BACKEND_SERVER& aServer )
{
    aServer.RegisterHandler<mdw::GetRevisionHistoryRequest, mdw::GetRevisionHistoryReply>(
            []( const mdw::GetRevisionHistoryRequest&,
                mdw::GetRevisionHistoryReply& aReply ) -> ws::WorkspaceError
            {
                injectLatency();

                ws::Revision* r1 = aReply.add_revisions();
                r1->set_revision_id( "r1" );
                r1->set_author( USER );
                r1->set_comment( "initial" );

                ws::Revision* r2 = aReply.add_revisions();
                r2->set_revision_id( "r2" );
                r2->add_parent_revision_ids( "r1" );
                r2->set_author( USER );
                r2->set_comment( "update" );
                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::DownloadRevisionRequest, mdw::DownloadRevisionReply>(
            []( const mdw::DownloadRevisionRequest&,
                mdw::DownloadRevisionReply& ) -> ws::WorkspaceError
            {
                injectLatency();
                return MdwOk();
            } );
}


void registerAttributeHandlers( MDW_BACKEND_SERVER& aServer )
{
    aServer.RegisterHandler<mdw::GetAttributesRequest, mdw::GetAttributesReply>(
            []( const mdw::GetAttributesRequest& aRequest,
                mdw::GetAttributesReply& aReply ) -> ws::WorkspaceError
            {
                injectLatency();

                MOCK_STATE&                 state = mockState();
                std::lock_guard<std::mutex> lock( state.mutex );

                auto it = state.attributes.find( aRequest.item_id() );

                if( it != state.attributes.end() )
                    *aReply.mutable_attributes() = it->second;

                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::SetAttributesRequest, mdw::SetAttributesReply>(
            []( const mdw::SetAttributesRequest& aRequest,
                mdw::SetAttributesReply& ) -> ws::WorkspaceError
            {
                injectLatency();

                MOCK_STATE&                 state = mockState();
                std::lock_guard<std::mutex> lock( state.mutex );

                state.attributes[aRequest.item_id()] = aRequest.attributes();
                return MdwOk();
            } );
}


void registerLifecycleHandlers( MDW_BACKEND_SERVER& aServer, MDW_EVENT_PUBLISHER* aPublisher,
                                std::atomic<uint64_t>* aSeq, bool aEvents )
{
    aServer.RegisterHandler<mdw::GetAvailableTransitionsRequest, mdw::GetAvailableTransitionsReply>(
            []( const mdw::GetAvailableTransitionsRequest&,
                mdw::GetAvailableTransitionsReply& aReply ) -> ws::WorkspaceError
            {
                injectLatency();

                mdw::ValidTransition* transition = aReply.add_transitions();
                transition->set_target_state_id( "final" );
                transition->set_display_name( "Final" );
                transition->set_target_category( ws::LC_RELEASED );
                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::TransitionLifecycleRequest, mdw::TransitionLifecycleReply>(
            [aPublisher, aSeq, aEvents]( const mdw::TransitionLifecycleRequest& aRequest,
                                         mdw::TransitionLifecycleReply& aReply ) -> ws::WorkspaceError
            {
                injectLatency();

                {
                    MOCK_STATE&                 state = mockState();
                    std::lock_guard<std::mutex> lock( state.mutex );

                    auto it = state.items.find( aRequest.item_id() );

                    if( it != state.items.end() )
                    {
                        it->second.set_category( ws::LC_RELEASED );
                        it->second.set_custom_state_id( aRequest.target_state_id() );
                    }
                }

                aReply.set_new_state_id( aRequest.target_state_id() );
                aReply.set_new_category( ws::LC_RELEASED );

                emitEvent( aPublisher, aSeq, aEvents, ws::ET_LIFECYCLE_CATEGORY_CHANGED,
                           aRequest.item_id() );
                return MdwOk();
            } );
}

} // namespace


int main( int argc, char** argv )
{
    MDW_BACKEND_LAUNCH launch = MDW_ParseLaunch( argc, argv );

    MDW_BACKEND_DESCRIPTOR descriptor;
    buildDescriptor( descriptor );

    MDW_EVENT_PUBLISHER   publisher( launch.channel_token );
    std::atomic<uint64_t> seq{ 0 };
    const bool            events = hasGroup( "EVENTS" );

    MDW_BACKEND_SERVER server;
    registerBaseHandlers( server, &publisher, &seq, events );

    if( hasGroup( "LOCKING" ) )
        registerLockingHandlers( server, &publisher, &seq, events );

    if( hasGroup( "REVISIONS" ) )
        registerRevisionHandlers( server );

    if( hasGroup( "ATTRIBUTES" ) )
        registerAttributeHandlers( server );

    if( hasGroup( "LIFECYCLE" ) )
        registerLifecycleHandlers( server, &publisher, &seq, events );

    return MDW_RunBackend( launch, descriptor, server, &publisher );
}
