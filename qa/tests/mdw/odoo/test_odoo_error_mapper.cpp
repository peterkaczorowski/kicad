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
 * Odoo reports faults as free-form prose, so the mapper classifies on phrases as
 * well as codes. These cover the precedence between those two signals and the
 * fall-through to the HTTP status, which is what a live server's proxy returns.
 */

#include <boost/test/unit_test.hpp>

#include <odoo_error_mapper.h>

#include <curl/curl.h>

namespace ws = kiapi::workspace;


namespace
{
ODOO_RPC_RESULT fault( int aCode, const std::string& aMessage, int aHttpStatus = 200 )
{
    ODOO_RPC_RESULT result;
    result.success = false;
    result.error_code = aCode;
    result.error_message = aMessage;
    result.http_status = aHttpStatus;

    return result;
}
}


BOOST_AUTO_TEST_SUITE( OdooErrorMapper )


BOOST_AUTO_TEST_CASE( ClassifiesFaultsByPhrase )
{
    // Odoo returns code 200 with the real fault in the message, so the phrase is
    // the only signal available.
    BOOST_CHECK( OdooMapError( fault( 200, "Odoo Session Expired" ) ).code()
                 == ws::WE_AUTH_FAILED );
    BOOST_CHECK( OdooMapError( fault( 200, "psycopg2.AccessError" ) ).code()
                 == ws::WE_PERMISSION_DENIED );
    BOOST_CHECK( OdooMapError( fault( 200, "Record does not exist" ) ).code()
                 == ws::WE_ITEM_NOT_FOUND );
    BOOST_CHECK( OdooMapError( fault( 200, "host unreachable" ) ).code() == ws::WE_NETWORK );
}


BOOST_AUTO_TEST_CASE( AuthenticationOutranksPermission )
{
    // "access denied" carries both an auth and a permission reading; an expired
    // login must not be reported as a permission problem the user cannot fix.
    BOOST_CHECK( OdooMapError( fault( 200, "Access Denied" ) ).code() == ws::WE_AUTH_FAILED );
}


BOOST_AUTO_TEST_CASE( FallsBackToHttpStatus )
{
    // A proxy or gateway answers with an HTML body, so no phrase matches.
    BOOST_CHECK( OdooMapError( fault( 0, "<html>Unauthorized</html>", 401 ) ).code()
                 == ws::WE_NOT_AUTHENTICATED );
    BOOST_CHECK( OdooMapError( fault( 0, "Bad Gateway", 502 ) ).code() == ws::WE_NETWORK );

    // Nothing matches and the status is 2xx, so the caller's fallback stands.
    BOOST_CHECK( OdooMapError( fault( 0, "something odd", 200 ), ws::WE_INTERNAL ).code()
                 == ws::WE_INTERNAL );
}


BOOST_AUTO_TEST_CASE( CurlFailureOutranksTheBody )
{
    ODOO_RPC_RESULT result = fault( 404, "not found" );
    result.curl_code = CURLE_OPERATION_TIMEDOUT;

    // The transport never delivered a body, so the message must not be believed.
    BOOST_CHECK( OdooMapError( result ).code() == ws::WE_TIMEOUT );

    result.curl_code = CURLE_COULDNT_RESOLVE_HOST;
    BOOST_CHECK( OdooMapError( result ).code() == ws::WE_NETWORK );
}


BOOST_AUTO_TEST_CASE( SuccessMapsToOk )
{
    ODOO_RPC_RESULT result;
    result.success = true;

    BOOST_CHECK( OdooMapError( result ).code() == ws::WE_OK );
}


BOOST_AUTO_TEST_SUITE_END()
