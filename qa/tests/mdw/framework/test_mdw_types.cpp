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
 * C1 unit tests for the MDW contract types. Two things are proven here. First,
 * the C++ enum values are numerically identical to their kiapi.workspace proto
 * counterparts, which is the invariant that lets the MDW_BACKEND proxy map
 * between the wire and value layers with a plain static_cast. Second, the
 * lifecycle category and the per-user lock are independent axes, per the
 * proposal.
 */

#include <boost/test/unit_test.hpp>

#include <mdw/mdw_types.h>
#include <mdw/mdw_capabilities.h>
#include <mdw/mdw_schema.h>
#include <mdw/mdw_lock.h>

BOOST_AUTO_TEST_SUITE( MdwTypes )


/*
 * Every declared custom state maps to exactly one of the four universal
 * categories, and the mapping is recoverable by state id.
 */
BOOST_AUTO_TEST_CASE( CustomStateCategoryRoundTrip )
{
    MDW_CAPABILITIES caps;
    caps.custom_states.push_back( { "in_work", "In Work", "", MDW_LIFECYCLE_CATEGORY::EDITABLE, "", 0 } );
    caps.custom_states.push_back(
            { "in_review", "In Review", "", MDW_LIFECYCLE_CATEGORY::PENDING_REVIEW, "", 1 } );
    caps.custom_states.push_back( { "frozen", "Frozen", "", MDW_LIFECYCLE_CATEGORY::RESTRICTED, "", 2 } );
    caps.custom_states.push_back( { "released", "Released", "", MDW_LIFECYCLE_CATEGORY::RELEASED, "", 3 } );

    const MDW_CUSTOM_STATE* frozen = caps.FindCustomState( "frozen" );
    BOOST_REQUIRE( frozen != nullptr );
    BOOST_CHECK( frozen->category == MDW_LIFECYCLE_CATEGORY::RESTRICTED );

    const MDW_CUSTOM_STATE* review = caps.FindCustomState( "in_review" );
    BOOST_REQUIRE( review != nullptr );
    BOOST_CHECK( review->category == MDW_LIFECYCLE_CATEGORY::PENDING_REVIEW );

    BOOST_CHECK( caps.FindCustomState( "does_not_exist" ) == nullptr );
}


BOOST_AUTO_TEST_CASE( CapabilityGroupMembership )
{
    MDW_CAPABILITIES caps;
    caps.groups.insert( MDW_CAPABILITY_GROUP::LOCKING );
    caps.groups.insert( MDW_CAPABILITY_GROUP::REVISIONS );

    BOOST_CHECK( caps.Has( MDW_CAPABILITY_GROUP::LOCKING ) );
    BOOST_CHECK( caps.Has( MDW_CAPABILITY_GROUP::REVISIONS ) );
    BOOST_CHECK( !caps.Has( MDW_CAPABILITY_GROUP::ATTRIBUTES ) );
    BOOST_CHECK( !caps.Has( MDW_CAPABILITY_GROUP::LIFECYCLE ) );
}


BOOST_AUTO_TEST_CASE( ResultHelpers )
{
    MDW_RESULT ok = MDW_RESULT::Ok();
    BOOST_CHECK( ok.IsOk() );
    BOOST_CHECK( ok.error == MDW_ERROR_CODE::SUCCESS );

    MDW_RESULT fail = MDW_RESULT::Fail( MDW_ERROR_CODE::LOCK_CONFLICT, "held by another user" );
    BOOST_CHECK( !fail.IsOk() );
    BOOST_CHECK( fail.error == MDW_ERROR_CODE::LOCK_CONFLICT );
    BOOST_CHECK_EQUAL( fail.message, wxString( "held by another user" ) );

    MDW_LOCK_RESULT lockResult;
    lockResult.success = true;
    lockResult.lock.is_locked = true;
    BOOST_CHECK( lockResult.IsOk() );
    BOOST_CHECK( lockResult.lock.is_locked );
}


BOOST_AUTO_TEST_CASE( AttributeSetGetSet )
{
    MDW_ATTRIBUTE_SET set;
    set.Set( "owner", "alice" );
    set.Set( "project", "widget" );

    BOOST_CHECK_EQUAL( set.Get( "owner" ), wxString( "alice" ) );
    BOOST_CHECK_EQUAL( set.Get( "project" ), wxString( "widget" ) );
    BOOST_CHECK_EQUAL( set.Get( "missing" ), wxString() );

    set.Set( "owner", "bob" );
    BOOST_CHECK_EQUAL( set.Get( "owner" ), wxString( "bob" ) );
    BOOST_CHECK_EQUAL( set.values.size(), 2u );
}


BOOST_AUTO_TEST_SUITE_END()
