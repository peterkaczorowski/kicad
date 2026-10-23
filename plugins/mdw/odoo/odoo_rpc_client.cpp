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

#include "odoo_rpc_client.h"

#include <curl/curl.h>


namespace
{

std::size_t writeToString( char* aData, std::size_t aSize, std::size_t aCount, void* aUser )
{
    std::size_t bytes = aSize * aCount;
    static_cast<std::string*>( aUser )->append( aData, bytes );
    return bytes;
}

} // namespace


void ODOO_RPC_CLIENT::SetServer( const std::string& aServerUrl, const std::string& aDatabase )
{
    m_serverUrl = aServerUrl;
    m_database = aDatabase;

    while( !m_serverUrl.empty() && m_serverUrl.back() == '/' )
        m_serverUrl.pop_back();
}


void ODOO_RPC_CLIENT::Reset()
{
    m_password.clear();
    m_uid = 0;
    m_authenticated = false;
}


ODOO_RPC_RESULT ODOO_RPC_CLIENT::Authenticate( const std::string& aLogin,
                                               const std::string& aPassword )
{
    // Start every attempt from a clean slate so a failed re-auth cannot leave the
    // client running as the previously authenticated user.
    Reset();

    nlohmann::json params;
    params["service"] = "common";
    params["method"] = "login";
    params["args"] = nlohmann::json::array( { m_database, aLogin, aPassword } );

    ODOO_RPC_RESULT result = sendRequest( params );

    if( result.success && result.result.is_number_integer() )
    {
        int uid = result.result.get<int>();

        if( uid > 0 )
        {
            m_uid = uid;
            m_password = aPassword;
            m_authenticated = true;
            return result;
        }
    }

    result.success = false;

    if( result.error_message.empty() )
        result.error_message = "Invalid username or password";

    if( result.error_code == 0 )
        result.error_code = 401;

    return result;
}


ODOO_RPC_RESULT ODOO_RPC_CLIENT::ExecuteKw( const std::string& aModel, const std::string& aMethod,
                                            const nlohmann::json& aArgs,
                                            const nlohmann::json& aKwargs )
{
    if( !m_authenticated )
    {
        ODOO_RPC_RESULT result;
        result.error_code = 401;
        result.error_message = "Not authenticated";
        return result;
    }

    nlohmann::json args = nlohmann::json::array();
    args.push_back( m_database );
    args.push_back( m_uid );
    args.push_back( m_password );
    args.push_back( aModel );
    args.push_back( aMethod );
    args.push_back( aArgs );
    args.push_back( aKwargs );

    nlohmann::json params;
    params["service"] = "object";
    params["method"] = "execute_kw";
    params["args"] = args;

    return sendRequest( params );
}


ODOO_RPC_RESULT ODOO_RPC_CLIENT::SearchRead( const std::string& aModel, const nlohmann::json& aDomain,
                                             const nlohmann::json& aFields, int aLimit )
{
    nlohmann::json args = nlohmann::json::array();
    args.push_back( aDomain );

    nlohmann::json kwargs;
    kwargs["fields"] = aFields;

    if( aLimit > 0 )
        kwargs["limit"] = aLimit;

    return ExecuteKw( aModel, "search_read", args, kwargs );
}


ODOO_RPC_RESULT ODOO_RPC_CLIENT::Read( const std::string& aModel, const nlohmann::json& aIds,
                                       const nlohmann::json& aFields )
{
    nlohmann::json args = nlohmann::json::array();
    args.push_back( aIds );
    args.push_back( aFields );

    return ExecuteKw( aModel, "read", args );
}


ODOO_RPC_RESULT ODOO_RPC_CLIENT::Write( const std::string& aModel, const nlohmann::json& aIds,
                                        const nlohmann::json& aValues )
{
    nlohmann::json args = nlohmann::json::array();
    args.push_back( aIds );
    args.push_back( aValues );

    return ExecuteKw( aModel, "write", args );
}


ODOO_RPC_RESULT ODOO_RPC_CLIENT::sendRequest( const nlohmann::json& aParams )
{
    ODOO_RPC_RESULT result;

    CURL* curl = curl_easy_init();

    if( !curl )
    {
        result.error_message = "curl init failed";
        result.error_code = -1;
        return result;
    }

    nlohmann::json envelope;
    envelope["jsonrpc"] = "2.0";
    envelope["method"] = "call";
    envelope["params"] = aParams;
    envelope["id"] = ++m_requestId;

    std::string        body = envelope.dump();
    std::string        response;
    std::string        url = m_serverUrl + "/jsonrpc";
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append( headers, "Content-Type: application/json" );
    headers = curl_slist_append( headers, "Accept: application/json" );

    curl_easy_setopt( curl, CURLOPT_URL, url.c_str() );
    curl_easy_setopt( curl, CURLOPT_HTTPHEADER, headers );
    curl_easy_setopt( curl, CURLOPT_POSTFIELDS, body.c_str() );
    curl_easy_setopt( curl, CURLOPT_POSTFIELDSIZE, static_cast<long>( body.size() ) );
    curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, writeToString );
    curl_easy_setopt( curl, CURLOPT_WRITEDATA, &response );
    curl_easy_setopt( curl, CURLOPT_TIMEOUT, m_timeoutSeconds );
    curl_easy_setopt( curl, CURLOPT_CONNECTTIMEOUT, 10L );
    curl_easy_setopt( curl, CURLOPT_NOSIGNAL, 1L );

    CURLcode code = curl_easy_perform( curl );

    long httpStatus = 0;
    curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &httpStatus );

    curl_slist_free_all( headers );
    curl_easy_cleanup( curl );

    if( code != CURLE_OK )
    {
        result.success = false;
        result.curl_code = static_cast<int>( code );
        result.error_code = static_cast<int>( code );
        result.error_message = curl_easy_strerror( code );
        return result;
    }

    return parseResponse( response, static_cast<int>( httpStatus ), static_cast<int>( code ) );
}


ODOO_RPC_RESULT ODOO_RPC_CLIENT::parseResponse( const std::string& aBody, int aHttpStatus,
                                                int aCurlCode )
{
    ODOO_RPC_RESULT result;
    result.http_status = aHttpStatus;
    result.curl_code = aCurlCode;

    nlohmann::json response = nlohmann::json::parse( aBody, nullptr, false );

    if( response.is_discarded() )
    {
        result.success = false;
        result.error_message = "Invalid JSON-RPC response body";
        return result;
    }

    if( response.contains( "error" ) && response["error"].is_object() )
    {
        const nlohmann::json& error = response["error"];
        result.success = false;

        if( error.contains( "code" ) && error["code"].is_number_integer() )
            result.error_code = error["code"].get<int>();

        if( error.contains( "message" ) && error["message"].is_string() )
            result.error_message = error["message"].get<std::string>();

        if( error.contains( "data" ) && error["data"].is_object() )
        {
            const nlohmann::json& data = error["data"];

            if( data.contains( "message" ) && data["message"].is_string() )
                result.error_data = data["message"].get<std::string>();
            else if( data.contains( "debug" ) && data["debug"].is_string() )
                result.error_data = data["debug"].get<std::string>();
        }

        return result;
    }

    if( response.contains( "result" ) )
    {
        result.success = true;
        result.result = response["result"];
        return result;
    }

    result.success = false;
    result.error_message = "JSON-RPC response missing result and error";
    return result;
}
