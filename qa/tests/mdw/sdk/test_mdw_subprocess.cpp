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

#include <mdw_sdk/mdw_subprocess.h>

BOOST_AUTO_TEST_SUITE( MdwSubprocess )


#if !defined( _WIN32 )

BOOST_AUTO_TEST_CASE( CapturesStdoutAndExit )
{
    MDW_PROCESS_RESULT result = MDW_RunProcess( { "/bin/echo", "hello" } );

    BOOST_CHECK( result.started );
    BOOST_CHECK_EQUAL( result.exit_code, 0 );
    BOOST_CHECK_EQUAL( result.out, std::string( "hello\n" ) );
}


BOOST_AUTO_TEST_CASE( FeedsStdin )
{
    MDW_PROCESS_RESULT result = MDW_RunProcess( { "cat" }, std::string(), std::string( "piped" ) );

    BOOST_CHECK( result.started );
    BOOST_CHECK_EQUAL( result.exit_code, 0 );
    BOOST_CHECK_EQUAL( result.out, std::string( "piped" ) );
}


BOOST_AUTO_TEST_CASE( ReportsNonZeroExit )
{
    MDW_PROCESS_RESULT result = MDW_RunProcess( { "false" } );

    BOOST_CHECK( result.started );
    BOOST_CHECK( result.exit_code != 0 );
}


BOOST_AUTO_TEST_CASE( MissingProgramExits127 )
{
    MDW_PROCESS_RESULT result = MDW_RunProcess( { "this-program-does-not-exist-mdw" } );

    BOOST_CHECK_EQUAL( result.exit_code, 127 );
}

#endif


BOOST_AUTO_TEST_SUITE_END()
