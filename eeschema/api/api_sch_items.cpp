/* Copyright The KiCad Developers, see AUTHORS.txt.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "api_sch_utils.h"
#include <sch_line.h>
#include <sch_sheet.h>
#include <sch_sheet_pin.h>
#include <google/protobuf/util/field_mask_util.h>
#include <cmath>
#include <limits>

using namespace kiapi::schematic::types;
using namespace kiapi::common;

void PackSchematicVector( types::Vector2& out, const VECTOR2I& value )
{
    out.set_x_nm( int64_t( value.x ) * 100 );
    out.set_y_nm( int64_t( value.y ) * 100 );
}

bool UnpackSchematicVector( const types::Vector2& value, VECTOR2I& out )
{
    constexpr int64_t bound = int64_t( std::numeric_limits<int>::max() ) / 4 * 100;
    if( value.x_nm() < -bound || value.x_nm() > bound
        || value.y_nm() < -bound || value.y_nm() > bound )
        return false;
    out = VECTOR2I( std::llround( value.x_nm() / 100.0 ),
                    std::llround( value.y_nm() / 100.0 ) );
    return true;
}

std::set<int> SupportedSchematicApiTypes()
{
    return { types::KOT_SCH_LINE, types::KOT_SCH_SHEET, types::KOT_SCH_SHEET_PIN };
}

std::optional<google::protobuf::Any> PackSchematicItem( const SCH_ITEM& item,
                                                       const SCH_SHEET_PATH* path )
{
    google::protobuf::Any out;
    switch( item.Type() )
    {
    case SCH_LINE_T: item.Serialize( out ); break;
    case SCH_SHEET_T: out.PackFrom( PackSchematicSheet( static_cast<const SCH_SHEET&>( item ) ) ); break;
    case SCH_SHEET_PIN_T:
        out.PackFrom( PackSchematicSheetPin( static_cast<const SCH_SHEET_PIN&>( item ) ) ); break;
    default: return std::nullopt;
    }
    return out;
}

std::unique_ptr<SCH_ITEM> UnpackSchematicItem( const google::protobuf::Any& data,
        std::string& error, const SCH_ITEM* existing, const SCH_SHEET_PATH* path )
{
    std::unique_ptr<SCH_ITEM> item;
    if( data.Is<Line>() )
    {
        if( existing && existing->Type() != SCH_LINE_T )
        {
            error = "An update cannot change the item's type";
            return nullptr;
        }
        item.reset( existing ? static_cast<SCH_ITEM*>( existing->Clone() ) : new SCH_LINE() );
        if( item->Deserialize( data ) )
            return item;
        error = "Invalid wire/bus/graphic line: UUID, bounded coordinates, nonzero length and layer required";
    }
    else
        error = "Unsupported schematic item type";
    return nullptr;
}

namespace
{
std::unique_ptr<google::protobuf::Message> decode( const google::protobuf::Any& data )
{
    std::string name = data.type_url().substr( data.type_url().find_last_of( '/' ) + 1 );
    const auto* descriptor = google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName( name );
    if( !descriptor ) return nullptr;
    const auto* prototype = google::protobuf::MessageFactory::generated_factory()->GetPrototype( descriptor );
    if( !prototype ) return nullptr;
    std::unique_ptr<google::protobuf::Message> message( prototype->New() );
    if( !message->ParseFromString( data.value() ) ) return nullptr;
    return message;
}
}

std::string SchematicApiItemId( const google::protobuf::Any& data )
{
    auto message = decode( data );
    if( !message ) return {};
    const auto* field = message->GetDescriptor()->FindFieldByName( "id" );
    if( !field || field->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE ) return {};
    const auto& id = message->GetReflection()->GetMessage( *message, field );
    const auto* value = id.GetDescriptor()->FindFieldByName( "value" );
    return value && value->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_STRING
                   ? id.GetReflection()->GetString( id, value ) : std::string();
}

bool MergeSchematicApiItem( const google::protobuf::Any& patch,
        const google::protobuf::Any& original, const google::protobuf::FieldMask& mask,
        google::protobuf::Any& out, std::string& error )
{
    if( mask.paths_size() == 0 ) { out = patch; return true; }
    auto source = decode( patch );
    auto destination = decode( original );
    if( !source || !destination || source->GetDescriptor() != destination->GetDescriptor() )
    { error = "Field masks require matching supported item types"; return false; }
    for( const auto& path : mask.paths() )
    {
        if( path == "id" || path.rfind( "id.", 0 ) == 0
            || !google::protobuf::util::FieldMaskUtil::GetFieldDescriptors(
                        source->GetDescriptor(), path, nullptr ) )
        { error = "Invalid or immutable field-mask path: " + path; return false; }
    }
    google::protobuf::util::FieldMaskUtil::MergeOptions options;
    options.set_replace_repeated_fields( true );
    options.set_replace_message_fields( true );
    google::protobuf::util::FieldMaskUtil::MergeMessageTo( *source, mask, options, destination.get() );
    out.PackFrom( *destination );
    return true;
}
