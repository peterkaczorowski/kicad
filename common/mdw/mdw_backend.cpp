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

#include <mdw/mdw_backend.h>
#include <mdw/mdw_proto_convert.h>

#include <api/mdw/backend_base.pb.h>

#include <string>


namespace
{
constexpr unsigned PROTOCOL_MAJOR = 1;
constexpr unsigned PROTOCOL_MINOR = 0;
}


MDW_BACKEND::MDW_BACKEND( std::unique_ptr<MDW_TRANSPORT> aTransport, const wxString& aChannelToken ) :
        m_transport( std::move( aTransport ) ),
        m_channelToken( aChannelToken )
{
}


MDW_BACKEND::~MDW_BACKEND()
{
    Stop();
}


bool MDW_BACKEND::Start()
{
    return m_transport->Start();
}


void MDW_BACKEND::Stop()
{
    m_transport->Stop();
}


kiapi::mdw::MdwRequest MDW_BACKEND::makeEnvelope( const google::protobuf::Message& aPayload )
{
    kiapi::mdw::MdwRequest envelope;
    envelope.mutable_protocol_version()->set_major( PROTOCOL_MAJOR );
    envelope.mutable_protocol_version()->set_minor( PROTOCOL_MINOR );
    envelope.set_channel_token( MdwToUtf8( m_channelToken ) );
    envelope.set_request_id( std::to_string( ++m_requestSeq ) );
    envelope.mutable_payload()->PackFrom( aPayload );

    return envelope;
}


bool MDW_BACKEND::Handshake( const wxString& aKicadVersion, const wxString& aEventsUrl,
                             int aTimeoutMs )
{
    kiapi::mdw::Hello hello;
    hello.mutable_protocol_version()->set_major( PROTOCOL_MAJOR );
    hello.mutable_protocol_version()->set_minor( PROTOCOL_MINOR );
    hello.set_kicad_version( MdwToUtf8( aKicadVersion ) );
    hello.set_channel_token( MdwToUtf8( m_channelToken ) );
    hello.set_events_url( MdwToUtf8( aEventsUrl ) );

    kiapi::mdw::HelloReply reply;

    if( CallSync<kiapi::mdw::Hello, kiapi::mdw::HelloReply>( hello, reply, aTimeoutMs )
        != MDW_ERROR_CODE::SUCCESS )
    {
        return false;
    }

    m_info = MdwToStruct( reply.backend_info() );
    m_capabilities = MdwToStruct( reply.capabilities() );

    kiapi::mdw::GetSchemaRequest schemaRequest;
    kiapi::mdw::GetSchemaReply   schemaReply;

    if( CallSync<kiapi::mdw::GetSchemaRequest, kiapi::mdw::GetSchemaReply>( schemaRequest, schemaReply,
                                                                           aTimeoutMs )
        == MDW_ERROR_CODE::SUCCESS )
    {
        m_schema = MdwToStruct( schemaReply.schema() );
    }

    m_handshaked = true;

    return true;
}


bool MDW_BACKEND::Ping( int aTimeoutMs )
{
    kiapi::mdw::PingRequest request;
    kiapi::mdw::PingReply   reply;

    return CallSync<kiapi::mdw::PingRequest, kiapi::mdw::PingReply>( request, reply, aTimeoutMs )
           == MDW_ERROR_CODE::SUCCESS;
}


void MDW_BACKEND::Bye( int aTimeoutMs )
{
    kiapi::mdw::ByeRequest request;
    kiapi::mdw::ByeReply   reply;

    CallSync<kiapi::mdw::ByeRequest, kiapi::mdw::ByeReply>( request, reply, aTimeoutMs );
}
