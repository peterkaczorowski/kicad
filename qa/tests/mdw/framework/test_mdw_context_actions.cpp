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
 * The context-action registry gates external clients behind a one-time consent
 * decision per identity. These tests exercise that gate without a GUI by
 * injecting the consent decision.
 */

#include <boost/test/unit_test.hpp>

#include <mdw/mdw_context_action_registry.h>


BOOST_AUTO_TEST_SUITE( MdwContextActions )


static MDW_CONTEXT_ACTION makeAction( const wxString& aClient )
{
    MDW_CONTEXT_ACTION action;
    action.client_identity = aClient;
    action.item_type_predicate = wxS( "schematic" );
    action.label = wxS( "Open in tool" );
    action.target_method = wxS( "open" );

    return action;
}


BOOST_AUTO_TEST_CASE( DeniedWithNoConsentHandler )
{
    MDW_CONTEXT_ACTION_REGISTRY registry;

    MDW_CONTEXT_ACTION action = makeAction( wxS( "client-a" ) );

    BOOST_CHECK( !registry.Register( action ) );
    BOOST_CHECK( registry.Actions().empty() );
}


BOOST_AUTO_TEST_CASE( ConsentPromptedOncePerIdentity )
{
    MDW_CONTEXT_ACTION_REGISTRY registry;

    int prompts = 0;
    registry.SetConsentHandler(
            [&]( const wxString& ) -> bool
            {
                prompts++;
                return true;
            } );

    MDW_CONTEXT_ACTION first = makeAction( wxS( "client-a" ) );
    MDW_CONTEXT_ACTION second = makeAction( wxS( "client-a" ) );

    BOOST_CHECK( registry.Register( first ) );
    BOOST_CHECK( registry.Register( second ) );

    BOOST_CHECK_EQUAL( prompts, 1 );
    BOOST_CHECK( !first.action_id.IsEmpty() );
    BOOST_CHECK( first.action_id != second.action_id );
    BOOST_CHECK_EQUAL( registry.Actions().size(), 2 );
}


BOOST_AUTO_TEST_CASE( DenialIsRememberedAndNotReprompted )
{
    MDW_CONTEXT_ACTION_REGISTRY registry;

    int prompts = 0;
    registry.SetConsentHandler(
            [&]( const wxString& ) -> bool
            {
                prompts++;
                return false;
            } );

    MDW_CONTEXT_ACTION first = makeAction( wxS( "client-a" ) );
    MDW_CONTEXT_ACTION second = makeAction( wxS( "client-a" ) );

    BOOST_CHECK( !registry.Register( first ) );
    BOOST_CHECK( !registry.Register( second ) );

    BOOST_CHECK_EQUAL( prompts, 1 );
    BOOST_CHECK( registry.Actions().empty() );
}


BOOST_AUTO_TEST_CASE( UnregisterRemovesOnlyTheNamedAction )
{
    MDW_CONTEXT_ACTION_REGISTRY registry;

    registry.SetConsentHandler( []( const wxString& ) { return true; } );

    MDW_CONTEXT_ACTION first = makeAction( wxS( "client-a" ) );
    MDW_CONTEXT_ACTION second = makeAction( wxS( "client-a" ) );

    registry.Register( first );
    registry.Register( second );

    BOOST_CHECK( registry.Unregister( first.action_id ) );
    BOOST_CHECK( !registry.Unregister( wxS( "no-such-id" ) ) );

    std::vector<MDW_CONTEXT_ACTION> remaining = registry.Actions();
    BOOST_REQUIRE_EQUAL( remaining.size(), 1 );
    BOOST_CHECK( remaining[0].action_id == second.action_id );
}


BOOST_AUTO_TEST_SUITE_END()
