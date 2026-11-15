/* Copyright The KiCad Developers, see AUTHORS.txt.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <boost/test/unit_test.hpp>
#include <api/api_sch_utils.h>
#include <sch_line.h>
#include <sch_label.h>
#include <api/schematic/connection_items.pb.h>

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
        BOOST_CHECK_EQUAL( PackSchematicItem( *restored )->SerializeAsString(), packed->SerializeAsString() );
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
        BOOST_CHECK_EQUAL( PackSchematicItem( *restored )->SerializeAsString(), packed->SerializeAsString() );
    }
    std::string error; google::protobuf::Any data;
    wireEntry.clear_size(); BOOST_REQUIRE( data.PackFrom( wireEntry ) );
    BOOST_CHECK( !UnpackSchematicItem( data, error ) );
    junction.set_diameter_nm( -1 ); BOOST_REQUIRE( data.PackFrom( junction ) );
    BOOST_CHECK( !UnpackSchematicItem( data, error ) );
}
BOOST_AUTO_TEST_SUITE_END()
