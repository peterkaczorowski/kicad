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

#include "mock_odoo_server.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{

const int UID = 7;


/// The two canned records the mock model serves, mirroring the mdw.item schema.
nlohmann::json records()
{
    nlohmann::json a;
    a["id"] = 101;
    a["name"] = "widget-a";
    a["folder"] = "";
    a["state"] = "draft";
    a["attributes"] = "{\"owner\":\"alice\",\"status\":\"draft\"}";

    nlohmann::json b;
    b["id"] = 102;
    b["name"] = "widget-b";
    b["folder"] = "";
    b["state"] = "review";
    b["attributes"] = "{\"owner\":\"bob\",\"status\":\"review\"}";

    return nlohmann::json::array( { a, b } );
}


/// Compute the "result" node for one parsed JSON-RPC request.
nlohmann::json dispatch( const nlohmann::json& aRequest )
{
    if( !aRequest.contains( "params" ) )
        return false;

    const nlohmann::json& params = aRequest["params"];
    std::string           method = params.value( "method", std::string() );

    if( method == "login" )
        return UID;

    if( method == "execute_kw" )
    {
        const nlohmann::json& args = params["args"];

        if( !args.is_array() || args.size() < 6 )
            return false;

        std::string modelMethod = args[4].get<std::string>();

        if( modelMethod == "search_read" )
            return records();

        if( modelMethod == "read" )
        {
            nlohmann::json ids = args[5][0];
            nlohmann::json out = nlohmann::json::array();

            for( const nlohmann::json& record : records() )
            {
                for( const nlohmann::json& id : ids )
                {
                    if( id == record["id"] )
                        out.push_back( record );
                }
            }

            return out.empty() ? records() : out;
        }

        if( modelMethod == "write" )
            return true;
    }

    return false;
}


/// Read an HTTP request off aFd until the headers and the Content-Length body are
/// complete. Returns false when the peer closes before a full request arrives.
bool readRequest( int aFd, std::string& aBody )
{
    std::string buffer;
    char        chunk[4096];
    std::size_t headerEnd = std::string::npos;

    while( headerEnd == std::string::npos )
    {
        ssize_t got = ::recv( aFd, chunk, sizeof( chunk ), 0 );

        if( got <= 0 )
            return false;

        buffer.append( chunk, static_cast<std::size_t>( got ) );
        headerEnd = buffer.find( "\r\n\r\n" );
    }

    std::size_t contentLength = 0;
    std::string headers = buffer.substr( 0, headerEnd );
    std::string lowered = headers;
    std::transform( lowered.begin(), lowered.end(), lowered.begin(),
                    []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );

    std::size_t pos = lowered.find( "content-length:" );

    if( pos != std::string::npos )
        contentLength = static_cast<std::size_t>(
                std::strtoul( lowered.c_str() + pos + std::strlen( "content-length:" ), nullptr,
                              10 ) );

    std::string body = buffer.substr( headerEnd + 4 );

    while( body.size() < contentLength )
    {
        ssize_t got = ::recv( aFd, chunk, sizeof( chunk ), 0 );

        if( got <= 0 )
            return false;

        body.append( chunk, static_cast<std::size_t>( got ) );
    }

    aBody = body;
    return true;
}


void writeAll( int aFd, const std::string& aData )
{
    std::size_t sent = 0;

    while( sent < aData.size() )
    {
        ssize_t wrote = ::send( aFd, aData.data() + sent, aData.size() - sent, 0 );

        if( wrote <= 0 )
            return;

        sent += static_cast<std::size_t>( wrote );
    }
}

} // namespace


MOCK_ODOO_SERVER::MOCK_ODOO_SERVER()
{
}


MOCK_ODOO_SERVER::~MOCK_ODOO_SERVER()
{
    Stop();
}


bool MOCK_ODOO_SERVER::Start()
{
    m_listenFd = ::socket( AF_INET, SOCK_STREAM, 0 );

    if( m_listenFd < 0 )
        return false;

    int reuse = 1;
    ::setsockopt( m_listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );

    sockaddr_in addr;
    std::memset( &addr, 0, sizeof( addr ) );
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
    addr.sin_port = htons( 0 );

    if( ::bind( m_listenFd, reinterpret_cast<sockaddr*>( &addr ), sizeof( addr ) ) < 0 )
    {
        ::close( m_listenFd );
        m_listenFd = -1;
        return false;
    }

    if( ::listen( m_listenFd, 8 ) < 0 )
    {
        ::close( m_listenFd );
        m_listenFd = -1;
        return false;
    }

    sockaddr_in bound;
    socklen_t   len = sizeof( bound );

    if( ::getsockname( m_listenFd, reinterpret_cast<sockaddr*>( &bound ), &len ) < 0 )
    {
        ::close( m_listenFd );
        m_listenFd = -1;
        return false;
    }

    m_port = ntohs( bound.sin_port );
    m_running.store( true );
    m_thread = std::thread( &MOCK_ODOO_SERVER::run, this );
    return true;
}


void MOCK_ODOO_SERVER::Stop()
{
    if( !m_running.exchange( false ) )
        return;

    if( m_listenFd >= 0 )
    {
        ::shutdown( m_listenFd, SHUT_RDWR );
        ::close( m_listenFd );
        m_listenFd = -1;
    }

    if( m_thread.joinable() )
        m_thread.join();
}


void MOCK_ODOO_SERVER::run()
{
    while( m_running.load() )
    {
        int client = ::accept( m_listenFd, nullptr, nullptr );

        if( client < 0 )
            break;

        std::string body;

        // A malformed request must never throw out of the accept thread and take
        // the whole test process down with it.
        try
        {
            if( readRequest( client, body ) )
            {
                nlohmann::json request = nlohmann::json::parse( body, nullptr, false );

                nlohmann::json response;
                response["jsonrpc"] = "2.0";
                response["id"] = request.is_discarded() ? 0 : request.value( "id", 0 );

                if( request.is_discarded() )
                    response["error"] = { { "code", 400 }, { "message", "parse error" } };
                else
                    response["result"] = dispatch( request );

                std::string payload = response.dump();
                std::string http = "HTTP/1.1 200 OK\r\n";
                http += "Content-Type: application/json\r\n";
                http += "Content-Length: " + std::to_string( payload.size() ) + "\r\n";
                http += "Connection: close\r\n\r\n";
                http += payload;

                writeAll( client, http );
            }
        }
        catch( ... )
        {
            std::string http = "HTTP/1.1 500 Internal Server Error\r\n";
            http += "Content-Length: 0\r\n";
            http += "Connection: close\r\n\r\n";
            writeAll( client, http );
        }

        ::close( client );
    }
}
