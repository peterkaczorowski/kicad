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
 * The backend half of the event channel was always built: the SDK pushes events
 * over a REQ socket and the supervisor names that socket at spawn. Nothing bound
 * it, so no event a backend sent ever reached the bus. This drives a real backend
 * through the real supervisor and asserts the event arrives.
 */

#include <boost/test/unit_test.hpp>

#include <mdw/mdw_backend.h>
#include <mdw/mdw_backend_supervisor.h>
#include <mdw/mdw_event_bus.h>
#include <mdw/mdw_session.h>

#include <api/mdw/backend_locking.pb.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

#include <wx/utils.h>


namespace
{
/// Wait for a predicate the ingress thread satisfies, so a slow machine reports
/// a real failure rather than a race.
template <typename Pred>
bool waitFor( std::mutex& aMutex, std::condition_variable& aCv, Pred aPred )
{
    std::unique_lock<std::mutex> lock( aMutex );

    return aCv.wait_for( lock, std::chrono::seconds( 5 ), aPred );
}
}


BOOST_AUTO_TEST_SUITE( MdwEventIngress )


BOOST_AUTO_TEST_CASE( BackendPushedEventReachesTheBus )
{
    wxSetEnv( wxS( "MDW_MOCK_CAPABILITIES" ), wxS( "LOCKING,EVENTS" ) );

    MDW_BACKEND_MANIFEST manifest;
    manifest.backend_id = wxS( "org.kicad.mdw.mock" );
    manifest.executable_path = wxString::FromUTF8( MDW_MOCK_EXECUTABLE );
    manifest.valid = true;

    auto supervisor = std::make_unique<MDW_BACKEND_SUPERVISOR>( manifest );
    BOOST_REQUIRE( supervisor->Launch( wxS( "10.0" ), 5000 ) );

    MDW_PROJECT_BINDING binding;
    binding.backend_id = wxS( "org.kicad.mdw.mock" );
    binding.workspace_id = wxS( "ws" );

    MDW_SESSION session( wxS( "/tmp/p.kicad_pro" ), binding, std::move( supervisor ) );

    BOOST_REQUIRE( session.StartEventIngress() );

    std::mutex              mutex;
    std::condition_variable cv;
    int                     locked = 0;

    session.EventBus().Subscribe(
            [&]( const MDW_EVENT& aEvent )
            {
                if( aEvent.type != MDW_EVENT_TYPE::ITEM_LOCKED )
                    return;

                {
                    std::lock_guard<std::mutex> lock( mutex );
                    ++locked;
                }

                cv.notify_all();
            } );

    // Drive the backend directly rather than through the service. The service
    // publishes its own optimistic event and the bus dedups the backend's echo of
    // it, which would hide exactly the delivery being tested here.
    kiapi::mdw::LockRequest request;
    request.set_item_id( "board.kicad_pcb" );

    kiapi::mdw::LockReply reply;
    BOOST_REQUIRE( session.Backend()->CallSync( request, reply, 5000 )
                   == MDW_ERROR_CODE::SUCCESS );

    BOOST_CHECK( waitFor( mutex, cv, [&]() { return locked > 0; } ) );

    wxUnsetEnv( wxS( "MDW_MOCK_CAPABILITIES" ) );
}


BOOST_AUTO_TEST_SUITE_END()
