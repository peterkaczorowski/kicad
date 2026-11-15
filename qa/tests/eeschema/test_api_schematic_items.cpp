/* Copyright The KiCad Developers, see AUTHORS.txt.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <boost/test/unit_test.hpp>
#include <api/api_sch_utils.h>
#include <sch_line.h>

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
BOOST_AUTO_TEST_SUITE_END()
