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

#include <api/mdw/backend_base.pb.h>

#include <string>


namespace
{
constexpr unsigned PROTOCOL_MAJOR = 1;
constexpr unsigned PROTOCOL_MINOR = 0;


wxString fromUtf8( const std::string& aValue )
{
    return wxString::FromUTF8( aValue );
}


MDW_VERSION toStruct( const kiapi::workspace::Version& aProto )
{
    MDW_VERSION version;
    version.major = aProto.major();
    version.minor = aProto.minor();
    version.patch = aProto.patch();
    version.full = fromUtf8( aProto.full() );
    return version;
}


MDW_BACKEND_INFO toStruct( const kiapi::workspace::BackendInfo& aProto )
{
    MDW_BACKEND_INFO info;
    info.backend_id = fromUtf8( aProto.backend_id() );
    info.display_name = fromUtf8( aProto.display_name() );
    info.description = fromUtf8( aProto.description() );
    info.vendor = fromUtf8( aProto.vendor() );
    info.vendor_url = fromUtf8( aProto.vendor_url() );
    info.support_url = fromUtf8( aProto.support_url() );
    info.icon_name = fromUtf8( aProto.icon_name() );
    info.version = toStruct( aProto.version() );
    return info;
}


MDW_CAPABILITIES toStruct( const kiapi::workspace::Capabilities& aProto )
{
    MDW_CAPABILITIES caps;
    caps.supports_offline = aProto.supports_offline();

    for( int group : aProto.groups() )
        caps.groups.insert( static_cast<MDW_CAPABILITY_GROUP>( group ) );

    for( const kiapi::workspace::CustomState& state : aProto.custom_states() )
    {
        MDW_CUSTOM_STATE custom;
        custom.state_id = fromUtf8( state.state_id() );
        custom.display_name = fromUtf8( state.display_name() );
        custom.description = fromUtf8( state.description() );
        custom.category = static_cast<MDW_LIFECYCLE_CATEGORY>( state.category() );
        custom.icon_name = fromUtf8( state.icon_name() );
        custom.sort_order = state.sort_order();
        caps.custom_states.push_back( custom );
    }

    return caps;
}


MDW_SCHEMA toStruct( const kiapi::workspace::Schema& aProto )
{
    MDW_SCHEMA schema;
    schema.schema_version = fromUtf8( aProto.schema_version() );

    for( const kiapi::workspace::AttributeSchema& attr : aProto.attributes() )
    {
        MDW_ATTRIBUTE_SCHEMA out;
        out.attr_id = fromUtf8( attr.attr_id() );
        out.display_name = fromUtf8( attr.display_name() );
        out.description = fromUtf8( attr.description() );
        out.type = static_cast<MDW_ATTR_TYPE>( attr.type() );
        out.required = attr.required();
        out.editable_in_kicad = attr.editable_in_kicad();
        out.default_value = fromUtf8( attr.default_value() );
        out.display_order = attr.display_order();

        for( const std::string& value : attr.enum_values() )
            out.enum_values.push_back( fromUtf8( value ) );

        schema.attributes.push_back( out );
    }

    return schema;
}
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
    envelope.set_channel_token( std::string( m_channelToken.utf8_string() ) );
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
    hello.set_kicad_version( std::string( aKicadVersion.utf8_string() ) );
    hello.set_channel_token( std::string( m_channelToken.utf8_string() ) );
    hello.set_events_url( std::string( aEventsUrl.utf8_string() ) );

    kiapi::mdw::HelloReply reply;

    if( CallSync<kiapi::mdw::Hello, kiapi::mdw::HelloReply>( hello, reply, aTimeoutMs )
        != MDW_ERROR_CODE::SUCCESS )
    {
        return false;
    }

    m_info = toStruct( reply.backend_info() );
    m_capabilities = toStruct( reply.capabilities() );

    kiapi::mdw::GetSchemaRequest schemaRequest;
    kiapi::mdw::GetSchemaReply   schemaReply;

    if( CallSync<kiapi::mdw::GetSchemaRequest, kiapi::mdw::GetSchemaReply>( schemaRequest, schemaReply,
                                                                           aTimeoutMs )
        == MDW_ERROR_CODE::SUCCESS )
    {
        m_schema = toStruct( schemaReply.schema() );
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
