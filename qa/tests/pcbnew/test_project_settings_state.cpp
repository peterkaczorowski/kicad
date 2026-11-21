/* Copyright The KiCad Developers, see AUTHORS.txt. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <board.h>
#include <board_design_settings.h>
#include <pcb_track.h>
#include <project.h>
#include <project/project_file.h>
#include <settings/json_settings_internals.h>
#include <drc/drc_engine.h>

BOOST_AUTO_TEST_SUITE( ProjectSettingsState )
BOOST_AUTO_TEST_CASE( DirtyTrackingSurvivesMaterializationWithoutChangingTheBaseline )
{
    BOARD_DESIGN_SETTINGS board( nullptr, "board.design_settings" );
    board.MarkSaved();
    const auto initial = board.GetCurrentParams();
    BOOST_CHECK( !board.HasUnsavedChanges() );
    board.m_TrackMinWidth += 10000;
    BOOST_CHECK( board.HasUnsavedChanges() );
    board.Store();
    BOOST_CHECK( board.HasUnsavedChanges() );
    BOOST_CHECK( board.GetCurrentParams() != initial );
    board.MarkSaved();
    BOOST_CHECK( !board.HasUnsavedChanges() );
}

BOOST_AUTO_TEST_CASE( StagedRulesAndConstraintReloadReachTheNativeDrcEngineWithoutDiskWrites )
{
    class TEST_PROJECT : public PROJECT
    {
    public:
        mutable PROJECT_FILE file{ "ipc-settings-in-memory.kicad_pro" };
        TEST_PROJECT() { file.SetProject( this ); }
        PROJECT_FILE& GetProjectFile() const override { return file; }
    } project;
    BOARD board;
    board.SetProject( &project, true );
    auto& design = board.GetDesignSettings();
    design.m_TrackMinWidth = 127000;
    DRC_ENGINE engine( &board, &design );
    auto* track = new PCB_TRACK( &board );
    track->SetStart( { 0, 0 } ); track->SetEnd( { 10000000, 0 } );
    track->SetWidth( 200000 ); board.Add( track );
    project.file.SetPendingBoardRules( "(version 1) (rule \"Staged\" (constraint track_width (min 0.4mm)))" );
    try { engine.InitEngine( wxFileName() ); }
    catch( const PARSE_ERROR& error )
    {
        BOOST_FAIL( error.What().ToStdString() + ": " + error.inputLine );
    }
    auto constraint = engine.EvalRules( TRACK_WIDTH_CONSTRAINT, track, nullptr, F_Cu );
    BOOST_CHECK_EQUAL( constraint.Value().Min(), 400000 );
    BOOST_CHECK( project.file.PendingBoardRules().has_value() );
    project.file.SetPendingBoardRules( std::nullopt );
    engine.InitEngine( wxFileName() );
    constraint = engine.EvalRules( TRACK_WIDTH_CONSTRAINT, track, nullptr, F_Cu );
    BOOST_CHECK_EQUAL( constraint.Value().Min(), 127000 );
    design.m_TrackMinWidth = 203200;
    engine.InitEngine( wxFileName() );
    constraint = engine.EvalRules( TRACK_WIDTH_CONSTRAINT, track, nullptr, F_Cu );
    BOOST_CHECK_EQUAL( constraint.Value().Min(), 203200 );
}

BOOST_AUTO_TEST_CASE( BoardReloadDoesNotMarkUnsavedProjectParametersAsSaved )
{
    PROJECT_FILE project( "ipc-unsaved-project.kicad_pro" );
    BOARD_DESIGN_SETTINGS first( nullptr, "board.design_settings" );
    project.Set( "board.design_settings", first.GetCurrentParams() );
    first.SetParent( &project );
    BOOST_REQUIRE( !first.HasUnsavedChanges() );
    first.m_TrackMinWidth = 127000;
    first.SaveToFile(); // Native board release flushes into the parent, not onto disk.
    BOARD_DESIGN_SETTINGS reopened( nullptr, "board.design_settings" );
    reopened.SetParent( &project );
    BOOST_CHECK_EQUAL( reopened.m_TrackMinWidth, 127000 );
    BOOST_CHECK( reopened.HasUnsavedChanges() );
}
BOOST_AUTO_TEST_SUITE_END()
