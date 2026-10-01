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


/*
 * The Windows command line is one string that the child re-splits, so quoting is
 * the whole contract. It is built on every platform so it can be checked here,
 * on the machine the tests actually run on, rather than only where it is used.
 * Expectations are what the child's runtime must recover, per the documented
 * backslash-doubling rule.
 */
BOOST_AUTO_TEST_CASE( WindowsCommandLineQuoting )
{
    // Nothing special: no quotes added, so a plain git invocation stays readable.
    BOOST_CHECK_EQUAL( MDW_WindowsCommandLine( { "git", "status" } ), std::string( "git status" ) );

    // Project paths with spaces are ordinary, not an edge case.
    BOOST_CHECK_EQUAL( MDW_WindowsCommandLine( { "git", "add", "My Board.kicad_pcb" } ),
                       std::string( "git add \"My Board.kicad_pcb\"" ) );

    // A backslash is only special before a quote, so a bare Windows path is not
    // doubled and stays the path the user typed.
    BOOST_CHECK_EQUAL( MDW_WindowsCommandLine( { "git", "-C", "C:\\proj\\hw" } ),
                       std::string( "git -C C:\\proj\\hw" ) );

    // A trailing backslash inside a quoted argument would escape the closing
    // quote, so it doubles.
    BOOST_CHECK_EQUAL( MDW_WindowsCommandLine( { "git", "-C", "C:\\my proj\\" } ),
                       std::string( "git -C \"C:\\my proj\\\\\"" ) );

    // An embedded quote is escaped, and any backslashes ahead of it double.
    BOOST_CHECK_EQUAL( MDW_WindowsCommandLine( { "git", "commit", "-m", "say \"hi\"" } ),
                       std::string( "git commit -m \"say \\\"hi\\\"\"" ) );

    // An empty argument must survive as an empty argument, not vanish.
    BOOST_CHECK_EQUAL( MDW_WindowsCommandLine( { "git", "commit", "-m", "" } ),
                       std::string( "git commit -m \"\"" ) );
}


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
