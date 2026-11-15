/* Copyright The KiCad Developers, see AUTHORS.txt.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <boost/test/unit_test.hpp>
#include <google/protobuf/util/message_differencer.h>
#include <api/api_sch_utils.h>
#include <sch_line.h>
#include <sch_label.h>
#include <lib_symbol.h>
#include <sch_field.h>
#include <sch_shape.h>
#include <sch_textbox.h>
#include <sch_symbol.h>
#include <sch_sheet.h>
#include <sch_sheet_path.h>
#include <api/schematic/connection_items.pb.h>

namespace
{
bool sameItem( const google::protobuf::Any& first, const google::protobuf::Any& second )
{
    if( first.type_url() != second.type_url() ) return false;
    auto name = first.type_url().substr( first.type_url().find_last_of( '/' ) + 1 );
    const auto* descriptor = google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName( name );
    if( !descriptor ) return false;
    const auto* prototype = google::protobuf::MessageFactory::generated_factory()->GetPrototype( descriptor );
    if( !prototype ) return false;
    std::unique_ptr<google::protobuf::Message> a( prototype->New() ), b( prototype->New() );
    return first.UnpackTo( a.get() ) && second.UnpackTo( b.get() )
           && google::protobuf::util::MessageDifferencer::Equals( *a, *b );
}
}
BOOST_AUTO_TEST_SUITE( SchematicItemsApi )

BOOST_AUTO_TEST_CASE( WireCoordinatesLayersAndGeneratedIds )
{
    for( auto layer : { LAYER_WIRE, LAYER_BUS, LAYER_NOTES } )
    {
        SCH_LINE original( VECTOR2I( -12345, 24000 ), layer );
        original.SetEndPoint( VECTOR2I( 34567, 24000 ) );
        auto data = PackSchematicItem( original );
        BOOST_REQUIRE( data );
        kiapi::schematic::types::Line wire;
        BOOST_REQUIRE( data->UnpackTo( &wire ) );
        BOOST_CHECK_EQUAL( wire.start().x_nm(), -1234500 );
        BOOST_CHECK_EQUAL( wire.end().x_nm(), 3456700 );
        std::string error;
        auto decoded = UnpackSchematicItem( *data, error );
        BOOST_REQUIRE_MESSAGE( decoded, error );
        BOOST_CHECK_EQUAL( decoded->m_Uuid.AsStdString(), original.m_Uuid.AsStdString() );
        BOOST_CHECK_EQUAL( decoded->GetLayer(), layer );
        BOOST_CHECK_EQUAL( PackSchematicItem( *decoded )->SerializeAsString(), data->SerializeAsString() );
        wire.clear_id();
        wire.mutable_start()->set_x_nm( 157 );
        data->PackFrom( wire );
        decoded = UnpackSchematicItem( *data, error );
        BOOST_REQUIRE( decoded );
        BOOST_CHECK( decoded->m_Uuid != niluuid );
        BOOST_CHECK_EQUAL( static_cast<SCH_LINE*>( decoded.get() )->GetStartPoint().x, 2 );
    }
}

BOOST_AUTO_TEST_CASE( WireValidationAndSelectiveUpdates )
{
    SCH_LINE original( VECTOR2I( 0, 0 ), LAYER_WIRE );
    original.SetEndPoint( VECTOR2I( 10000, 0 ) );
    const auto before = PackSchematicItem( original );
    kiapi::schematic::types::Line patch;
    patch.mutable_id()->set_value( original.m_Uuid.AsStdString() );
    patch.mutable_end()->set_y_nm( 3000000 );
    google::protobuf::Any input, merged;
    input.PackFrom( patch );
    google::protobuf::FieldMask mask;
    mask.add_paths( "end.y_nm" );
    std::string error;
    BOOST_REQUIRE( MergeSchematicApiItem( input, *before, mask, merged, error ) );
    auto decoded = UnpackSchematicItem( merged, error, &original );
    BOOST_REQUIRE( decoded );
    auto* line = static_cast<SCH_LINE*>( decoded.get() );
    BOOST_CHECK_EQUAL( line->GetEndPoint().x, 10000 );
    BOOST_CHECK_EQUAL( line->GetEndPoint().y, 30000 );
    BOOST_CHECK_EQUAL( line->GetLayer(), LAYER_WIRE );
    BOOST_CHECK_EQUAL( SchematicApiItemId( merged ), original.m_Uuid.AsStdString() );
    mask.set_paths( 0, "end" );
    BOOST_REQUIRE( MergeSchematicApiItem( input, *before, mask, merged, error ) );
    decoded = UnpackSchematicItem( merged, error, &original );
    BOOST_REQUIRE( decoded );
    BOOST_CHECK_EQUAL( static_cast<SCH_LINE*>( decoded.get() )->GetEndPoint().x, 0 );
    BOOST_CHECK_EQUAL( static_cast<SCH_LINE*>( decoded.get() )->GetEndPoint().y, 30000 );
    for( auto path : { "id", "id.value", "end.no_such_field" } )
    {
        mask.set_paths( 0, path );
        BOOST_CHECK( !MergeSchematicApiItem( input, *before, mask, merged, error ) );
    }
    kiapi::schematic::types::Line wire;
    before->UnpackTo( &wire );
    wire.mutable_end()->CopyFrom( wire.start() );
    input.PackFrom( wire );
    BOOST_CHECK( !UnpackSchematicItem( input, error ) );
    before->UnpackTo( &wire );
    wire.mutable_start()->set_x_nm( INT64_MAX );
    input.PackFrom( wire );
    BOOST_CHECK( !UnpackSchematicItem( input, error ) );
    before->UnpackTo( &wire );
    wire.mutable_id()->set_value( "bad-uuid" );
    input.PackFrom( wire );
    BOOST_CHECK( !UnpackSchematicItem( input, error ) );
    BOOST_CHECK_EQUAL( before->SerializeAsString(), PackSchematicItem( original )->SerializeAsString() );
}
BOOST_AUTO_TEST_CASE( LabelsTextAndTransformValidation )
{
    using namespace kiapi::schematic::types;
    std::string error;
    for( int kind = 0; kind < 4; ++kind )
    {
        google::protobuf::Any input;
        auto prepare = [&]( auto& label )
        {
            label.mutable_position()->set_x_nm( 12000000 );
            label.mutable_position()->set_y_nm( -2000000 );
            label.mutable_text()->mutable_text()->set_text( "CLK" );
            label.set_spin( 1 );
            label.set_shape( 2 );
            (*label.mutable_properties())["Netclass"] = "Fast";
            BOOST_REQUIRE( input.PackFrom( label ) );
        };
        if( kind == 0 ) { LocalLabel label; prepare( label ); }
        if( kind == 1 ) { GlobalLabel label; prepare( label ); }
        if( kind == 2 ) { HierarchicalLabel label; prepare( label ); }
        if( kind == 3 ) { DirectiveLabel label; prepare( label ); }
        auto item = UnpackSchematicItem( input, error );
        BOOST_REQUIRE_MESSAGE( item, error );
        auto* label = static_cast<SCH_LABEL_BASE*>( item.get() );
        BOOST_CHECK_EQUAL( label->GetPosition().x, 120000 );
        BOOST_CHECK_EQUAL( label->GetPosition().y, -20000 );
        BOOST_CHECK_EQUAL( label->GetText(), "CLK" );
        BOOST_CHECK_EQUAL( int( label->GetSpinStyle() ), 1 );
        auto packed = PackSchematicItem( *item );
        BOOST_REQUIRE( packed );
        auto restored = UnpackSchematicItem( *packed, error );
        BOOST_REQUIRE_MESSAGE( restored, error );
        BOOST_CHECK( sameItem( *PackSchematicItem( *restored ), *packed ) );
    }
    LocalLabel bad;
    bad.mutable_text()->mutable_text()->set_text( "CLK" );
    bad.set_spin( 4 );
    google::protobuf::Any data;
    BOOST_REQUIRE( data.PackFrom( bad ) );
    BOOST_CHECK( !UnpackSchematicItem( data, error ) );
    bad.set_spin( 0 );
    bad.mutable_text()->mutable_text()->mutable_attributes()->mutable_size()->set_x_nm( -1 );
    BOOST_REQUIRE( data.PackFrom( bad ) );
    BOOST_CHECK( !UnpackSchematicItem( data, error ) );
    Text text;
    text.mutable_text()->set_text( "Note" );
    text.mutable_text()->mutable_position()->set_x_nm( 3000000 );
    BOOST_REQUIRE( data.PackFrom( text ) );
    auto item = UnpackSchematicItem( data, error );
    BOOST_REQUIRE_MESSAGE( item, error );
    BOOST_CHECK_EQUAL( item->GetPosition().x, 30000 );
}
BOOST_AUTO_TEST_CASE( ConnectionMarkersRoundTripAndValidation )
{
    using namespace kiapi::schematic::types;
    Junction junction; junction.set_diameter_nm( 508000 );
    junction.mutable_position()->set_x_nm( -2540000 );
    NoConnect nc; nc.mutable_position()->set_y_nm( 5080000 );
    BusEntry wireEntry; wireEntry.mutable_size()->set_x_nm( 2540000 );
    wireEntry.mutable_size()->set_y_nm( -2540000 );
    BusEntry busEntry = wireEntry; busEntry.set_bus_to_bus( true );
    for( const google::protobuf::Message* value : std::vector<const google::protobuf::Message*>{ &junction, &nc, &wireEntry, &busEntry } )
    {
        google::protobuf::Any data; BOOST_REQUIRE( data.PackFrom( *value ) );
        std::string error; auto item = UnpackSchematicItem( data, error );
        BOOST_REQUIRE_MESSAGE( item, error );
        auto packed = PackSchematicItem( *item ); BOOST_REQUIRE( packed );
        auto restored = UnpackSchematicItem( *packed, error ); BOOST_REQUIRE_MESSAGE( restored, error );
        BOOST_CHECK( sameItem( *PackSchematicItem( *restored ), *packed ) );
    }
    std::string error; google::protobuf::Any data;
    wireEntry.clear_size(); BOOST_REQUIRE( data.PackFrom( wireEntry ) );
    BOOST_CHECK( !UnpackSchematicItem( data, error ) );
    junction.set_diameter_nm( -1 ); BOOST_REQUIRE( data.PackFrom( junction ) );
    BOOST_CHECK( !UnpackSchematicItem( data, error ) );
}
BOOST_AUTO_TEST_CASE( SymbolPinsUseAbsoluteTransformedCoordinates )
{
    LIB_SYMBOL definition( "Test" );
    auto* pin = new SCH_PIN( &definition );
    pin->SetNumber( "1" ); pin->SetName( "IN" );
    pin->SetPosition( VECTOR2I( -10000, 20000 ) );
    pin->SetUnit( 1 ); pin->SetBodyStyle( 1 );
    pin->SetType( ELECTRICAL_PINTYPE::PT_INPUT );
    definition.AddDrawItem( pin );
    SCH_SHEET root;
    SCH_SHEET_PATH path; path.push_back( &root );
    kiapi::schematic::types::Symbol input;
    input.mutable_lib_id()->set_library_nickname( "Test" );
    input.mutable_lib_id()->set_entry_name( "Test" );
    input.mutable_position()->set_x_nm( 10000000 );
    input.mutable_position()->set_y_nm( 30000000 );
    input.set_unit( 1 ); input.set_body_style( 1 );
    for( int rotation : { 0, 90, 180, 270 } )
        for( bool mirror : { false, true } )
        {
            input.set_rotation_degrees( rotation ); input.set_mirror_x( mirror );
            std::string error;
            auto symbol = UnpackSchematicSymbol( input, definition, path, nullptr, error );
            BOOST_REQUIRE_MESSAGE( symbol, error );
            symbol->SetRef( &path, "U1" );
            auto out = PackSchematicSymbol( *symbol, path );
            BOOST_REQUIRE_EQUAL( out.pins_size(), 1 );
            auto expected = symbol->GetTransform().TransformCoordinate( pin->GetPosition() ) + symbol->GetPosition();
            BOOST_CHECK_EQUAL( out.pins( 0 ).position().x_nm(), int64_t( expected.x ) * 100 );
            BOOST_CHECK_EQUAL( out.pins( 0 ).position().y_nm(), int64_t( expected.y ) * 100 );
            BOOST_CHECK_EQUAL( out.reference(), "U1" );
            BOOST_CHECK( !out.pins( 0 ).id().value().empty() );
        }
    std::string error;
    input.set_rotation_degrees( 45 );
    BOOST_CHECK( !UnpackSchematicSymbol( input, definition, path, nullptr, error ) );
    input.set_rotation_degrees( 0 ); input.set_unit( 2 );
    BOOST_CHECK( !UnpackSchematicSymbol( input, definition, path, nullptr, error ) );
}
BOOST_AUTO_TEST_CASE( StructuredLibraryDefinitionsPreserveUnitsAndValidateBeforeMutation )
{
    kiapi::schematic::types::LibrarySymbolDefinition input;
    input.mutable_lib_id()->set_library_nickname( "Project" );
    input.mutable_lib_id()->set_entry_name( "TwoUnit" );
    input.set_unit_count( 2 ); input.set_body_style_count( 2 ); input.set_units_locked( true );
    input.set_keywords( "timer dual" );
    auto* reference = input.add_fields(); reference->set_name( "Reference" ); reference->mutable_text()->set_text( "U" );
    auto* footprint = input.add_fields(); footprint->set_name( "Footprint" ); footprint->mutable_text()->set_text( "Package_QFN:Test" );
    for( int unit = 1; unit <= 2; ++unit )
    {
        auto* pin = input.add_pins(); pin->set_number( "1" ); pin->set_name( "IN" ); pin->set_unit( unit ); pin->set_body_style( unit );
        pin->set_electrical_type( kiapi::common::types::EPT_INPUT );
        pin->mutable_position()->set_x_nm( -2540000 ); pin->set_length_nm( 1270000 ); pin->set_visible( true );
        pin->set_name_size_nm( 1270000 ); pin->set_number_size_nm( 1270000 );
    }
    auto* graphic = input.add_graphics(); graphic->set_unit( 0 ); graphic->set_body_style( 0 );
    graphic->mutable_shape()->mutable_rectangle()->mutable_top_left()->set_x_nm( -1270000 );
    graphic->mutable_shape()->mutable_rectangle()->mutable_bottom_right()->set_x_nm( 1270000 );
    graphic->mutable_shape()->mutable_rectangle()->mutable_bottom_right()->set_y_nm( 2540000 );
    graphic->set_fill_mode( 3 );
    auto* box = input.add_graphics();
    box->set_unit( 2 ); box->set_body_style( 2 ); box->set_fill_mode( 3 );
    box->mutable_text_box()->set_text( "Second body note" );
    box->mutable_text_box()->mutable_bottom_right()->set_x_nm( 10000000 );
    box->mutable_text_box()->mutable_bottom_right()->set_y_nm( 5000000 );
    box->mutable_text_box()->mutable_margin_left()->set_value_nm( 254000 );
    std::string error;
    auto native = UnpackLibrarySymbol( input, error ); BOOST_REQUIRE_MESSAGE( native, error );
    auto packed = PackLibrarySymbol( *native, error ); BOOST_REQUIRE_MESSAGE( packed, error );
    BOOST_CHECK_EQUAL( packed->unit_count(), 2 ); BOOST_CHECK_EQUAL( packed->body_style_count(), 2 );
    BOOST_CHECK_EQUAL( packed->pins_size(), 2 ); BOOST_CHECK_EQUAL( packed->graphics_size(), 2 );
    BOOST_CHECK_EQUAL( packed->graphics( 0 ).fill_mode(), 3 );
    BOOST_CHECK_EQUAL( packed->graphics( 1 ).text_box().text(), "Second body note" );
    BOOST_CHECK_EQUAL( packed->pins( 0 ).position().x_nm(), -2540000 );
    BOOST_CHECK_EQUAL( native->GetReferenceField().GetText(), "U" );
    BOOST_CHECK_EQUAL( native->GetFootprintField().GetText(), "Package_QFN:Test" );
    auto restored = UnpackLibrarySymbol( *packed, error ); BOOST_REQUIRE_MESSAGE( restored, error );
    BOOST_CHECK( google::protobuf::util::MessageDifferencer::Equals( *PackLibrarySymbol( *restored, error ), *packed ) );
    const std::string before = packed->SerializeAsString();
    input.mutable_pins( 1 )->set_unit( 1 ); input.mutable_pins( 1 )->set_body_style( 0 );
    BOOST_CHECK( !UnpackLibrarySymbol( input, error ) );
    input.mutable_pins( 1 )->set_unit( 3 ); BOOST_CHECK( !UnpackLibrarySymbol( input, error ) );
    input.mutable_pins( 1 )->set_unit( 2 ); input.mutable_pins( 1 )->set_electrical_type( kiapi::common::types::EPT_UNKNOWN );
    BOOST_CHECK( !UnpackLibrarySymbol( input, error ) );
    BOOST_CHECK_EQUAL( PackLibrarySymbol( *native, error )->SerializeAsString(), before );
}
BOOST_AUTO_TEST_SUITE_END()
