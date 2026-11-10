/*
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <boost/test/unit_test.hpp>
#include <api/api_sch_utils.h>
#include <sch_sheet.h>
#include <sch_sheet_pin.h>

BOOST_AUTO_TEST_SUITE( SchematicSheetApi )

BOOST_AUTO_TEST_CASE( PackSheetUsesNanometersAndPreservesPins )
{
    SCH_SHEET sheet( nullptr, VECTOR2I( -10000, 20000 ), VECTOR2I( 30000, 40000 ) );
    sheet.SetName( wxS( "power" ) );
    sheet.SetFileName( wxS( "power.kicad_sch" ) );
    auto* pin = new SCH_SHEET_PIN( &sheet, VECTOR2I( -10000, 30000 ), wxS( "+5V" ) );
    pin->SetSide( SHEET_SIDE::LEFT );
    pin->SetLabelShape( LABEL_INPUT );
    sheet.AddPin( pin );

    const auto packed = PackSchematicSheet( sheet );
    BOOST_CHECK_EQUAL( packed.id().value(), sheet.m_Uuid.AsStdString() );
    BOOST_CHECK_EQUAL( packed.name(), "power" );
    BOOST_CHECK_EQUAL( packed.filename(), "power.kicad_sch" );
    BOOST_CHECK_EQUAL( packed.position().x_nm(), -1000000 );
    BOOST_CHECK_EQUAL( packed.position().y_nm(), 2000000 );
    BOOST_CHECK_EQUAL( packed.size().x_nm(), 3000000 );
    BOOST_REQUIRE_EQUAL( packed.pins_size(), 1 );
    BOOST_CHECK_EQUAL( packed.pins( 0 ).id().value(), pin->m_Uuid.AsStdString() );
    BOOST_CHECK_EQUAL( packed.pins( 0 ).sheet_id().value(), sheet.m_Uuid.AsStdString() );
    BOOST_CHECK_EQUAL( packed.pins( 0 ).name(), "+5V" );
    BOOST_CHECK_EQUAL( packed.pins( 0 ).side(), kiapi::schematic::types::SPS_LEFT );
    BOOST_CHECK_EQUAL( packed.pins( 0 ).shape(), kiapi::schematic::types::SPH_INPUT );
    BOOST_CHECK_EQUAL( packed.pins( 0 ).position().y_nm(), 3000000 );
}

BOOST_AUTO_TEST_CASE( EmptySheetHasNoPorts )
{
    SCH_SHEET sheet;
    const auto packed = PackSchematicSheet( sheet );
    BOOST_CHECK_EQUAL( packed.pins_size(), 0 );
}

BOOST_AUTO_TEST_SUITE_END()
