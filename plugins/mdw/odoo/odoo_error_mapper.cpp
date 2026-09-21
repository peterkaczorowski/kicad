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
#include <string_view>
#include <vector>

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


/// Classify a curl transport failure. Returns WE_OK when aResult is not a curl error.
ws::WorkspaceErrorCode fromCurl( const ODOO_RPC_RESULT& aResult )
{
    if( aResult.curl_code == CURLE_OK )
        return ws::WE_OK;

    // Every other curl failure, resolve, connect and TLS alike, is a network
    // problem as far as the workspace protocol is concerned.
    return aResult.curl_code == CURLE_OPERATION_TIMEDOUT ? ws::WE_TIMEOUT : ws::WE_NETWORK;
}


/// A JSON-RPC fault classification. A fault matches on its numeric code or on
/// any of its phrases appearing in the lowercased message and data.
struct FAULT_RULE
{
    int                              json_rpc_code;   ///< 0 when only phrases apply
    std::vector<std::string_view>    phrases;
    ws::WorkspaceErrorCode           code;
};


const std::vector<FAULT_RULE>& faultRules()
{
    // Ordered most specific first: an "access denied" authentication failure must
    // classify as auth rather than as a bare permission problem.
    static const std::vector<FAULT_RULE> rules = {
        { 401, { "session expired", "invalid session", "authentication", "access denied" },
          ws::WE_AUTH_FAILED },
        { 100, {}, ws::WE_AUTH_FAILED },
        { 403, { "permission", "not allowed", "forbidden", "accesserror" },
          ws::WE_PERMISSION_DENIED },
        { 404, { "not found", "does not exist", "missingrecord" }, ws::WE_ITEM_NOT_FOUND },
        { 0, { "timeout" }, ws::WE_TIMEOUT },
        { 0, { "connection", "network", "unreachable" }, ws::WE_NETWORK },
    };

    return rules;
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

        auto matches =
                [&]( const FAULT_RULE& aRule )
                {
                    if( aRule.json_rpc_code != 0 && aResult.error_code == aRule.json_rpc_code )
                        return true;

                    return std::any_of( aRule.phrases.begin(), aRule.phrases.end(),
                                        [&]( std::string_view aPhrase )
                                        { return haystack.find( aPhrase ) != std::string::npos; } );
                };

        const std::vector<FAULT_RULE>& rules = faultRules();
        auto hit = std::find_if( rules.begin(), rules.end(), matches );

        if( hit != rules.end() )
        {
            code = hit->code;
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
