/* Copyright The KiCad Developers, see AUTHORS.txt. SPDX-License-Identifier: GPL-3.0-or-later */
#include <api/api_pcb_library_utils.h>
#include <api/api_pcb_utils.h>
#include <api/api_enums.h>
#include <api/api_utils.h>
#include <footprint.h>
#include <board.h>
#include <pad.h>
#include <pcb_field.h>
#include <pcb_shape.h>
#include <template_fieldnames.h>
#include <google/protobuf/dynamic_message.h>
#include <climits>
#include <cmath>
#include <set>

namespace kiapi::board
{
namespace
{
bool physicalLayer( types::BoardLayer layer );
bool bounded( const google::protobuf::Message& message, std::string& error )
{
    const auto* reflection = message.GetReflection();
    if( reflection->GetUnknownFields( message ).field_count() )
    { error = "Unknown footprint protocol fields cannot be saved losslessly"; return false; }
    if( message.GetDescriptor()->full_name() == "kiapi.board.types.Net" )
    {
        types::Net net;
        net.CopyFrom( message );
        if( !net.name().empty() || net.code().value() )
        { error = "Library definitions cannot contain board nets"; return false; }
    }
    if( message.GetDescriptor()->full_name() == "kiapi.common.types.KIID" )
    {
        const auto* value = message.GetDescriptor()->FindFieldByName( "value" );
        const std::string id = reflection->GetString( message, value );
        if( !id.empty() )
        {
            bool valid = id.size() == 36;
            for( size_t i = 0; valid && i < id.size(); ++i )
                valid = ( i == 8 || i == 13 || i == 18 || i == 23 ) ? id[i] == '-'
                        : ( id[i] >= '0' && id[i] <= '9' ) || ( id[i] >= 'a' && id[i] <= 'f' )
                          || ( id[i] >= 'A' && id[i] <= 'F' );
            if( !valid ) { error = "Invalid footprint item UUID"; return false; }
        }
    }
    std::vector<const google::protobuf::FieldDescriptor*> fields;
    reflection->ListFields( message, &fields );
    for( const auto* field : fields )
    {
        const int count = field->is_repeated() ? reflection->FieldSize( message, field ) : 1;
        for( int i = 0; i < count; ++i )
        {
            using FD = google::protobuf::FieldDescriptor;
            if( field->cpp_type() == FD::CPPTYPE_MESSAGE )
            {
                const auto& child = field->is_repeated() ? reflection->GetRepeatedMessage( message, field, i )
                                                       : reflection->GetMessage( message, field );
                if( !bounded( child, error ) ) return false;
            }
            else if( field->cpp_type() == FD::CPPTYPE_ENUM )
            {
                const int value = field->is_repeated() ? reflection->GetRepeatedEnumValue( message, field, i )
                                                      : reflection->GetEnumValue( message, field );
                if( !field->enum_type()->FindValueByNumber( value ) )
                { error = "Unknown footprint enum value"; return false; }
                if( field->enum_type()->full_name() == "kiapi.board.types.BoardLayer"
                    && !physicalLayer( static_cast<types::BoardLayer>( value ) ) )
                { error = "Invalid footprint layer"; return false; }
            }
            else if( field->cpp_type() == FD::CPPTYPE_FLOAT )
            {
                const float value = field->is_repeated() ? reflection->GetRepeatedFloat( message, field, i )
                                                        : reflection->GetFloat( message, field );
                if( !std::isfinite( value ) ) { error = "Non-finite footprint value"; return false; }
            }
            else if( field->cpp_type() == FD::CPPTYPE_DOUBLE )
            {
                const double value = field->is_repeated() ? reflection->GetRepeatedDouble( message, field, i )
                                                         : reflection->GetDouble( message, field );
                if( !std::isfinite( value ) ) { error = "Non-finite footprint value"; return false; }
            }
            else if( field->cpp_type() == FD::CPPTYPE_INT64 && field->name().find( "nm" ) != std::string::npos )
            {
                const auto value = field->is_repeated() ? reflection->GetRepeatedInt64( message, field, i )
                                                       : reflection->GetInt64( message, field );
                if( value < -INT_MAX / 4 || value > INT_MAX / 4 )
                { error = "Footprint coordinate/dimension outside native range"; return false; }
            }
        }
    }
    return true;
}

bool physicalLayer( types::BoardLayer layer )
{
    auto native = FromProtoEnum<PCB_LAYER_ID, types::BoardLayer>( layer );
    return native >= 0 && native < PCB_LAYER_ID_COUNT;
}
}

types::FootprintInstance PackFootprintDefinition( const FOOTPRINT& source )
{
    // Native FOOTPRINT::Serialize currently omits group membership entirely.
    // Refuse that source instead of presenting a silently incomplete library copy.
    if( !source.Groups().empty() )
        throw std::runtime_error( "Footprint group serialization is unsupported; ungroup before exporting" );
    // PADSTACK and text Flip require a board's opposite-layer mapping, even for
    // an otherwise detached library model. Give the clone an isolated holder,
    // including custom front/back user-layer pairs, never the live source board.
    BOARD holder;
    holder.SetBoardUse( BOARD_USE::FPHOLDER );
    if( const BOARD* board = source.GetBoard() )
    {
        holder.SetCopperLayerCount( board->GetCopperLayerCount() );
        for( int i = 0; i < PCB_LAYER_ID_COUNT; ++i )
        {
            auto id = static_cast<PCB_LAYER_ID>( i );
            LAYER layer;
            layer.m_number = i;
            layer.m_name = BOARD::GetStandardLayerName( id );
            layer.m_userName = board->GetLayerName( id );
            layer.m_type = board->GetLayerType( id );
            holder.SetLayerDescr( id, layer );
        }
    }
    auto copy = std::unique_ptr<FOOTPRINT>( static_cast<FOOTPRINT*>( source.Clone() ) );
    copy->SetParent( &holder );
    copy->SetParentGroup( nullptr );
    copy->ClearFlags();
    copy->SetLayerAndFlip( F_Cu );
    copy->SetOrientation( ANGLE_0 );
    copy->SetPosition( VECTOR2I( 0, 0 ) );
    copy->SetLocked( false );
    copy->SetReference( "REF**" );
    if( !copy->GetFPID().GetLibItemName().empty() )
        copy->SetValue( copy->GetFPID().GetLibItemName().wx_str() );
    copy->SetPath( KIID_PATH() );
    copy->SetLink( niluuid );
    copy->SetSheetname( wxEmptyString );
    copy->SetSheetfile( wxEmptyString );
    copy->SetFilters( wxEmptyString );
    for( PAD* pad : copy->Pads() )
    {
        pad->SetNetCode( 0 );
        pad->SetPinFunction( wxEmptyString );
        pad->SetPinType( wxEmptyString );
    }
    for( BOARD_ITEM* item : copy->GraphicalItems() )
        if( auto* shape = dynamic_cast<PCB_SHAPE*>( item ) ) shape->SetNetCode( 0 );
    google::protobuf::Any packed;
    copy->Serialize( packed );
    types::FootprintInstance result;
    if( !packed.UnpackTo( &result ) ) throw std::runtime_error( "Native footprint serialization failed" );
    result.clear_parent();
    result.clear_symbol_units();
    result.clear_variants();
    return result;
}

std::unique_ptr<FOOTPRINT> UnpackFootprintDefinition( const types::FootprintInstance& input,
                                                    std::string& error )
{
    error.clear();
    if( input.position().x_nm() || input.position().y_nm() || input.orientation().value_degrees()
        || input.layer() != types::BL_F_Cu || input.symbol_path().path_size()
        || input.symbol_units_size() || input.variants_size() || !input.symbol_sheet_name().empty()
        || !input.symbol_sheet_filename().empty() || !input.symbol_footprint_filters().empty() )
    { error = "Use a local F.Cu library definition, not a placed board instance"; return nullptr; }
    const auto& definition = input.definition();
    if( definition.anchor().x_nm() || definition.anchor().y_nm() || definition.has_overrides()
        || definition.has_reference_field() || definition.has_value_field()
        || definition.has_datasheet_field() || definition.has_description_field() )
    { error = "Use zero anchor and the instance header for mandatory fields and rule overrides"; return nullptr; }
    if( !bounded( input, error ) ) return nullptr;
    const auto id = common::LibIdFromProto( input.definition().id() );
    const wxString name = id.GetLibItemName().wx_str();
    if( !id.IsValid() || LIB_ID::FindIllegalLibraryNameChar( id.GetLibNickname() )
        || name.empty() || name == "." || name == ".."
        || name.find_first_of( "/\\:" ) != wxString::npos )
    { error = "A safe complete library:footprint identifier is required"; return nullptr; }

    types::FootprintInstance normalized = input;
    normalized.mutable_id()->set_value( KIID().AsStdString() );
    normalized.clear_parent();
    std::unique_ptr<FOOTPRINT> footprint( new FOOTPRINT( nullptr ) );
    std::set<std::string> ids;
    std::set<std::string> fieldNames;
    for( const auto* field : { &input.reference_field(), &input.value_field(),
                              &input.datasheet_field(), &input.description_field() } )
        if( !field->text().id().value().empty() && !ids.insert( field->text().id().value() ).second )
        { error = "Duplicate footprint item UUID"; return nullptr; }
    // Probe every child independently: native FOOTPRINT::Deserialize skips unknown
    // or malformed children and still returns true, which is unsuitable for saving.
    for( const auto& any : input.definition().items() )
    {
        std::string typeName = any.type_url().substr( any.type_url().find_last_of( '/' ) + 1 );
        const auto* descriptor = google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName( typeName );
        const auto* prototype = descriptor ? google::protobuf::MessageFactory::generated_factory()->GetPrototype( descriptor ) : nullptr;
        if( !prototype ) { error = "Unknown footprint child type: " + typeName; return nullptr; }
        std::unique_ptr<google::protobuf::Message> child( prototype->New() );
        if( !any.UnpackTo( child.get() ) || !bounded( *child, error ) )
        { if( error.empty() ) error = "Malformed footprint child"; return nullptr; }
        if( any.Is<types::Footprint3DModel>() )
        {
            const auto& model = static_cast<const types::Footprint3DModel&>( *child );
            if( model.filename().empty() || model.scale().x_nm() <= 0 || model.scale().y_nm() <= 0
                || model.scale().z_nm() <= 0 || model.opacity() < 0 || model.opacity() > 1 )
            { error = "Invalid 3D model filename, scale or opacity"; return nullptr; }
            continue;
        }
        auto type = common::TypeNameFromAny( any );
        if( !type || ( *type != PCB_PAD_T && *type != PCB_SHAPE_T && *type != PCB_TEXT_T
                      && *type != PCB_TEXTBOX_T && *type != PCB_FIELD_T && *type != PCB_POINT_T ) )
        { error = "Unsupported footprint child type: " + typeName; return nullptr; }
        if( any.Is<types::Pad>() )
        {
            const auto& pad = static_cast<const types::Pad&>( *child );
            if( !pad.net().name().empty() || pad.type() < types::PT_PTH || pad.type() > types::PT_NPTH
                || !pad.pad_stack().layers_size() || !pad.pad_stack().copper_layers_size()
                || pad.pad_stack().type() < types::PST_NORMAL || pad.pad_stack().type() > types::PST_CUSTOM )
            { error = "Library pads require a valid padstack and no board nets"; return nullptr; }
            for( int layer : pad.pad_stack().layers() )
                if( !physicalLayer( static_cast<types::BoardLayer>( layer ) ) )
                { error = "Invalid pad layer"; return nullptr; }
            for( const auto& layer : pad.pad_stack().copper_layers() )
                if( !physicalLayer( layer.layer() ) || layer.shape() < types::PSS_CIRCLE || layer.shape() > types::PSS_CUSTOM
                    || layer.size().x_nm() <= 0 || layer.size().y_nm() <= 0 )
                { error = "Invalid pad shape or size"; return nullptr; }
        }
        if( any.Is<types::Field>() )
        {
            const auto& field = static_cast<const types::Field&>( *child );
            if( field.id().id() != static_cast<int>( FIELD_T::USER ) || field.name().empty()
                || !fieldNames.insert( field.name() ).second )
            { error = "Mandatory footprint fields belong in the definition header"; return nullptr; }
        }
        if( any.Is<types::BoardGraphicShape>() )
        {
            const auto& shape = static_cast<const types::BoardGraphicShape&>( *child );
            if( shape.shape().geometry_case() == common::types::GraphicShape::GEOMETRY_NOT_SET )
            { error = "Footprint graphic shape has no geometry"; return nullptr; }
        }
        auto item = CreateItemForType( *type, footprint.get() );
        if( !item || !item->Deserialize( any )
            || ( item->Type() != PCB_PAD_T && !physicalLayer( ToProtoEnum<PCB_LAYER_ID, types::BoardLayer>( item->GetLayer() ) ) ) )
        { error = "Invalid native footprint child"; return nullptr; }
        if( !ids.insert( item->m_Uuid.AsStdString() ).second )
        { error = "Duplicate footprint item UUID"; return nullptr; }
    }
    google::protobuf::Any packed;
    if( !packed.PackFrom( normalized ) )
    { error = "Footprint encoding failed"; return nullptr; }
    if( !footprint->Deserialize( packed ) )
    { error = "Native footprint decoding failed"; return nullptr; }
    footprint->SetFPID( id );
    footprint->SetReference( "REF**" );
    footprint->SetValue( name );
    footprint->SetLocked( false );
    return footprint;
}
}
