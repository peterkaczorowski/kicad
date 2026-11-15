/* Copyright The KiCad Developers, see AUTHORS.txt.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "api_sch_utils.h"
#include <sch_line.h>
#include <sch_label.h>
#include <sch_field.h>
#include <eda_text.h>
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

// Reuse the core shape/text codecs, converting only dimensional protobuf types.
// Angles, colors and dimensionless attributes remain unchanged.
bool ConvertSchematicUnits( google::protobuf::Message& message, bool toApi, std::string& error )
{
    using FD = google::protobuf::FieldDescriptor;
    const auto* descriptor = message.GetDescriptor();
    const auto* reflection = message.GetReflection();
    const bool dimensional = descriptor->full_name() == "kiapi.common.types.Vector2"
                             || descriptor->full_name() == "kiapi.common.types.Distance";
    for( int n = 0; n < descriptor->field_count(); ++n )
    {
        const auto* field = descriptor->field( n );
        if( dimensional && field->cpp_type() == FD::CPPTYPE_INT64 )
        {
            int64_t value = reflection->GetInt64( message, field );
            constexpr int64_t bound = int64_t( std::numeric_limits<int>::max() ) / 4;
            if( value < -bound * ( toApi ? 1 : 100 ) || value > bound * ( toApi ? 1 : 100 ) )
            { error = "Schematic dimension exceeds the supported model range"; return false; }
            reflection->SetInt64( &message, field, toApi ? value * 100 : std::llround( value / 100.0 ) );
        }
        else if( field->cpp_type() == FD::CPPTYPE_MESSAGE )
        {
            if( field->is_repeated() )
            {
                for( int i = 0; i < reflection->FieldSize( message, field ); ++i )
                    if( !ConvertSchematicUnits( *reflection->MutableRepeatedMessage( &message, field, i ), toApi, error ) )
                        return false;
            }
            else if( reflection->HasField( message, field )
                     && !ConvertSchematicUnits( *reflection->MutableMessage( &message, field ), toApi, error ) )
                return false;
        }
        else if( field->cpp_type() == FD::CPPTYPE_DOUBLE
                 && !std::isfinite( reflection->GetDouble( message, field ) ) )
        { error = "Non-finite schematic attribute"; return false; }
    }
    return true;
}

void PackSchematicText( types::Text& output, const EDA_TEXT& text )
{
    text.EDA_TEXT::Serialize( output );
    std::string error;
    ConvertSchematicUnits( output, true, error );
}

bool UnpackSchematicText( const types::Text& input, EDA_TEXT& text, std::string& error )
{
    types::Text native = input;
    types::Text defaults;
    PackSchematicText( defaults, text );
    if( !native.has_attributes() ) *native.mutable_attributes() = defaults.attributes();
    auto* attrs = native.mutable_attributes();
    if( !attrs->horizontal_alignment() ) attrs->set_horizontal_alignment( defaults.attributes().horizontal_alignment() );
    if( !attrs->vertical_alignment() ) attrs->set_vertical_alignment( defaults.attributes().vertical_alignment() );
    if( !attrs->has_size() ) *attrs->mutable_size() = defaults.attributes().size();
    if( attrs->size().x_nm() <= 0 || attrs->size().y_nm() <= 0 )
    { error = "Text size must be positive"; return false; }
    if( !ConvertSchematicUnits( native, false, error ) ) return false;
    return text.EDA_TEXT::Deserialize( native );
}

namespace
{
bool applyId( const types::KIID& id, SCH_ITEM& item, std::string& error )
{
    if( id.value().empty() ) return true;
    if( !KIID::SniffTest( wxString::FromUTF8( id.value() ) ) || KIID( id.value() ) == niluuid )
    { error = "Invalid schematic item UUID"; return false; }
    const_cast<KIID&>( item.m_Uuid ) = KIID( id.value() );
    return true;
}

template<class Proto>
google::protobuf::Any packLabel( const SCH_LABEL_BASE& label, bool directive = false )
{
    Proto out;
    out.mutable_id()->set_value( label.m_Uuid.AsStdString() );
    PackSchematicVector( *out.mutable_position(), label.GetPosition() );
    PackSchematicText( *out.mutable_text()->mutable_text(), label );
    out.set_spin( int( label.GetSpinStyle() ) );
    out.set_shape( int( label.GetLabelShape() ) - ( directive ? int( F_FIRST ) : 0 ) );
    for( const auto& field : label.GetFields() )
        ( *out.mutable_properties() )[field.GetCanonicalName().ToStdString()] = field.GetText().ToStdString();
    google::protobuf::Any result; result.PackFrom( out ); return result;
}

template<class Proto, class Label>
std::unique_ptr<SCH_ITEM> unpackLabel( const google::protobuf::Any& data, const SCH_ITEM* existing,
                                      KICAD_T kind, bool directive, std::string& error )
{
    Proto input;
    if( !data.UnpackTo( &input ) ) return nullptr;
    VECTOR2I position;
    if( ( existing && existing->Type() != kind ) || input.spin() > 3
        || input.shape() > ( directive ? 3u : 4u )
        || !UnpackSchematicVector( input.position(), position )
        || ( !directive && input.text().text().text().empty() ) )
    { error = "Invalid label type, text, shape, spin or position"; return nullptr; }
    std::unique_ptr<SCH_ITEM> item( existing ? static_cast<SCH_ITEM*>( existing->Clone() ) : new Label() );
    auto* label = static_cast<Label*>( item.get() );
    if( !applyId( input.id(), *item, error )
        || !UnpackSchematicText( input.text().text(), *label, error ) ) return nullptr;
    label->SetPosition( position );
    label->SetSpinStyle( SPIN_STYLE( static_cast<SPIN_STYLE::SPIN>( input.spin() ) ) );
    if constexpr( std::is_same_v<Label, SCH_DIRECTIVE_LABEL> )
        label->SetFlagShape( static_cast<FLAG_SHAPE>( input.shape() + F_FIRST ) );
    else
        label->SetLabelShape( static_cast<LABEL_SHAPE>( input.shape() ) );
    auto& fields = label->GetFields();
    fields.erase( std::remove_if( fields.begin(), fields.end(), [&]( const SCH_FIELD& field )
                  { return field.GetId() == FIELD_T::USER
                           && input.properties().find( field.GetCanonicalName().ToStdString() ) == input.properties().end(); } ), fields.end() );
    for( const auto& [name, value] : input.properties() )
    {
        SCH_FIELD* found = nullptr;
        for( auto& field : fields ) if( field.GetCanonicalName() == wxString::FromUTF8( name ) ) found = &field;
        if( found ) found->SetText( wxString::FromUTF8( value ) );
        else
        {
            SCH_FIELD field( label, FIELD_T::USER, wxString::FromUTF8( name ) );
            field.SetText( wxString::FromUTF8( value ) );
            fields.push_back( field );
        }
    }
    label->AutoplaceFields( nullptr, AUTOPLACE_AUTO );
    return item;
}
}

std::set<int> SupportedSchematicApiTypes()
{
    return { types::KOT_SCH_LINE, types::KOT_SCH_SHEET, types::KOT_SCH_SHEET_PIN,
             types::KOT_SCH_LABEL, types::KOT_SCH_GLOBAL_LABEL, types::KOT_SCH_HIER_LABEL,
             types::KOT_SCH_DIRECTIVE_LABEL, types::KOT_SCH_TEXT };
}

std::optional<google::protobuf::Any> PackSchematicItem( const SCH_ITEM& item,
                                                       const SCH_SHEET_PATH* path )
{
    google::protobuf::Any out;
    switch( item.Type() )
    {
    case SCH_LINE_T: item.Serialize( out ); break;
    case SCH_LABEL_T: return packLabel<LocalLabel>( static_cast<const SCH_LABEL_BASE&>( item ) );
    case SCH_GLOBAL_LABEL_T: return packLabel<GlobalLabel>( static_cast<const SCH_LABEL_BASE&>( item ) );
    case SCH_HIER_LABEL_T: return packLabel<HierarchicalLabel>( static_cast<const SCH_LABEL_BASE&>( item ) );
    case SCH_DIRECTIVE_LABEL_T: return packLabel<DirectiveLabel>( static_cast<const SCH_LABEL_BASE&>( item ), true );
    case SCH_TEXT_T:
    {
        kiapi::schematic::types::Text text;
        text.mutable_id()->set_value( item.m_Uuid.AsStdString() );
        PackSchematicText( *text.mutable_text(), static_cast<const SCH_TEXT&>( item ) );
        out.PackFrom( text ); break;
    }
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
    if( data.Is<LocalLabel>() ) return unpackLabel<LocalLabel, SCH_LABEL>( data, existing, SCH_LABEL_T, false, error );
    if( data.Is<GlobalLabel>() ) return unpackLabel<GlobalLabel, SCH_GLOBALLABEL>( data, existing, SCH_GLOBAL_LABEL_T, false, error );
    if( data.Is<HierarchicalLabel>() ) return unpackLabel<HierarchicalLabel, SCH_HIERLABEL>( data, existing, SCH_HIER_LABEL_T, false, error );
    if( data.Is<DirectiveLabel>() ) return unpackLabel<DirectiveLabel, SCH_DIRECTIVE_LABEL>( data, existing, SCH_DIRECTIVE_LABEL_T, true, error );
    if( data.Is<kiapi::schematic::types::Text>() )
    {
        kiapi::schematic::types::Text input;
        data.UnpackTo( &input );
        if( existing && existing->Type() != SCH_TEXT_T ) { error = "Cannot change item type"; return nullptr; }
        item.reset( existing ? static_cast<SCH_ITEM*>( existing->Clone() ) : new SCH_TEXT() );
        if( applyId( input.id(), *item, error )
            && UnpackSchematicText( input.text(), *static_cast<SCH_TEXT*>( item.get() ), error ) ) return item;
        return nullptr;
    }
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
