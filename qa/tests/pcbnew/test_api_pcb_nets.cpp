/* Copyright The KiCad Developers, see AUTHORS.txt. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <api/api_pcb_utils.h>
#include <board.h>
#include <netinfo.h>
#include <pcb_track.h>
#include <footprint.h>
#include <pad.h>

BOOST_AUTO_TEST_SUITE( PcbApiNetSafety )
BOOST_AUTO_TEST_CASE( UnknownNetNeverCreatesOrChangesAssignment )
{
    BOARD board;
    auto* net = new NETINFO_ITEM( &board, "GND", 0 ); board.Add( net );
    PCB_TRACK track( &board ); track.SetNet( net );
    kiapi::board::types::Net input; input.set_name( "TYPO" );
    BOOST_CHECK( !track.UnpackNet( input ) );
    BOOST_CHECK( board.FindNet( "TYPO" ) == nullptr );
    BOOST_CHECK_EQUAL( track.GetNetname(), "GND" );
    input.set_name( "GND" ); BOOST_CHECK( track.UnpackNet( input ) );
    input.clear_name(); BOOST_CHECK( track.UnpackNet( input ) );
    BOOST_CHECK_EQUAL( track.GetNetCode(), 0 );
}
BOOST_AUTO_TEST_CASE( NestedFootprintPadsValidateBeforeNativeDeserialization )
{
    BOARD board;
    kiapi::board::types::Pad pad; pad.mutable_net()->set_name( "TYPO" );
    kiapi::board::types::FootprintInstance footprint;
    footprint.mutable_definition()->add_items()->PackFrom( pad );
    google::protobuf::Any packed; packed.PackFrom( footprint );
    std::set<std::string> missing; std::string error;
    BOOST_CHECK( !kiapi::board::ValidateNetReferences( packed, board, false, missing, error ) );
    BOOST_CHECK( error.find( "TYPO" ) != std::string::npos );
    BOOST_CHECK( board.FindNet( "TYPO" ) == nullptr );
    BOOST_CHECK( kiapi::board::ValidateNetReferences( packed, board, true, missing, error ) );
    BOOST_CHECK( missing == std::set<std::string>{ "TYPO" } );
    BOOST_CHECK( board.FindNet( "TYPO" ) == nullptr );
    board.Add( new NETINFO_ITEM( &board, "TYPO", 0 ) ); missing.clear();
    BOOST_CHECK( kiapi::board::ValidateNetReferences( packed, board, false, missing, error ) );
    BOOST_CHECK( missing.empty() );
}
BOOST_AUTO_TEST_CASE( TrackDeserializerPropagatesUnknownNetFailure )
{
    BOARD board; PCB_TRACK track( &board );
    kiapi::board::types::Track input;
    input.mutable_id()->set_value( KIID().AsStdString() );
    input.set_layer( kiapi::board::types::BL_F_Cu );
    input.mutable_net()->set_name( "TYPO" );
    google::protobuf::Any packed; packed.PackFrom( input );
    BOOST_CHECK( !track.Deserialize( packed ) );
    BOOST_CHECK( board.FindNet( "TYPO" ) == nullptr );
}
BOOST_AUTO_TEST_SUITE_END()
