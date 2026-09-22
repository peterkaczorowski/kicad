/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <mdw/mdw_nng_transport.h>

#include <kinng.h>


static MDW_TRANSPORT_RESULT mapResult( const KINNG_REQUEST_CLIENT::RESULT& aRaw )
{
    MDW_TRANSPORT_RESULT result;

    if( aRaw.received )
    {
        // An unparseable reply is indistinguishable from a dead backend as far as
        // the caller is concerned, so it maps the same way.
        result.status = result.response.ParseFromString( aRaw.reply )
                                ? MDW_TRANSPORT_STATUS::OK
                                : MDW_TRANSPORT_STATUS::BACKEND_LOST;
    }
    else if( aRaw.timed_out )
    {
        result.status = MDW_TRANSPORT_STATUS::TIMED_OUT;
    }
    else if( aRaw.cancelled )
    {
        result.status = MDW_TRANSPORT_STATUS::CANCELLED;
    }

    return result;
}


MDW_NNG_TRANSPORT::MDW_NNG_TRANSPORT( const std::string& aControlUrl ) :
        m_client( std::make_unique<KINNG_REQUEST_CLIENT>( aControlUrl ) )
{
}


MDW_NNG_TRANSPORT::~MDW_NNG_TRANSPORT()
{
    Stop();
}


bool MDW_NNG_TRANSPORT::Start()
{
    return m_client->Start();
}


void MDW_NNG_TRANSPORT::Stop()
{
    m_client->Stop();
}


std::uint64_t MDW_NNG_TRANSPORT::SendAsync( const kiapi::mdw::MdwRequest& aRequest, int aTimeoutMs,
                                            CALLBACK aOnComplete )
{
    std::string wire;
    aRequest.SerializeToString( &wire );

    return m_client->Request( wire, aTimeoutMs,
                              [aOnComplete]( const KINNG_REQUEST_CLIENT::RESULT& aRaw )
                              { aOnComplete( mapResult( aRaw ) ); } );
}


MDW_TRANSPORT_RESULT MDW_NNG_TRANSPORT::SendSync( const kiapi::mdw::MdwRequest& aRequest,
                                                 int aTimeoutMs )
{
    std::string wire;
    aRequest.SerializeToString( &wire );

    return mapResult( m_client->RequestSync( wire, aTimeoutMs ) );
}


void MDW_NNG_TRANSPORT::Cancel( std::uint64_t aHandle )
{
    m_client->Cancel( aHandle );
}
