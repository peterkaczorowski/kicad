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

#include <boost/test/unit_test.hpp>

#include <mdw/mdw_restart_policy.h>

BOOST_AUTO_TEST_SUITE( MdwRestartPolicy )


BOOST_AUTO_TEST_CASE( BackoffSchedule )
{
    MDW_RESTART_POLICY policy;

    BOOST_CHECK_EQUAL( policy.NextDelayMs( 0 ), 0 );
    BOOST_CHECK_EQUAL( policy.NextDelayMs( 100 ), 2000 );
    BOOST_CHECK_EQUAL( policy.NextDelayMs( 200 ), 10000 );

    // A fourth restart inside the window gives up.
    BOOST_CHECK_EQUAL( policy.NextDelayMs( 300 ), -1 );
}


BOOST_AUTO_TEST_CASE( WindowExpiryResets )
{
    MDW_RESTART_POLICY policy;

    policy.NextDelayMs( 0 );
    policy.NextDelayMs( 1000 );
    policy.NextDelayMs( 2000 );
    BOOST_CHECK_EQUAL( policy.NextDelayMs( 3000 ), -1 );

    // Once every attempt (the last at t=2000) ages out of the window, restarts
    // are allowed again from the start of the schedule.
    std::int64_t afterWindow = 2000 + MDW_RESTART_POLICY::WINDOW_MS + 1;
    BOOST_CHECK_EQUAL( policy.NextDelayMs( afterWindow ), 0 );
}


BOOST_AUTO_TEST_CASE( ExplicitReset )
{
    MDW_RESTART_POLICY policy;

    policy.NextDelayMs( 0 );
    policy.NextDelayMs( 100 );
    policy.NextDelayMs( 200 );
    policy.Reset();

    BOOST_CHECK_EQUAL( policy.NextDelayMs( 300 ), 0 );
}


BOOST_AUTO_TEST_SUITE_END()
