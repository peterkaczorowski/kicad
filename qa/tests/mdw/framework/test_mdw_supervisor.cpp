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

#include <wx/utils.h>

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


BOOST_AUTO_TEST_SUITE_END()
