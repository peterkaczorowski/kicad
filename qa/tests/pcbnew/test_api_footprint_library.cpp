/* Copyright The KiCad Developers, see AUTHORS.txt. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <api/api_pcb_library_utils.h>
#include <board.h>
#include <footprint.h>
#include <pad.h>
#include <pcb_shape.h>
#include <pcb_field.h>
#include <netinfo.h>
#include <limits>

namespace
{
std::unique_ptr<FOOTPRINT> example()
{
    auto fp = std::make_unique<FOOTPRINT>( nullptr );
    fp->SetFPID( LIB_ID( "Project", "Asymmetric" ) );
    fp->SetReference( "U42" ); fp->SetValue( "User value" );
    fp->SetLocalClearance( 150000 ); fp->SetLocalSolderMaskMargin( 25000 );
    fp->SetLocalSolderPasteMarginRatio( -0.1 );
    fp->AddNetTiePadGroup( "1,2" );
    for( int i = 0; i < 2; ++i )
    {
        auto* pad = new PAD( fp.get() );
        pad->SetNumber( wxString::Format( "%d", i + 1 ) );
        pad->SetPosition( VECTOR2I( i * 2400000 - 500000, 700000 + i * 300000 ) );
        pad->SetSize( F_Cu, VECTOR2I( 600000, 1200000 ) );
        pad->SetAttribute( PAD_ATTRIB::SMD );
        pad->SetLayerSet( LSET( { F_Cu, F_Mask, F_Paste } ) );
        fp->Add( pad );
    }
    FP_3DMODEL model;
    model.m_Filename = "${KIPRJMOD}/asymmetric.step";
    fp->Models().push_back( model );
    return fp;
}
}

BOOST_AUTO_TEST_SUITE( FootprintLibraryApi )
BOOST_AUTO_TEST_CASE( BackSideRecoveryIsDetachedAndPreservesSource )
{
    BOARD board;
    auto owned = example(); owned->SetParent( &board );
    auto* fp = owned.release(); board.Add( fp );
    auto* net = new NETINFO_ITEM( &board, "SOURCE", 0 ); board.Add( net );
    for( PAD* pad : fp->Pads() ) pad->SetNet( net );
    const auto expected = kiapi::board::PackFootprintDefinition( *fp );
    fp->SetPosition( VECTOR2I( 123000000, 76000000 ) );
    fp->SetOrientationDegrees( 37 ); fp->SetLayerAndFlip( B_Cu ); fp->SetLocked( true );
    KIID_PATH path; path.push_back( KIID() ); path.push_back( KIID() ); fp->SetPath( path );
    google::protobuf::Any before; fp->Serialize( before );
    const auto recovered = kiapi::board::PackFootprintDefinition( *fp );
    google::protobuf::Any after; fp->Serialize( after );
    BOOST_CHECK_EQUAL( before.SerializeAsString(), after.SerializeAsString() );
    BOOST_CHECK_EQUAL( board.ResolveItem( fp->m_Uuid, true ), fp );
    for( PAD* pad : fp->Pads() ) BOOST_CHECK_EQUAL( board.ResolveItem( pad->m_Uuid, true ), pad );
    BOOST_CHECK_EQUAL( recovered.position().x_nm(), 0 );
    BOOST_CHECK_EQUAL( recovered.position().y_nm(), 0 );
    BOOST_CHECK_EQUAL( recovered.orientation().value_degrees(), 0 );
    BOOST_CHECK_EQUAL( recovered.layer(), kiapi::board::types::BL_F_Cu );
    BOOST_CHECK_EQUAL( recovered.symbol_path().path_size(), 0 );
    std::string error;
    auto local = kiapi::board::UnpackFootprintDefinition( recovered, error );
    BOOST_REQUIRE_MESSAGE( local, error );
    auto original = kiapi::board::UnpackFootprintDefinition( expected, error );
    BOOST_REQUIRE_MESSAGE( original, error );
    BOOST_REQUIRE_EQUAL( local->Pads().size(), original->Pads().size() );
    auto a = local->Pads().begin(), b = original->Pads().begin();
    for( ; a != local->Pads().end(); ++a, ++b )
    {
        // Native integer rotations round to nm; inverse transforms may differ by 1nm.
        BOOST_CHECK_SMALL( double( ( *a )->GetPosition().x - ( *b )->GetPosition().x ), 2.0 );
        BOOST_CHECK_SMALL( double( ( *a )->GetPosition().y - ( *b )->GetPosition().y ), 2.0 );
        BOOST_CHECK( ( *a )->GetLayerSet() == ( *b )->GetLayerSet() );
        BOOST_CHECK( ( *a )->GetSize( F_Cu ) == ( *b )->GetSize( F_Cu ) );
        BOOST_CHECK_EQUAL( ( *a )->GetNetCode(), 0 );
    }
    BOOST_CHECK_EQUAL( local->GetReference(), "REF**" );
    BOOST_CHECK_EQUAL( local->GetValue(), "Asymmetric" );
    BOOST_CHECK( local->GetLocalClearance() == fp->GetLocalClearance() );
    BOOST_CHECK( local->GetLocalSolderMaskMargin() == fp->GetLocalSolderMaskMargin() );
    BOOST_CHECK( local->GetLocalSolderPasteMarginRatio() == fp->GetLocalSolderPasteMarginRatio() );
    BOOST_CHECK( local->MapPadNumbersToNetTieGroups() == fp->MapPadNumbersToNetTieGroups() );
    BOOST_CHECK_EQUAL( local->Models().front().m_Filename, fp->Models().front().m_Filename );
}

BOOST_AUTO_TEST_CASE( InvalidModelsAreRefusedInsteadOfSilentlyTruncated )
{
    using namespace kiapi::board;
    const auto source = PackFootprintDefinition( *example() );
    auto refuses = [&]( const types::FootprintInstance& input )
    {
        std::string error;
        BOOST_CHECK( !UnpackFootprintDefinition( input, error ) );
        BOOST_CHECK( !error.empty() );
    };
    auto input = source; input.mutable_orientation()->set_value_degrees( 1 ); refuses( input );
    input = source; input.set_layer( types::BL_B_Cu ); refuses( input );
    input = source; input.mutable_definition()->mutable_id()->set_entry_name( "../escape" ); refuses( input );
    input = source; input.mutable_definition()->add_items()->set_type_url( "type.googleapis.com/unknown" ); refuses( input );
    input = source; input.mutable_position()->set_x_nm( std::numeric_limits<int64_t>::max() ); refuses( input );
    input = source; input.mutable_orientation()->set_value_degrees( std::numeric_limits<double>::quiet_NaN() ); refuses( input );
    input = source; *input.mutable_definition()->add_items() = input.definition().items( 0 ); refuses( input );
    types::Pad pad;
    BOOST_REQUIRE( source.definition().items( 0 ).UnpackTo( &pad ) );
    pad.mutable_net()->set_name( "BAD" );
    input = source; input.mutable_definition()->mutable_items( 0 )->PackFrom( pad ); refuses( input );
    pad.clear_net(); pad.mutable_pad_stack()->mutable_copper_layers( 0 )->mutable_size()->set_x_nm( -1 );
    input = source; input.mutable_definition()->mutable_items( 0 )->PackFrom( pad ); refuses( input );
    BOOST_REQUIRE( source.definition().items( 0 ).UnpackTo( &pad ) );
    pad.mutable_symbol_pin()->set_name( "Instance pin function" );
    input = source; input.mutable_definition()->mutable_items( 0 )->PackFrom( pad ); refuses( input );
    input = source; input.mutable_definition()->mutable_attributes()->set_not_in_schematic( true ); refuses( input );
    input = source;
    input.mutable_definition()->mutable_items( input.definition().items_size() - 1 )
            ->set_type_url( "custom/kiapi.board.types.Footprint3DModel" );
    refuses( input );
    types::Zone zone; zone.mutable_id()->set_value( KIID().AsStdString() );
    input = source; input.mutable_definition()->add_items()->PackFrom( zone ); refuses( input );
}
BOOST_AUTO_TEST_SUITE_END()
