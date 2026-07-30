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

/*
 * Covers the framework runtime state: the state mapper, the echo-deduping event
 * bus, the lock and dirty trackers, and the workspace service that coordinates
 * them, including capability gating and the proposal's lifecycle/lock
 * independence case.
 */

#include <boost/test/unit_test.hpp>

#include <mdw/mdw_state_mapper.h>
#include <mdw/mdw_event_bus.h>
#include <mdw/mdw_lock_tracker.h>
#include <mdw/mdw_dirty_tracker.h>
#include <mdw/mdw_workspace_service.h>
#include <mdw/mdw_backend.h>
#include <mdw/mdw_loopback_transport.h>
#include <mdw/mdw_item.h>

#include <api/mdw/backend_base.pb.h>
#include <api/mdw/backend_locking.pb.h>

#include <future>
#include <memory>
#include <string>


BOOST_AUTO_TEST_SUITE( MdwRuntime )


BOOST_AUTO_TEST_CASE( StateMapperCategoriesAndColors )
{
    MDW_STATE_MAPPER mapper;
    mapper.SetCustomStates(
            { { "in_work", "In Work", "", MDW_LIFECYCLE_CATEGORY::EDITABLE, "", 0 },
              { "frozen", "Frozen", "", MDW_LIFECYCLE_CATEGORY::RESTRICTED, "", 1 } } );

    BOOST_CHECK( mapper.GetCategory( "frozen" ) == MDW_LIFECYCLE_CATEGORY::RESTRICTED );
    BOOST_CHECK( mapper.GetCategory( "unknown" ) == MDW_LIFECYCLE_CATEGORY::EDITABLE );
    BOOST_CHECK_EQUAL( mapper.GetDisplayName( "in_work" ), wxString( "In Work" ) );

    BOOST_CHECK( MDW_STATE_MAPPER::CategoryColor( MDW_LIFECYCLE_CATEGORY::EDITABLE )
                 != MDW_STATE_MAPPER::CategoryColor( MDW_LIFECYCLE_CATEGORY::RESTRICTED ) );
    BOOST_CHECK( MDW_STATE_MAPPER::CategoryBlocksWrites( MDW_LIFECYCLE_CATEGORY::RESTRICTED ) );
    BOOST_CHECK( !MDW_STATE_MAPPER::CategoryBlocksWrites( MDW_LIFECYCLE_CATEGORY::EDITABLE ) );
}


BOOST_AUTO_TEST_CASE( EventBusEchoDedup )
{
    MDW_EVENT_BUS bus;
    int           count = 0;

    bus.Subscribe( [&count]( const MDW_EVENT& ) { ++count; } );

    MDW_EVENT locked;
    locked.type = MDW_EVENT_TYPE::ITEM_LOCKED;
    locked.workspace_id = "ws";
    locked.item_id = "board";

    // A local optimistic event fires; the backend's echo of the same operation
    // is suppressed.
    bus.PublishLocal( locked );
    BOOST_CHECK_EQUAL( count, 1 );

    MDW_EVENT echo = locked;
    echo.seq = 5;
    bus.PublishFromBackend( echo );
    BOOST_CHECK_EQUAL( count, 1 );

    // A genuinely new backend event fires, and a re-delivery of its seq is dropped.
    MDW_EVENT unlocked;
    unlocked.type = MDW_EVENT_TYPE::ITEM_UNLOCKED;
    unlocked.workspace_id = "ws";
    unlocked.item_id = "board";
    unlocked.seq = 6;
    bus.PublishFromBackend( unlocked );
    BOOST_CHECK_EQUAL( count, 2 );

    bus.PublishFromBackend( unlocked );
    BOOST_CHECK_EQUAL( count, 2 );
}


BOOST_AUTO_TEST_CASE( LifecycleAndLockTrackedIndependently )
{
    MDW_LOCK_TRACKER locks;

    MDW_LOCK_INFO teammate;
    teammate.is_locked = true;
    teammate.lock_owner = "teammate";
    teammate.locked_by_me = false;
    locks.Set( "board", teammate );

    // An EDITABLE item is locked by a teammate: category and lock are separate.
    MDW_ITEM_INFO editable;
    editable.category = MDW_LIFECYCLE_CATEGORY::EDITABLE;

    BOOST_CHECK( editable.category == MDW_LIFECYCLE_CATEGORY::EDITABLE );
    BOOST_CHECK( locks.IsLocked( "board" ) );
    BOOST_CHECK( !locks.LockedByMe( "board" ) );

    // A RESTRICTED item with no lock.
    MDW_ITEM_INFO restricted;
    restricted.category = MDW_LIFECYCLE_CATEGORY::RESTRICTED;

    BOOST_CHECK( restricted.category == MDW_LIFECYCLE_CATEGORY::RESTRICTED );
    BOOST_CHECK( !locks.IsLocked( "sch" ) );
}


BOOST_AUTO_TEST_CASE( DirtyTracker )
{
    MDW_DIRTY_TRACKER dirty;

    BOOST_CHECK( !dirty.IsDirty( "board" ) );
    dirty.SetDirty( "board", true );
    BOOST_CHECK( dirty.IsDirty( "board" ) );
    dirty.SetDirty( "board", false );
    BOOST_CHECK( !dirty.IsDirty( "board" ) );
}


namespace
{
std::string typeName( const kiapi::mdw::MdwRequest& aRequest )
{
    const std::string&     url = aRequest.payload().type_url();
    std::string::size_type slash = url.find_last_of( '/' );

    return slash == std::string::npos ? url : url.substr( slash + 1 );
}


/// A dispatch that advertises the given capability group and answers locking.
MDW_LOOPBACK_TRANSPORT::DISPATCH lockingDispatch( bool aAdvertiseLocking )
{
    return [aAdvertiseLocking]( const kiapi::mdw::MdwRequest& aRequest,
                                kiapi::mdw::MdwResponse&      aResponse )
    {
        aResponse.set_channel_token( aRequest.channel_token() );
        aResponse.set_request_id( aRequest.request_id() );
        aResponse.mutable_error()->set_code( kiapi::workspace::WE_OK );

        std::string name = typeName( aRequest );

        if( name == "kiapi.mdw.Hello" )
        {
            kiapi::mdw::HelloReply reply;

            if( aAdvertiseLocking )
                reply.mutable_capabilities()->add_groups( kiapi::workspace::CG_LOCKING );

            aResponse.mutable_payload()->PackFrom( reply );
        }
        else if( name == "kiapi.mdw.GetSchemaRequest" )
        {
            aResponse.mutable_payload()->PackFrom( kiapi::mdw::GetSchemaReply() );
        }
        else if( name == "kiapi.mdw.LockRequest" )
        {
            kiapi::mdw::LockReply reply;
            reply.mutable_lock()->set_is_locked( true );
            reply.mutable_lock()->set_locked_by_me( true );
            reply.mutable_lock()->set_lock_owner( "me" );
            aResponse.mutable_payload()->PackFrom( reply );
        }
        else
        {
            aResponse.mutable_error()->set_code( kiapi::workspace::WE_NOT_SUPPORTED );
        }
    };
}


std::unique_ptr<MDW_BACKEND> handshakenBackend( bool aAdvertiseLocking )
{
    auto transport = std::make_unique<MDW_LOOPBACK_TRANSPORT>( lockingDispatch( aAdvertiseLocking ), 0 );
    auto backend = std::make_unique<MDW_BACKEND>( std::move( transport ), wxS( "tok" ) );
    backend->Start();
    backend->Handshake( wxS( "10.0" ), wxEmptyString, 3000 );

    return backend;
}
}


BOOST_AUTO_TEST_CASE( ServiceLockUpdatesTrackerAndBus )
{
    std::unique_ptr<MDW_BACKEND> backend = handshakenBackend( true );
    MDW_EVENT_BUS                bus;
    MDW_LOCK_TRACKER             locks;
    MDW_DIRTY_TRACKER            dirty;

    int lockedEvents = 0;
    bus.Subscribe(
            [&lockedEvents]( const MDW_EVENT& aEvent )
            {
                if( aEvent.type == MDW_EVENT_TYPE::ITEM_LOCKED )
                    ++lockedEvents;
            } );

    MDW_WORKSPACE_SERVICE service( backend.get(), &bus, &locks, &dirty, wxS( "ws" ), wxS( "client" ) );

    std::promise<MDW_ERROR_CODE> promise;
    std::future<MDW_ERROR_CODE>  future = promise.get_future();

    service.Lock( wxS( "board" ),
                  [&promise]( MDW_ERROR_CODE aCode, const MDW_LOCK_INFO& )
                  { promise.set_value( aCode ); } );

    BOOST_CHECK( future.get() == MDW_ERROR_CODE::SUCCESS );
    BOOST_CHECK( locks.IsLocked( "board" ) );
    BOOST_CHECK( locks.LockedByMe( "board" ) );
    BOOST_CHECK_EQUAL( lockedEvents, 1 );
}


BOOST_AUTO_TEST_CASE( ServiceGatesUnadvertisedCapability )
{
    std::unique_ptr<MDW_BACKEND> backend = handshakenBackend( false );
    MDW_EVENT_BUS                bus;
    MDW_LOCK_TRACKER             locks;
    MDW_DIRTY_TRACKER            dirty;

    MDW_WORKSPACE_SERVICE service( backend.get(), &bus, &locks, &dirty, wxS( "ws" ), wxS( "client" ) );

    std::promise<MDW_ERROR_CODE> promise;
    std::future<MDW_ERROR_CODE>  future = promise.get_future();

    service.Lock( wxS( "board" ),
                  [&promise]( MDW_ERROR_CODE aCode, const MDW_LOCK_INFO& )
                  { promise.set_value( aCode ); } );

    BOOST_CHECK( future.get() == MDW_ERROR_CODE::NOT_SUPPORTED );
    BOOST_CHECK( !locks.IsLocked( "board" ) );
}


BOOST_AUTO_TEST_SUITE_END()
