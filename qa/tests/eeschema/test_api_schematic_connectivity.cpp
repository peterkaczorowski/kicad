/* Copyright The KiCad Developers. SPDX-License-Identifier: GPL-3.0-or-later */
#include <qa_utils/wx_utils/unit_test_utils.h>
#include <schematic_utils/schematic_file_util.h>
#include <api/api_sch_connectivity.h>
#include <connection_graph.h>
#include <schematic.h>
#include <sch_sheet.h>
#include <sch_screen.h>
#include <sch_label.h>
#include <sch_pin.h>
#include <sch_no_connect.h>
#include <settings/settings_manager.h>
#include <locale_io.h>
#include <set>

struct CONNECTIVITY_API_FIXTURE
{
    SETTINGS_MANAGER manager;
    std::unique_ptr<SCHEMATIC> schematic;
    void load( const std::string& fixture ) { KI_TEST::LoadSchematic( manager, fixture, schematic ); }
};

BOOST_FIXTURE_TEST_SUITE( SchematicConnectivityApi, CONNECTIVITY_API_FIXTURE )

BOOST_AUTO_TEST_CASE( HierarchicalBusAliasesAndSharedSheetInstances )
{
    LOCALE_IO locale;
    load( "issue18299/issue18299" );
    auto snapshot = BuildSchematicConnectivity( *schematic );
    bool busFound = false, sharedNetFound = false;
    std::set<std::string> sharedIds;
    std::set<std::string> ids;
    for( const auto& net : snapshot.nets() )
    {
        ids.insert( net.id() );
        if( net.is_bus() && net.bus_member_ids_size() > 0 ) busFound = true;
        std::set<std::string> instances;
        for( const auto& member : net.members() )
            if( member.text() == "b" ) instances.insert( member.document().sheet_path().path_human_readable() );
        if( instances.size() >= 2 )
        {
            sharedNetFound = true;
            sharedIds.insert( net.id() );
        }
    }
    BOOST_CHECK( busFound );
    BOOST_CHECK( sharedNetFound );
    bool linkedThroughBus = false;
    for( const auto& net : snapshot.nets() )
        for( const auto& id : net.bus_member_ids() ) linkedThroughBus |= sharedIds.count( id ) > 0;
    BOOST_CHECK( linkedThroughBus );
    for( const auto& net : snapshot.nets() )
        for( const auto& member : net.bus_member_ids() ) BOOST_CHECK( ids.count( member ) );
    std::string canonical = snapshot.SerializeAsString();
    schematic->ConnectionGraph()->Recalculate( schematic->Hierarchy(), true );
    BOOST_CHECK_EQUAL( canonical, BuildSchematicConnectivity( *schematic ).SerializeAsString() );
    StampSchematicConnectivity( snapshot, "session-one", 12 );
    BOOST_CHECK_EQUAL( snapshot.session_id(), "session-one" );
    BOOST_CHECK_EQUAL( snapshot.revision(), 12 );
    std::set<std::string> stamped;
    for( const auto& net : snapshot.nets() )
    {
        BOOST_CHECK_EQUAL( net.id().find( "session-one/12/" ), 0 );
        stamped.insert( net.id() );
    }
    for( const auto& net : snapshot.nets() )
        for( const auto& member : net.bus_member_ids() ) BOOST_CHECK( stamped.count( member ) );
}

BOOST_AUTO_TEST_CASE( UnsavedLabelChangesAreReadFromLiveModel )
{
    LOCALE_IO locale;
    load( "issue18299/issue18299" );
    auto before = BuildSchematicConnectivity( *schematic ).SerializeAsString();
    SCH_LABEL_BASE* label = nullptr;
    for( const auto& path : schematic->Hierarchy() )
        for( SCH_ITEM* item : path.LastScreen()->Items() )
            if( item->Type() == SCH_LABEL_T ) label = static_cast<SCH_LABEL_BASE*>( item );
    BOOST_REQUIRE( label );
    label->SetText( "IPC_UNSAVED_LABEL" );
    schematic->ConnectionGraph()->Recalculate( schematic->Hierarchy(), true );
    auto after = BuildSchematicConnectivity( *schematic );
    BOOST_CHECK( before != after.SerializeAsString() );
    bool found = false;
    for( const auto& net : after.nets() )
        for( const auto& member : net.members() ) if( member.text() == "IPC_UNSAVED_LABEL" ) found = true;
    BOOST_CHECK( found );
}

BOOST_AUTO_TEST_CASE( NativePinMetadataPowerAndNoConnectArePreserved )
{
    LOCALE_IO locale;
    load( "issue22938/issue22938" );
    auto before = BuildSchematicConnectivity( *schematic );
    int pins = 0;
    for( const auto& net : before.nets() )
        for( const auto& member : net.members() )
            if( member.kind() == "pin" )
            {
                ++pins;
                BOOST_CHECK( !member.reference().empty() );
                BOOST_CHECK( !member.pin_number().empty() );
                BOOST_CHECK( !member.electrical_type().empty() );
                BOOST_CHECK( !member.symbol_id().value().empty() );
                BOOST_CHECK( member.document().sheet_path().path_size() > 0 );
            }
    BOOST_CHECK( pins > 0 );
    SCH_PIN* testPin = nullptr;
    SCH_SHEET_PATH pinPath;
    for( const auto& [key, graphs] : schematic->ConnectionGraph()->GetNetMap() )
        for( auto* graph : graphs )
            for( SCH_ITEM* item : graph->GetItems() )
                if( item->Type() == SCH_PIN_T )
                {
                    testPin = static_cast<SCH_PIN*>( item ); pinPath = graph->GetSheet();
                }
    BOOST_REQUIRE( testPin );
    auto* nc = new SCH_NO_CONNECT( testPin->GetPosition() );
    pinPath.LastScreen()->Append( nc );
    schematic->ConnectionGraph()->Recalculate( schematic->Hierarchy(), true );
    auto after = BuildSchematicConnectivity( *schematic );
    bool marked = false;
    for( const auto& net : after.nets() )
        for( const auto& member : net.members() )
            if( member.id().value() == testPin->m_Uuid.AsStdString() && member.no_connect() ) marked = true;
    BOOST_CHECK( marked );
}
BOOST_AUTO_TEST_SUITE_END()
