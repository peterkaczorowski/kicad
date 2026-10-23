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

/**
 * @file odoo_rpc_client.h
 * @brief A wx-free libcurl JSON-RPC 2.0 client for the Odoo web API.
 *
 * The Odoo backend links kiapi, nng and curl only, so this client speaks plain
 * std::string and nlohmann::json. It POSTs the standard {"jsonrpc","method",
 * "params","id"} envelope to <url>/jsonrpc, authenticates through the "common"
 * service login, and drives model calls through object.execute_kw.
 */

#ifndef ODOO_RPC_CLIENT_H
#define ODOO_RPC_CLIENT_H

#include <nlohmann/json.hpp>

#include <string>

/// The outcome of one JSON-RPC call. On success result holds the "result" node;
/// on failure the error_* fields carry the Odoo fault or the curl transport code.
struct ODOO_RPC_RESULT
{
    bool           success = false;
    nlohmann::json result;
    int            http_status = 0;
    int            curl_code = 0;
    int            error_code = 0;
    std::string    error_message;
    std::string    error_data;
};


class ODOO_RPC_CLIENT
{
public:
    /// Set the base server url (trailing slash trimmed) and the database name.
    void SetServer( const std::string& aServerUrl, const std::string& aDatabase );

    /// Log in through the "common" service and cache the uid and password for the
    /// execute_kw calls that follow. On success the returned result holds the uid.
    ODOO_RPC_RESULT Authenticate( const std::string& aLogin, const std::string& aPassword );

    void Reset();

    bool IsAuthenticated() const { return m_authenticated; }

    int Uid() const { return m_uid; }

    /// Invoke aMethod on aModel through object.execute_kw. aArgs are the model
    /// method's positional arguments; aKwargs its keyword arguments.
    ODOO_RPC_RESULT ExecuteKw( const std::string& aModel, const std::string& aMethod,
                               const nlohmann::json& aArgs,
                               const nlohmann::json& aKwargs = nlohmann::json::object() );

    ODOO_RPC_RESULT SearchRead( const std::string& aModel, const nlohmann::json& aDomain,
                                const nlohmann::json& aFields, int aLimit = 0 );

    ODOO_RPC_RESULT Read( const std::string& aModel, const nlohmann::json& aIds,
                          const nlohmann::json& aFields );

    ODOO_RPC_RESULT Write( const std::string& aModel, const nlohmann::json& aIds,
                           const nlohmann::json& aValues );

private:
    /// POST the JSON-RPC envelope wrapping aParams and parse the response.
    ODOO_RPC_RESULT sendRequest( const nlohmann::json& aParams );

    ODOO_RPC_RESULT parseResponse( const std::string& aBody, int aHttpStatus, int aCurlCode );

    std::string m_serverUrl;
    std::string m_database;
    std::string m_password;
    int         m_uid = 0;
    bool        m_authenticated = false;
    long        m_timeoutSeconds = 30;
    long long   m_requestId = 0;
};

#endif // ODOO_RPC_CLIENT_H
