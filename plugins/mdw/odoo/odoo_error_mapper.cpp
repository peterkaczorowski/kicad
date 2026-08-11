/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2026 KiCad Developers, see AUTHORS.txt for contributors.
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

#include "odoo_error_mapper.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <string>

namespace ws = kiapi::workspace;

namespace
{

std::string lowered( const std::string& aText )
{
    std::string out = aText;
    std::transform( out.begin(), out.end(), out.begin(),
                    []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
    return out;
}


bool mentions( const std::string& aHaystack, const char* aNeedle )
{
    return aHaystack.find( aNeedle ) != std::string::npos;
}


/// Classify a curl transport failure. Returns WE_OK when aResult is not a curl error.
ws::WorkspaceErrorCode fromCurl( const ODOO_RPC_RESULT& aResult )
{
    if( aResult.curl_code == CURLE_OK )
        return ws::WE_OK;

    switch( aResult.curl_code )
    {
    case CURLE_OPERATION_TIMEDOUT:
        return ws::WE_TIMEOUT;

    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_CONNECT:
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_PEER_FAILED_VERIFICATION:
        return ws::WE_NETWORK;

    default:
        return ws::WE_NETWORK;
    }
}


/// Classify an HTTP status when the body carried no JSON-RPC fault to key off.
/// Returns WE_OK for a 2xx status so the caller can keep its message-based result.
ws::WorkspaceErrorCode fromHttpStatus( int aStatus )
{
    if( aStatus >= 200 && aStatus < 300 )
        return ws::WE_OK;

    switch( aStatus )
    {
    case 401:
        return ws::WE_NOT_AUTHENTICATED;

    case 403:
        return ws::WE_PERMISSION_DENIED;

    case 404:
        return ws::WE_ITEM_NOT_FOUND;

    default:
        break;
    }

    if( aStatus >= 500 && aStatus < 600 )
        return ws::WE_NETWORK;

    return ws::WE_INTERNAL;
}

} // namespace


ws::WorkspaceError OdooMapError( const ODOO_RPC_RESULT& aResult,
                                 ws::WorkspaceErrorCode aFallback )
{
    ws::WorkspaceError error;

    if( aResult.success )
    {
        error.set_code( ws::WE_OK );
        return error;
    }

    ws::WorkspaceErrorCode code = fromCurl( aResult );

    if( code == ws::WE_OK )
    {
        const std::string haystack = lowered( aResult.error_message + " " + aResult.error_data );

        if( aResult.error_code == 401 || aResult.error_code == 100
            || mentions( haystack, "session expired" ) || mentions( haystack, "invalid session" )
            || mentions( haystack, "authentication" ) || mentions( haystack, "access denied" ) )
        {
            code = ws::WE_AUTH_FAILED;
        }
        else if( aResult.error_code == 403 || mentions( haystack, "permission" )
                 || mentions( haystack, "not allowed" ) || mentions( haystack, "forbidden" )
                 || mentions( haystack, "accesserror" ) )
        {
            code = ws::WE_PERMISSION_DENIED;
        }
        else if( aResult.error_code == 404 || mentions( haystack, "not found" )
                 || mentions( haystack, "does not exist" ) || mentions( haystack, "missingrecord" ) )
        {
            code = ws::WE_ITEM_NOT_FOUND;
        }
        else if( mentions( haystack, "timeout" ) )
        {
            code = ws::WE_TIMEOUT;
        }
        else if( mentions( haystack, "connection" ) || mentions( haystack, "network" )
                 || mentions( haystack, "unreachable" ) )
        {
            code = ws::WE_NETWORK;
        }
        else
        {
            // The body carried no recognizable JSON-RPC fault, so fold in the HTTP
            // status. A non-JSON error page (proxy 401 HTML, gateway 5xx) lands here.
            ws::WorkspaceErrorCode httpCode = fromHttpStatus( aResult.http_status );
            code = ( httpCode == ws::WE_OK ) ? aFallback : httpCode;
        }
    }

    error.set_code( code );

    std::string message = aResult.error_message;

    if( !aResult.error_data.empty() )
        message += message.empty() ? aResult.error_data : ( ": " + aResult.error_data );

    if( message.empty() )
        message = "Odoo request failed";

    error.set_message( message );
    return error;
}
