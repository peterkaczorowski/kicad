/* Copyright The KiCad Developers. SPDX-License-Identifier: GPL-3.0-or-later */
#include <qa_utils/wx_utils/unit_test_utils.h>
#include <api/api_sch_connectivity.h>
#include <api/api_sch_net_rename.h>
#include <schematic.h>
#include <connection_graph.h>
#include <sch_sheet.h>
#include <sch_sheet_pin.h>
#include <sch_screen.h>
#include <sch_label.h>
#include <sch_line.h>
#include <settings/settings_manager.h>
#include <locale_io.h>

struct NET_RENAME_FIXTURE
{
    SETTINGS_MANAGER manager;
    std::unique_ptr<SCHEMATIC> schematic;
    SCH_SCREEN* root;
    NET_RENAME_FIXTURE()
    {
        manager.LoadProject( "" );
        schematic = std::make_unique<SCHEMATIC>( &manager.Prj() );
        schematic->Reset();
        root = schematic->GetTopLevelSheet( 0 )->GetScreen();
        root->SetFileName( "rename-root.kicad_sch" );
    }
    SCH_SHEET* child( const wxString& name, SCH_SCREEN* shared = nullptr )
    {
        auto* sheet = new SCH_SHEET( schematic->GetTopLevelSheet( 0 ), VECTOR2I( 100000, 100000 ), VECTOR2I( 20000, 10000 ) );
        sheet->SetName( name ); sheet->SetFileName( name + ".kicad_sch" );
        sheet->SetScreen( shared ? shared : new SCH_SCREEN( schematic.get() ) );
        sheet->GetScreen()->SetFileName( name + ".kicad_sch" );
        root->Append( sheet );
        schematic->RefreshHierarchy();
        return sheet;
    }
    auto snapshot()
    {
        schematic->RefreshHierarchy();
        schematic->ConnectionGraph()->Recalculate( schematic->Hierarchy(), true );
        auto result = BuildSchematicConnectivity( *schematic );
        StampSchematicConnectivity( result, "rename-test", 1 );
        return result;
    }
    std::string netFor( const kiapi::schematic::types::ConnectivitySnapshot& state, SCH_ITEM* item )
    {
        for( const auto& net : state.nets() )
            for( const auto& member : net.members() )
                if( member.id().value() == item->m_Uuid.AsStdString() ) return net.id();
        BOOST_FAIL( "Missing test item" ); return "";
    }
};
BOOST_FIXTURE_TEST_SUITE( SchematicNetRenameApi, NET_RENAME_FIXTURE )

BOOST_AUTO_TEST_CASE( GlobalRenamePlanAcrossSheetsRestoresModelAndFlags )
{
    LOCALE_IO locale;
    auto* a = new SCH_GLOBALLABEL( VECTOR2I( 10000, 10000 ), "OLD" ); root->Append( a );
    auto* sub = child( "child" );
    auto* b = new SCH_GLOBALLABEL( VECTOR2I( 10000, 10000 ), "OLD" ); sub->GetScreen()->Append( b );
    auto before = snapshot();
    root->SetContentModified( false ); sub->GetScreen()->SetContentModified( false );
    auto analysis = PlanSchematicNetRename( *schematic, before, netFor( before, a ), "NEW" );
    BOOST_CHECK_MESSAGE( analysis.plan.can_apply(), analysis.plan.DebugString() );
    BOOST_CHECK_EQUAL( analysis.plan.changes_size(), 2 );
    BOOST_CHECK_EQUAL( analysis.edits.size(), 2 );
    BOOST_CHECK_EQUAL( analysis.plan.resolved_name(), "NEW" );
    BOOST_CHECK_EQUAL( a->GetText(), "OLD" ); BOOST_CHECK_EQUAL( b->GetText(), "OLD" );
    BOOST_CHECK( !root->IsContentModified() ); BOOST_CHECK( !sub->GetScreen()->IsContentModified() );
    BOOST_CHECK_EQUAL( before.SerializeAsString(), snapshot().SerializeAsString() );
}

BOOST_AUTO_TEST_CASE( ExistingTargetAlwaysRefusedWithoutModification )
{
    LOCALE_IO locale;
    auto* a = new SCH_GLOBALLABEL( VECTOR2I( 10000, 10000 ), "OLD" ); root->Append( a );
    root->Append( new SCH_GLOBALLABEL( VECTOR2I( 20000, 20000 ), "EXISTING" ) );
    auto before = snapshot();
    auto analysis = PlanSchematicNetRename( *schematic, before, netFor( before, a ), "EXISTING" );
    BOOST_CHECK( !analysis.plan.can_apply() );
    BOOST_REQUIRE_EQUAL( analysis.plan.collisions_size(), 1 );
    BOOST_CHECK_EQUAL( analysis.plan.collisions( 0 ).name(), "EXISTING" );
    BOOST_CHECK( analysis.plan.blockers_size() > 0 );
    BOOST_CHECK_EQUAL( before.SerializeAsString(), snapshot().SerializeAsString() );
}

BOOST_AUTO_TEST_CASE( HierarchicalPortAndChildLabelRenamedTogether )
{
    LOCALE_IO locale;
    auto* sub = child( "child" );
    auto* pin = new SCH_SHEET_PIN( sub, VECTOR2I( 100000, 105000 ), "SIGNAL" );
    pin->SetSide( SHEET_SIDE::LEFT ); sub->AddPin( pin );
    auto* label = new SCH_HIERLABEL( VECTOR2I( 10000, 10000 ), "SIGNAL" ); sub->GetScreen()->Append( label );
    auto before = snapshot();
    auto analysis = PlanSchematicNetRename( *schematic, before, netFor( before, pin ), "RENAMED" );
    BOOST_CHECK_MESSAGE( analysis.plan.can_apply(), analysis.plan.DebugString() );
    BOOST_CHECK_EQUAL( analysis.edits.size(), 2 );
    BOOST_CHECK_EQUAL( before.SerializeAsString(), snapshot().SerializeAsString() );
}

BOOST_AUTO_TEST_CASE( HeterogeneousAliasesAndInvalidNamesRefused )
{
    LOCALE_IO locale;
    auto* a = new SCH_GLOBALLABEL( VECTOR2I( 10000, 10000 ), "ONE" ); root->Append( a );
    auto before = snapshot();
    for( const auto& name : { "", "BAD NAME", "BUS[0..3]", "${VAR}", "other/name" } )
    {
        auto analysis = PlanSchematicNetRename( *schematic, before, netFor( before, a ), name );
        BOOST_CHECK( !analysis.plan.can_apply() );
        BOOST_CHECK( analysis.plan.blockers_size() );
    }
    BOOST_CHECK_EQUAL( before.SerializeAsString(), snapshot().SerializeAsString() );
    root->Append( new SCH_LABEL( VECTOR2I( 10000, 10000 ), "ALIAS" ) );
    before = snapshot();
    auto aliases = PlanSchematicNetRename( *schematic, before, netFor( before, a ), "NEW" );
    BOOST_CHECK( !aliases.plan.can_apply() );
    BOOST_CHECK_EQUAL( before.SerializeAsString(), snapshot().SerializeAsString() );
}

BOOST_AUTO_TEST_CASE( BusAndGeneratedNamesRefused )
{
    LOCALE_IO locale;
    auto* bus = new SCH_LABEL( VECTOR2I( 10000, 10000 ), "BUS[0..1]" ); root->Append( bus );
    auto* wire = new SCH_LINE( VECTOR2I( 50000, 50000 ), LAYER_WIRE );
    wire->SetEndPoint( VECTOR2I( 60000, 50000 ) ); root->Append( wire );
    auto before = snapshot();
    BOOST_CHECK( !PlanSchematicNetRename( *schematic, before, netFor( before, bus ), "NEW" ).plan.can_apply() );
    BOOST_CHECK( !PlanSchematicNetRename( *schematic, before, netFor( before, wire ), "OTHER" ).plan.can_apply() );
    BOOST_CHECK_EQUAL( before.SerializeAsString(), snapshot().SerializeAsString() );
}

BOOST_AUTO_TEST_CASE( SharedLocalSheetSideEffectsRefused )
{
    LOCALE_IO locale;
    auto* first = child( "one" );
    auto* label = new SCH_LABEL( VECTOR2I( 10000, 10000 ), "LOCAL" ); first->GetScreen()->Append( label );
    child( "two", first->GetScreen() );
    auto before = snapshot();
    auto analysis = PlanSchematicNetRename( *schematic, before, netFor( before, label ), "NEW" );
    BOOST_CHECK( !analysis.plan.can_apply() );
    BOOST_CHECK( analysis.plan.blockers_size() );
    BOOST_CHECK_EQUAL( before.SerializeAsString(), snapshot().SerializeAsString() );
}
BOOST_AUTO_TEST_SUITE_END()
