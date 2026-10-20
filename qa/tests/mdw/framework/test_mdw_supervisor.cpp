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
 * Spawns the real kicad-mdw-mock backend through the supervisor, completes the
 * handshake, verifies the advertised capabilities came across, probes liveness,
 * and shuts the process down.
 */

#include <boost/test/unit_test.hpp>

#include <mdw/mdw_backend_supervisor.h>
#include <mdw/mdw_backend.h>
#include <mdw/mdw_credential_store.h>
#include <mdw/mdw_session.h>

#include <memory>

#include <wx/utils.h>


namespace
{
MDW_BACKEND_MANIFEST mockManifest()
{
    MDW_BACKEND_MANIFEST manifest;
    manifest.backend_id = wxS( "org.kicad.mdw.mock" );
    manifest.display_name = wxS( "Mock Workspace" );
    manifest.executable_path = wxString::FromUTF8( MDW_MOCK_EXECUTABLE );
    manifest.valid = true;

    return manifest;
}
}

BOOST_AUTO_TEST_SUITE( MdwSupervisor )


BOOST_AUTO_TEST_CASE( SpawnHandshakeShutdown )
{
    wxSetEnv( wxS( "MDW_MOCK_CAPABILITIES" ), wxS( "LOCKING,REVISIONS,ATTRIBUTES" ) );

    MDW_BACKEND_MANIFEST manifest;
    manifest.backend_id = wxS( "org.kicad.mdw.mock" );
    manifest.display_name = wxS( "Mock Workspace" );
    manifest.executable_path = wxString::FromUTF8( MDW_MOCK_EXECUTABLE );
    manifest.valid = true;

    MDW_BACKEND_SUPERVISOR supervisor( manifest );

    BOOST_REQUIRE( supervisor.Launch( wxS( "10.0" ), 5000 ) );
    BOOST_CHECK( supervisor.Online() );

    MDW_BACKEND* backend = supervisor.Backend();
    BOOST_REQUIRE( backend != nullptr );
    BOOST_CHECK_EQUAL( backend->Info().backend_id, wxString( "org.kicad.mdw.mock" ) );
    BOOST_CHECK( backend->Has( MDW_CAPABILITY_GROUP::LOCKING ) );
    BOOST_CHECK( backend->Has( MDW_CAPABILITY_GROUP::REVISIONS ) );
    BOOST_CHECK( backend->Has( MDW_CAPABILITY_GROUP::ATTRIBUTES ) );
    BOOST_CHECK( !backend->Has( MDW_CAPABILITY_GROUP::FORGE ) );

    BOOST_CHECK( supervisor.Ping( 2000 ) );

    supervisor.Shutdown();
    BOOST_CHECK( !supervisor.Online() );
}


/*
 * Most backends need no credentials, and the git reference backend is one, so
 * the framework must not make an unauthenticated session out of a backend that
 * considers itself authenticated already. The keychain is never consulted on
 * this path, which is what lets this run without one.
 */
BOOST_AUTO_TEST_CASE( AuthenticateAcceptsBackendNeedingNoCredentials )
{
    wxUnsetEnv( wxS( "MDW_MOCK_REQUIRE_AUTH" ) );

    auto supervisor = std::make_unique<MDW_BACKEND_SUPERVISOR>( mockManifest() );
    BOOST_REQUIRE( supervisor->Launch( wxS( "10.0" ), 5000 ) );

    MDW_PROJECT_BINDING binding;
    binding.backend_id = wxS( "org.kicad.mdw.mock" );
    binding.workspace_id = wxS( "ws" );

    MDW_SESSION          session( wxS( "/tmp/p.kicad_pro" ), binding, std::move( supervisor ) );
    MDW_CREDENTIAL_STORE store;

    BOOST_CHECK( session.Authenticate( store, 5000 ) );
    BOOST_CHECK( session.Authenticated() );
}


/*
 * A backend that does require credentials is asked for them. The store is empty
 * here, so this pins the framework issuing the Authenticate call at all, which
 * is what was missing: every backend implemented the handler and nothing called
 * it.
 */
BOOST_AUTO_TEST_CASE( AuthenticateCallsBackendThatRequiresCredentials )
{
    wxSetEnv( wxS( "MDW_MOCK_REQUIRE_AUTH" ), wxS( "1" ) );

    auto supervisor = std::make_unique<MDW_BACKEND_SUPERVISOR>( mockManifest() );
    BOOST_REQUIRE( supervisor->Launch( wxS( "10.0" ), 5000 ) );

    MDW_PROJECT_BINDING binding;
    binding.backend_id = wxS( "org.kicad.mdw.mock" );
    binding.workspace_id = wxS( "ws" );

    MDW_SESSION          session( wxS( "/tmp/p.kicad_pro" ), binding, std::move( supervisor ) );
    MDW_CREDENTIAL_STORE store;

    BOOST_CHECK( session.Authenticate( store, 5000 ) );
    BOOST_CHECK( session.Authenticated() );

    wxUnsetEnv( wxS( "MDW_MOCK_REQUIRE_AUTH" ) );
}


/*
 * A rejected credential leaves the session open but unauthenticated, so the UI
 * can say so and offer Configure rather than the project looking unbound.
 */
BOOST_AUTO_TEST_CASE( AuthenticateFailureLeavesSessionUnauthenticated )
{
    wxSetEnv( wxS( "MDW_MOCK_REQUIRE_AUTH" ), wxS( "1" ) );
    wxSetEnv( wxS( "MDW_MOCK_FAIL_AUTH" ), wxS( "1" ) );

    auto supervisor = std::make_unique<MDW_BACKEND_SUPERVISOR>( mockManifest() );
    BOOST_REQUIRE( supervisor->Launch( wxS( "10.0" ), 5000 ) );

    MDW_PROJECT_BINDING binding;
    binding.backend_id = wxS( "org.kicad.mdw.mock" );
    binding.workspace_id = wxS( "ws" );

    MDW_SESSION          session( wxS( "/tmp/p.kicad_pro" ), binding, std::move( supervisor ) );
    MDW_CREDENTIAL_STORE store;

    BOOST_CHECK( !session.Authenticate( store, 5000 ) );
    BOOST_CHECK( !session.Authenticated() );
    BOOST_CHECK( session.Online() );

    wxUnsetEnv( wxS( "MDW_MOCK_FAIL_AUTH" ) );
    wxUnsetEnv( wxS( "MDW_MOCK_REQUIRE_AUTH" ) );
}


/*
 * A backend that dies on its own used to take the session down silently, because
 * nothing ever noticed. These pin the two halves of the recovery: that an
 * unasked-for exit is distinguished from a Shutdown, and that a respawn produces
 * a working backend rather than just a live process.
 */
BOOST_AUTO_TEST_CASE( CrashIsDetectedAndTheBackendRespawns )
{
    MDW_BACKEND_SUPERVISOR supervisor( mockManifest() );

    BOOST_REQUIRE( supervisor.Launch( wxS( "10.0" ), 5000 ) );
    BOOST_REQUIRE( supervisor.Online() );
    BOOST_CHECK( !supervisor.CrashDetected() );

    const long pid = supervisor.ProcessId();
    BOOST_REQUIRE( pid > 0 );

    wxKill( pid, wxSIGKILL );

    // The signal is asynchronous, so poll rather than assume the child is already
    // reapable on the next line.
    bool detected = false;

    for( int i = 0; i < 50 && !detected; ++i )
    {
        detected = supervisor.CrashDetected();

        if( !detected )
            wxMilliSleep( 100 );
    }

    BOOST_REQUIRE( detected );
    BOOST_CHECK( !supervisor.Online() );

    BOOST_REQUIRE( supervisor.Restart( wxS( "10.0" ), 5000 ) );
    BOOST_CHECK( supervisor.Online() );
    BOOST_CHECK( supervisor.ProcessId() != pid );

    // A respawn that cannot answer is not a recovery.
    BOOST_CHECK( supervisor.Ping( 2000 ) );

    supervisor.Shutdown();
}


BOOST_AUTO_TEST_CASE( ShutdownIsNotReportedAsACrash )
{
    MDW_BACKEND_SUPERVISOR supervisor( mockManifest() );

    BOOST_REQUIRE( supervisor.Launch( wxS( "10.0" ), 5000 ) );
    supervisor.Shutdown();

    // The process is gone, but we asked for that, so nothing should try to
    // resurrect it.
    BOOST_CHECK( !supervisor.CrashDetected() );
    BOOST_CHECK( !supervisor.Restart( wxS( "10.0" ), 5000 ) );
}


BOOST_AUTO_TEST_SUITE_END()
