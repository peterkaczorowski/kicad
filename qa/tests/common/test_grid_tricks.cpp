/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#define BOOST_TEST_NO_MAIN
#include <boost/test/unit_test.hpp>

#include <grid_tricks.h>
#include <lib_table_grid_tricks.h>
#include <dialog_shim.h>
#include <base_units.h>
#include <units_provider.h>
#include <qa_utils/wx_utils/unit_test_utils.h>
#include <widgets/grid_combobox.h>
#include <widgets/grid_icon_text_helpers.h>
#include <widgets/grid_text_helpers.h>

#include <array>

#include <wx/app.h>
#include <wx/button.h>
#include <wx/combobox.h>
#include <wx/evtloop.h>
#include <wx/frame.h>
#include <wx/textctrl.h>

#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#include <wx/scopeguard.h>
#endif


#define SKIP_IF_HEADLESS()                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if( !m_grid )                                                                                                  \
        {                                                                                                              \
            BOOST_TEST_MESSAGE( "No display available" );                                                              \
            return;                                                                                                    \
        }                                                                                                              \
    } while( 0 )


namespace
{
class GRID_TRICKS_FIXTURE
{
public:
    GRID_TRICKS_FIXTURE()
    {
        if( !KI_TEST::CanDoDisplayTests() )
            return;

        BOOST_TEST_CHECKPOINT( "Creating the grid test frame" );
        m_frame = new wxFrame( nullptr, wxID_ANY, wxT( "Grid tricks test" ), wxDefaultPosition, wxSize( 640, 400 ) );
        BOOST_TEST_CHECKPOINT( "Creating the grid" );
        m_grid = new WX_GRID( m_frame, wxID_ANY, wxDefaultPosition, wxSize( 600, 350 ) );
        BOOST_TEST_CHECKPOINT( "Initializing the grid cells" );
        m_grid->CreateGrid( 0, 3 );
        m_grid->SetTable( new wxGridStringTable( 5, 3 ), true );
        m_grid->Bind( wxEVT_GRID_CELL_CHANGED, &GRID_TRICKS_FIXTURE::onCellChanged, this );

        for( int row = 0; row < m_grid->GetNumberRows(); ++row )
        {
            for( int col = 0; col < m_grid->GetNumberCols(); ++col )
                m_grid->SetCellValue( row, col, wxString::Format( "%d,%d", row, col ) );
        }

        BOOST_TEST_CHECKPOINT( "Attaching the grid event handler" );
        m_tricks = new GRID_TRICKS( m_grid );
        m_tricks->EnableColumnBulkEdit();
        m_grid->PushEventHandler( m_tricks );
    }

    ~GRID_TRICKS_FIXTURE()
    {
        if( m_grid )
        {
            m_grid->CancelPendingChanges();
            m_grid->PopEventHandler( true );
            delete m_grid;
            m_frame->Destroy();
        }
    }

    bool Edit( int aRow, int aCol, const wxString& aValue )
    {
        const wxString oldValue = m_grid->GetCellValue( aRow, aCol );
        wxGridEvent    changing( m_grid->GetId(), wxEVT_GRID_CELL_CHANGING, m_grid, aRow, aCol );
        changing.SetString( aValue );
        m_grid->GetEventHandler()->ProcessEvent( changing );

        if( !changing.IsAllowed() )
            return false;

        m_grid->SetCellValue( aRow, aCol, aValue );
        NotifyChanged( aRow, aCol, oldValue );
        return true;
    }

    void NotifyChanged( int aRow, int aCol, const wxString& aOldValue )
    {
        wxGridEvent changed( m_grid->GetId(), wxEVT_GRID_CELL_CHANGED, m_grid, aRow, aCol );
        changed.SetString( aOldValue );
        m_grid->GetEventHandler()->ProcessEvent( changed );
    }

    void ClickCell( int aRow, int aCol, bool aControl = false, bool aShift = false )
    {
        const wxRect rect = m_grid->CellToRect( aRow, aCol );
        wxWindow*    window = m_grid->GetGridWindow();

        for( wxEventType type : { wxEVT_LEFT_DOWN, wxEVT_LEFT_UP } )
        {
            wxMouseEvent event( type );
            event.SetEventObject( window );
            event.SetId( window->GetId() );
            event.SetPosition( wxPoint( rect.x + rect.width / 2, rect.y + rect.height / 2 ) );
            event.SetControlDown( aControl );
            event.SetShiftDown( aShift );
            event.SetLeftDown( type == wxEVT_LEFT_DOWN );
            window->ProcessWindowEvent( event );
        }
    }

    void PressKey( wxWindow* aWindow, int aKey, wxEventType aType = wxEVT_KEY_DOWN )
    {
        wxKeyEvent event( aType );
        event.SetEventObject( aWindow );
        event.SetId( aWindow->GetId() );
        event.m_keyCode = aKey;
        event.m_uniChar = aKey;
        aWindow->ProcessWindowEvent( event );
    }

    void SetChoiceColumn( bool aIcons )
    {
        wxArrayString choices;
        choices.Add( wxT( "Input" ) );
        choices.Add( wxT( "Output" ) );
        choices.Add( wxT( "Bidirectional" ) );

        wxGridCellAttr* attr = new wxGridCellAttr;

        if( aIcons )
            attr->SetEditor( new GRID_CELL_ICON_TEXT_POPUP( {}, choices ) );
        else
            attr->SetEditor( new GRID_CELL_COMBOBOX( choices ) );

        m_grid->SetColAttr( 1, attr );

        for( int row = 0; row < 5; ++row )
            m_grid->SetCellValue( row, 1, wxT( "Input" ) );

        m_grid->SetCellValue( 1, 1, wxT( "Output" ) );
        m_grid->SetGridCursor( 0, 1 );
        m_grid->SelectBlock( 0, 1, 2, 1 );
        m_changedCells.clear();
    }

    wxComboBox* GetCombo()
    {
        wxGridCellEditor* editor = m_grid->GetCellEditor( m_grid->GetGridCursorRow(), 1 );
        wxComboBox*       control = dynamic_cast<wxComboBox*>( editor->GetWindow() );
        editor->DecRef();
        return control;
    }

    void SelectChoice( wxComboBox* aControl, int aChoice )
    {
        aControl->SetSelection( aChoice );
        wxCommandEvent event( wxEVT_COMBOBOX, aControl->GetId() );
        event.SetEventObject( aControl );
        event.SetInt( aChoice );
        event.SetString( aControl->GetString( aChoice ) );
        aControl->ProcessWindowEvent( event );
    }

private:
    void onCellChanged( wxGridEvent& aEvent )
    {
        m_changedCells.emplace_back( aEvent.GetRow(), aEvent.GetCol() );

        std::vector<wxString> values;

        for( int row = 0; row < m_grid->GetNumberRows(); ++row )
            values.push_back( m_grid->GetCellValue( row, aEvent.GetCol() ) );

        m_snapshots.push_back( values );
        aEvent.Skip();
    }

protected:
    wxTopLevelWindow*                  m_frame = nullptr;
    WX_GRID*                           m_grid = nullptr;
    GRID_TRICKS*                       m_tricks = nullptr;
    std::vector<wxGridCellCoords>      m_changedCells;
    std::vector<std::vector<wxString>> m_snapshots;
    std::unique_ptr<UNITS_PROVIDER>    m_unitsProvider;
};
} // namespace


BOOST_FIXTURE_TEST_SUITE( GridTricksColumnBulkEdit, GRID_TRICKS_FIXTURE )


BOOST_AUTO_TEST_CASE( BlockSelectionEditsOnlyOneColumn )
{
    SKIP_IF_HEADLESS();

    BOOST_TEST_CHECKPOINT( "Selecting cells for the bulk edit" );
    m_grid->SelectBlock( 1, 0, 3, 2 );
    BOOST_TEST_CHECKPOINT( "Committing the bulk edit" );
    BOOST_REQUIRE( Edit( 2, 1, wxT( "25 mils" ) ) );

    for( int row = 0; row < 5; ++row )
    {
        const wxString expected = row >= 1 && row <= 3 ? wxString( wxT( "25 mils" ) ) : wxString::Format( "%d,1", row );
        BOOST_CHECK_EQUAL( m_grid->GetCellValue( row, 1 ), expected );
        BOOST_CHECK_EQUAL( m_grid->GetCellValue( row, 0 ), wxString::Format( "%d,0", row ) );
        BOOST_CHECK_EQUAL( m_grid->GetCellValue( row, 2 ), wxString::Format( "%d,2", row ) );
    }

    BOOST_REQUIRE_EQUAL( m_changedCells.size(), 3 );
    BOOST_REQUIRE_EQUAL( m_snapshots.size(), 3 );
    BOOST_CHECK_EQUAL( m_snapshots.front()[1], wxT( "25 mils" ) );
    BOOST_CHECK_EQUAL( m_snapshots.front()[2], wxT( "25 mils" ) );
    BOOST_CHECK_EQUAL( m_snapshots.front()[3], wxT( "25 mils" ) );
}


BOOST_AUTO_TEST_CASE( DisjointSelectionSkipsGapsAndReadOnlyCells )
{
    SKIP_IF_HEADLESS();

    m_grid->SelectBlock( 0, 1, 0, 1 );
    m_grid->SelectBlock( 2, 1, 2, 1, true );
    m_grid->SelectBlock( 4, 1, 4, 1, true );
    m_grid->SetReadOnly( 2, 1 );
    BOOST_REQUIRE( Edit( 0, 1, wxT( "new name" ) ) );

    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 0, 1 ), wxT( "new name" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 1, 1 ), wxT( "1,1" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 2, 1 ), wxT( "2,1" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 3, 1 ), wxT( "3,1" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 4, 1 ), wxT( "new name" ) );
    BOOST_CHECK_EQUAL( m_changedCells.size(), 2 );
}


BOOST_AUTO_TEST_CASE( EditingUnselectedCellDoesNotChangeSelection )
{
    SKIP_IF_HEADLESS();

    m_grid->SelectBlock( 0, 1, 2, 1 );
    BOOST_REQUIRE( Edit( 4, 1, wxT( "single" ) ) );

    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 0, 1 ), wxT( "0,1" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 2, 1 ), wxT( "2,1" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 4, 1 ), wxT( "single" ) );
    BOOST_CHECK_EQUAL( m_changedCells.size(), 1 );
}


BOOST_AUTO_TEST_CASE( VetoedTargetPreventsEntireEdit )
{
    SKIP_IF_HEADLESS();

    m_grid->Bind( wxEVT_GRID_CELL_CHANGING,
                  []( wxGridEvent& aEvent )
                  {
                      if( aEvent.GetRow() == 2 )
                          aEvent.Veto();
                      else
                          aEvent.Skip();
                  } );
    m_grid->SelectBlock( 0, 1, 3, 1 );
    BOOST_CHECK( !Edit( 0, 1, wxT( "rejected" ) ) );

    for( int row = 0; row < 5; ++row )
        BOOST_CHECK_EQUAL( m_grid->GetCellValue( row, 1 ), wxString::Format( "%d,1", row ) );

    BOOST_CHECK( m_changedCells.empty() );
    m_grid->SetCellValue( 0, 1, wxT( "restored" ) );
    NotifyChanged( 0, 1, wxT( "0,1" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 1, 1 ), wxT( "1,1" ) );
}


BOOST_AUTO_TEST_CASE( ProgrammaticChangesDoNotRepeatBulkEdit )
{
    SKIP_IF_HEADLESS();

    m_grid->SelectBlock( 0, 1, 2, 1 );
    BOOST_REQUIRE( Edit( 0, 1, wxT( "edited" ) ) );

    m_grid->SetCellValue( 0, 1, wxT( "restored" ) );
    NotifyChanged( 0, 1, wxT( "edited" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 1, 1 ), wxT( "edited" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 2, 1 ), wxT( "edited" ) );
}


BOOST_AUTO_TEST_CASE( TextEditorCommitPreservesSelection )
{
    SKIP_IF_HEADLESS();

    m_grid->SetGridCursor( 0, 1 );
    m_grid->SelectBlock( 0, 1, 0, 1 );
    m_grid->SelectBlock( 3, 1, 3, 1, true );

    wxGridEvent click( m_grid->GetId(), wxEVT_GRID_CELL_LEFT_CLICK, m_grid, 3, 1 );
    m_grid->GetEventHandler()->ProcessEvent( click );
    BOOST_CHECK( m_grid->IsInSelection( 0, 1 ) );
    BOOST_CHECK( m_grid->IsInSelection( 3, 1 ) );
    m_grid->ClearSelection();
    m_grid->EnableCellEditControl();

    wxGridCellEditor* editor = m_grid->GetCellEditor( 3, 1 );
    wxTextCtrl*       control = dynamic_cast<wxTextCtrl*>( editor->GetWindow() );
    BOOST_REQUIRE( control );
    control->ChangeValue( wxT( "typed" ) );
    editor->DecRef();
    BOOST_REQUIRE( m_grid->CommitPendingChanges() );

    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 0, 1 ), wxT( "typed" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 3, 1 ), wxT( "typed" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 1, 1 ), wxT( "1,1" ) );
    BOOST_CHECK( m_grid->IsInSelection( 0, 1 ) );
    BOOST_CHECK( m_grid->IsInSelection( 3, 1 ) );
}


BOOST_AUTO_TEST_CASE( DropdownCommitUpdatesSelectedCells )
{
    SKIP_IF_HEADLESS();

    wxArrayString choices;
    choices.Add( wxT( "Input" ) );
    choices.Add( wxT( "Output" ) );

    wxGridCellAttr* attr = new wxGridCellAttr;
    attr->SetEditor( new GRID_CELL_COMBOBOX( choices ) );
    m_grid->SetColAttr( 1, attr );

    for( int row = 0; row < 5; ++row )
        m_grid->SetCellValue( row, 1, wxT( "Input" ) );

    m_grid->SetGridCursor( 0, 1 );
    m_grid->SelectBlock( 0, 1, 2, 1 );
    m_grid->EnableCellEditControl();

    wxGridCellEditor* editor = m_grid->GetCellEditor( 0, 1 );
    wxComboBox*       control = dynamic_cast<wxComboBox*>( editor->GetWindow() );
    BOOST_REQUIRE( control );
    control->SetSelection( 1 );
    editor->DecRef();
    BOOST_REQUIRE( m_grid->CommitPendingChanges() );

    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 0, 1 ), wxT( "Output" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 1, 1 ), wxT( "Output" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 2, 1 ), wxT( "Output" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 3, 1 ), wxT( "Input" ) );
}


BOOST_AUTO_TEST_CASE( NavigationCommitsToOriginalSelection )
{
    SKIP_IF_HEADLESS();

    m_grid->SetGridCursor( 0, 1 );
    m_grid->SelectBlock( 0, 1, 2, 1 );
    m_grid->EnableCellEditControl();

    wxGridCellEditor* editor = m_grid->GetCellEditor( 0, 1 );
    wxTextCtrl*       control = dynamic_cast<wxTextCtrl*>( editor->GetWindow() );
    BOOST_REQUIRE( control );
    control->ChangeValue( wxT( "typed" ) );
    editor->DecRef();
    m_grid->ClearSelection();
    BOOST_REQUIRE( m_grid->CommitPendingChanges() );

    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 0, 1 ), wxT( "typed" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 1, 1 ), wxT( "typed" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 2, 1 ), wxT( "typed" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 3, 1 ), wxT( "3,1" ) );
}


BOOST_AUTO_TEST_CASE( CheckboxSpaceAppliesOneValueOnce )
{
    SKIP_IF_HEADLESS();

    wxGridCellAttr* attr = new wxGridCellAttr;
    attr->SetRenderer( new wxGridCellBoolRenderer );
    attr->SetEditor( new wxGridCellBoolEditor );
    m_grid->SetColAttr( 1, attr );

    for( int row = 0; row < 5; ++row )
        m_grid->SetCellValue( row, 1, wxT( "0" ) );

    m_grid->SetCellValue( 2, 1, wxT( "1" ) );
    m_grid->SetGridCursor( 0, 1 );
    m_grid->SelectBlock( 0, 1, 0, 1 );
    m_grid->SelectBlock( 2, 1, 2, 1, true );
    m_grid->SelectBlock( 4, 1, 4, 1, true );

    wxKeyEvent space( wxEVT_KEY_DOWN );
    space.m_keyCode = ' ';
    m_grid->GetEventHandler()->ProcessEvent( space );

    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 0, 1 ), wxT( "1" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 2, 1 ), wxT( "1" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 4, 1 ), wxT( "1" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 1, 1 ), wxT( "0" ) );
    BOOST_CHECK_EQUAL( m_changedCells.size(), 3 );
}


BOOST_AUTO_TEST_CASE( ReselectingDropdownValueUpdatesMixedSelection )
{
    SKIP_IF_HEADLESS();

    wxArrayString choices;
    choices.Add( wxT( "Input" ) );
    choices.Add( wxT( "Output" ) );

    wxGridCellAttr* attr = new wxGridCellAttr;
    attr->SetEditor( new GRID_CELL_COMBOBOX( choices ) );
    m_grid->SetColAttr( 1, attr );
    m_grid->SetCellValue( 0, 1, wxT( "Input" ) );
    m_grid->SetCellValue( 1, 1, wxT( "Output" ) );
    m_grid->SetGridCursor( 0, 1 );
    m_grid->SelectBlock( 0, 1, 1, 1 );
    m_grid->EnableCellEditControl();

    wxGridCellEditor* editor = m_grid->GetCellEditor( 0, 1 );
    wxComboBox*       control = dynamic_cast<wxComboBox*>( editor->GetWindow() );
    BOOST_REQUIRE( control );
    control->SetSelection( 0 );

    wxCommandEvent selection( wxEVT_COMBOBOX, control->GetId() );
    selection.SetEventObject( control );
    selection.SetString( wxT( "Input" ) );
    control->GetEventHandler()->ProcessEvent( selection );
    editor->DecRef();

    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 0, 1 ), wxT( "Input" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 1, 1 ), wxT( "Input" ) );
    BOOST_CHECK( !m_grid->IsCellEditControlEnabled() );
}


BOOST_AUTO_TEST_CASE( ClosingUntouchedEditorKeepsMixedValues )
{
    SKIP_IF_HEADLESS();

    m_grid->SetGridCursor( 0, 1 );
    m_grid->SelectBlock( 0, 1, 2, 1 );
    m_grid->EnableCellEditControl();
    BOOST_REQUIRE( m_grid->CommitPendingChanges() );

    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 0, 1 ), wxT( "0,1" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 1, 1 ), wxT( "1,1" ) );
    BOOST_CHECK( m_changedCells.empty() );
}


BOOST_AUTO_TEST_CASE( RetypingOriginalValueUpdatesMixedSelection )
{
    SKIP_IF_HEADLESS();

    m_grid->SetGridCursor( 0, 1 );
    m_grid->SelectBlock( 0, 1, 2, 1 );
    m_grid->EnableCellEditControl();

    wxGridCellEditor* editor = m_grid->GetCellEditor( 0, 1 );
    wxTextCtrl*       control = dynamic_cast<wxTextCtrl*>( editor->GetWindow() );
    BOOST_REQUIRE( control );
    control->SetValue( wxT( "0,1" ) );
    editor->DecRef();
    BOOST_REQUIRE( m_grid->CommitPendingChanges() );

    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 0, 1 ), wxT( "0,1" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 1, 1 ), wxT( "0,1" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 2, 1 ), wxT( "0,1" ) );
}


BOOST_AUTO_TEST_CASE( CancellingTypedValueKeepsAllCells )
{
    SKIP_IF_HEADLESS();

    m_grid->SetGridCursor( 0, 1 );
    m_grid->SelectBlock( 0, 1, 2, 1 );
    m_grid->EnableCellEditControl();

    wxGridCellEditor* editor = m_grid->GetCellEditor( 0, 1 );
    wxTextCtrl*       control = dynamic_cast<wxTextCtrl*>( editor->GetWindow() );
    BOOST_REQUIRE( control );
    control->SetValue( wxT( "cancelled" ) );
    editor->DecRef();
    BOOST_REQUIRE( m_grid->CancelPendingChanges() );

    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 0, 1 ), wxT( "0,1" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 1, 1 ), wxT( "1,1" ) );
    BOOST_CHECK( m_changedCells.empty() );
}


BOOST_AUTO_TEST_CASE( ModifierClicksSelectWithoutEditing )
{
    SKIP_IF_HEADLESS();

    ClickCell( 0, 1 );
    BOOST_REQUIRE( m_grid->CommitPendingChanges() );
    BOOST_CHECK( m_grid->IsInSelection( 0, 1 ) );

    ClickCell( 2, 1, true );
    BOOST_CHECK( m_grid->IsInSelection( 0, 1 ) );
    BOOST_CHECK( m_grid->IsInSelection( 2, 1 ) );
    BOOST_CHECK( !m_grid->IsInSelection( 1, 1 ) );
    BOOST_CHECK( !m_grid->IsCellEditControlShown() );

    ClickCell( 2, 1, true );
    BOOST_CHECK( m_grid->IsInSelection( 0, 1 ) );
    BOOST_CHECK( !m_grid->IsInSelection( 2, 1 ) );

    ClickCell( 0, 1 );
    BOOST_REQUIRE( m_grid->CommitPendingChanges() );
    ClickCell( 4, 1, false, true );

    for( int row = 0; row < 5; ++row )
        BOOST_CHECK( m_grid->IsInSelection( row, 1 ) );

    BOOST_CHECK( !m_grid->IsCellEditControlShown() );
    BOOST_CHECK( m_changedCells.empty() );
}


BOOST_AUTO_TEST_CASE( MouseSelectionAndTextCommitUpdateSelectedCells )
{
    SKIP_IF_HEADLESS();

    ClickCell( 0, 1 );
    BOOST_REQUIRE( m_grid->CommitPendingChanges() );
    ClickCell( 2, 1, false, true );
    ClickCell( 4, 1, true );
    ClickCell( 2, 1 );

    wxGridCellEditor* editor = m_grid->GetCellEditor( 2, 1 );
    wxTextCtrl*       control = dynamic_cast<wxTextCtrl*>( editor->GetWindow() );
    BOOST_REQUIRE( control );
    control->SetValue( wxT( "selected pins" ) );
    editor->DecRef();
    BOOST_REQUIRE( m_grid->CommitPendingChanges() );

    for( int row = 0; row < 5; ++row )
    {
        const wxString expected = row == 3 ? wxString( wxT( "3,1" ) ) : wxString( wxT( "selected pins" ) );
        BOOST_CHECK_EQUAL( m_grid->GetCellValue( row, 1 ), expected );
        BOOST_CHECK_EQUAL( m_grid->GetCellValue( row, 0 ), wxString::Format( "%d,0", row ) );
    }
}


BOOST_AUTO_TEST_CASE( PinDropdownChoiceImmediatelyUpdatesMouseSelection )
{
    SKIP_IF_HEADLESS();

    wxArrayString choices;
    choices.Add( wxT( "Input" ) );
    choices.Add( wxT( "Output" ) );

    wxGridCellAttr* attr = new wxGridCellAttr;
    attr->SetEditor( new GRID_CELL_ICON_TEXT_POPUP( {}, choices ) );
    m_grid->SetColAttr( 1, attr );

    for( int row = 0; row < 5; ++row )
        m_grid->SetCellValue( row, 1, wxT( "Input" ) );

    m_grid->SetGridCursor( 0, 1 );
    m_grid->SelectBlock( 0, 1, 0, 1 );
    ClickCell( 2, 1, true );
    ClickCell( 4, 1, true );
    ClickCell( 2, 1 );

    wxGridCellEditor* editor = m_grid->GetCellEditor( 2, 1 );
    wxBitmapComboBox* control = dynamic_cast<wxBitmapComboBox*>( editor->GetWindow() );
    BOOST_REQUIRE( control );
    control->SetSelection( 1 );

    wxCommandEvent selection( wxEVT_COMBOBOX, control->GetId() );
    selection.SetEventObject( control );
    selection.SetString( wxT( "Output" ) );
    control->GetEventHandler()->ProcessEvent( selection );
    editor->DecRef();

    for( int row = 0; row < 5; ++row )
    {
        const wxString expected = row % 2 == 0 ? wxString( wxT( "Output" ) ) : wxString( wxT( "Input" ) );
        BOOST_CHECK_EQUAL( m_grid->GetCellValue( row, 1 ), expected );
    }

    BOOST_CHECK( !m_grid->IsCellEditControlEnabled() );
    BOOST_CHECK( m_grid->IsInSelection( 0, 1 ) );
    BOOST_CHECK( m_grid->IsInSelection( 2, 1 ) );
    BOOST_CHECK( m_grid->IsInSelection( 4, 1 ) );
    BOOST_CHECK_EQUAL( m_changedCells.size(), 3 );
}


BOOST_AUTO_TEST_CASE( ReselectingPinDropdownValueImmediatelyUpdatesMixedSelection )
{
    SKIP_IF_HEADLESS();

    wxArrayString choices;
    choices.Add( wxT( "Input" ) );
    choices.Add( wxT( "Output" ) );

    wxGridCellAttr* attr = new wxGridCellAttr;
    attr->SetEditor( new GRID_CELL_ICON_TEXT_POPUP( {}, choices ) );
    m_grid->SetColAttr( 1, attr );
    m_grid->SetCellValue( 0, 1, wxT( "Input" ) );
    m_grid->SetCellValue( 1, 1, wxT( "Output" ) );
    m_grid->SetCellValue( 2, 1, wxT( "Output" ) );
    m_grid->SetReadOnly( 2, 1 );
    m_grid->SetGridCursor( 0, 1 );
    m_grid->SelectBlock( 0, 1, 2, 1 );
    m_grid->EnableCellEditControl();

    wxGridCellEditor* editor = m_grid->GetCellEditor( 0, 1 );
    wxBitmapComboBox* control = dynamic_cast<wxBitmapComboBox*>( editor->GetWindow() );
    BOOST_REQUIRE( control );
    control->SetSelection( 0 );

    wxCommandEvent selection( wxEVT_COMBOBOX, control->GetId() );
    selection.SetEventObject( control );
    selection.SetString( wxT( "Input" ) );
    control->GetEventHandler()->ProcessEvent( selection );
    editor->DecRef();

    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 0, 1 ), wxT( "Input" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 1, 1 ), wxT( "Input" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 2, 1 ), wxT( "Output" ) );
    BOOST_CHECK( !m_grid->IsCellEditControlEnabled() );
    BOOST_CHECK_EQUAL( m_changedCells.size(), 2 );
}


BOOST_AUTO_TEST_CASE( DropdownArrowNavigationWaitsForEnter )
{
    SKIP_IF_HEADLESS();

    for( bool icons : { false, true } )
    {
        BOOST_TEST_CONTEXT( "Icon dropdown: " << icons )
        {
            SetChoiceColumn( icons );
            m_grid->EnableCellEditControl();
            wxComboBox* control = GetCombo();
            BOOST_REQUIRE( control );

            PressKey( control, WXK_DOWN );
            SelectChoice( control, 1 );
            BOOST_REQUIRE( m_grid->IsCellEditControlEnabled() );
            PressKey( control, WXK_DOWN );
            SelectChoice( control, 2 );
            BOOST_REQUIRE( m_grid->IsCellEditControlEnabled() );
            PressKey( control, WXK_UP );
            SelectChoice( control, 1 );

            BOOST_CHECK_EQUAL( m_grid->GetCellValue( 0, 1 ), wxT( "Input" ) );
            BOOST_CHECK_EQUAL( m_grid->GetCellValue( 1, 1 ), wxT( "Output" ) );
            BOOST_CHECK_EQUAL( m_grid->GetCellValue( 2, 1 ), wxT( "Input" ) );
            BOOST_CHECK( m_changedCells.empty() );

            PressKey( control, WXK_RETURN );
            BOOST_CHECK( !m_grid->IsCellEditControlEnabled() );

            for( int row = 0; row < 3; ++row )
                BOOST_CHECK_EQUAL( m_grid->GetCellValue( row, 1 ), wxT( "Output" ) );

            BOOST_CHECK_EQUAL( m_grid->GetCellValue( 3, 1 ), wxT( "Input" ) );
            BOOST_CHECK_EQUAL( m_changedCells.size(), 3 );
        }
    }
}


BOOST_AUTO_TEST_CASE( DropdownEscapePreservesOriginalMixedValues )
{
    SKIP_IF_HEADLESS();

    for( bool icons : { false, true } )
    {
        BOOST_TEST_CONTEXT( "Icon dropdown: " << icons )
        {
            SetChoiceColumn( icons );
            m_grid->EnableCellEditControl();
            wxComboBox* control = GetCombo();
            BOOST_REQUIRE( control );

            PressKey( control, WXK_DOWN );
            SelectChoice( control, 2 );
            BOOST_REQUIRE( m_grid->IsCellEditControlEnabled() );
            PressKey( control, WXK_ESCAPE );

            wxCommandEvent close( wxEVT_COMBOBOX_CLOSEUP, control->GetId() );
            close.SetEventObject( control );
            control->ProcessWindowEvent( close );

            BOOST_CHECK( !m_grid->IsCellEditControlEnabled() );
            BOOST_CHECK_EQUAL( m_grid->GetCellValue( 0, 1 ), wxT( "Input" ) );
            BOOST_CHECK_EQUAL( m_grid->GetCellValue( 1, 1 ), wxT( "Output" ) );
            BOOST_CHECK_EQUAL( m_grid->GetCellValue( 2, 1 ), wxT( "Input" ) );
            BOOST_CHECK( m_changedCells.empty() );
        }
    }
}


BOOST_AUTO_TEST_CASE( DropdownMouseChoiceAfterArrowNavigationCommitsOnClose )
{
    SKIP_IF_HEADLESS();

    for( bool icons : { false, true } )
    {
        SetChoiceColumn( icons );
        m_grid->EnableCellEditControl();
        wxComboBox* control = GetCombo();
        BOOST_REQUIRE( control );
        PressKey( control, WXK_DOWN );
        SelectChoice( control, 1 );
        SelectChoice( control, 2 );

        wxCommandEvent close( wxEVT_COMBOBOX_CLOSEUP, control->GetId() );
        close.SetEventObject( control );
        control->ProcessWindowEvent( close );
        wxTheApp->ProcessPendingEvents();

        BOOST_CHECK( !m_grid->IsCellEditControlEnabled() );

        for( int row = 0; row < 3; ++row )
            BOOST_CHECK_EQUAL( m_grid->GetCellValue( row, 1 ), wxT( "Bidirectional" ) );
    }
}


BOOST_AUTO_TEST_CASE( DropdownCloseBeforeSelectionCommitsUpdatedValue )
{
    SKIP_IF_HEADLESS();

    for( bool icons : { false, true } )
    {
        for( bool multiple : { false, true } )
        {
            BOOST_TEST_CONTEXT( "Icon dropdown: " << icons << ", multiple cells: " << multiple )
            {
                SetChoiceColumn( icons );

                if( !multiple )
                    m_grid->SelectBlock( 0, 1, 0, 1 );

                m_grid->EnableCellEditControl();
                wxComboBox* control = GetCombo();
                BOOST_REQUIRE( control );

                wxCommandEvent close( wxEVT_COMBOBOX_CLOSEUP, control->GetId() );
                close.SetEventObject( control );
                control->ProcessWindowEvent( close );
                BOOST_CHECK( m_grid->IsCellEditControlEnabled() );

                SelectChoice( control, 2 );
                wxTheApp->ProcessPendingEvents();

                BOOST_CHECK( !m_grid->IsCellEditControlEnabled() );

                for( int row = 0; row < 5; ++row )
                {
                    const wxString expected = row == 0 || ( multiple && row < 3 ) ? wxT( "Bidirectional" )
                                              : row == 1                          ? wxT( "Output" )
                                                                                  : wxT( "Input" );
                    BOOST_CHECK_EQUAL( m_grid->GetCellValue( row, 1 ), expected );
                }
            }
        }
    }
}


BOOST_AUTO_TEST_CASE( DeferredDropdownCloseDoesNotCommitReopenedEditor )
{
    SKIP_IF_HEADLESS();

    for( bool icons : { false, true } )
    {
        SetChoiceColumn( icons );
        m_grid->EnableCellEditControl();
        wxComboBox* control = GetCombo();
        BOOST_REQUIRE( control );

        wxCommandEvent close( wxEVT_COMBOBOX_CLOSEUP, control->GetId() );
        close.SetEventObject( control );
        control->ProcessWindowEvent( close );
        m_grid->CancelPendingChanges();
        m_grid->EnableCellEditControl();
        m_tricks->ProcessPendingEvents();

        BOOST_CHECK( m_grid->IsCellEditControlEnabled() );
        BOOST_CHECK( m_changedCells.empty() );
        m_grid->CancelPendingChanges();
        wxTheApp->ProcessPendingEvents();
    }
}


BOOST_AUTO_TEST_CASE( EnterAtLastRowEditsWithoutAddingOrMoving )
{
    SKIP_IF_HEADLESS();

    m_grid->SetGridCursor( 4, 1 );
    m_grid->SelectBlock( 2, 1, 4, 1 );
    PressKey( m_grid->GetGridWindow(), WXK_RETURN );
    BOOST_REQUIRE( m_grid->IsCellEditControlEnabled() );
    BOOST_CHECK_EQUAL( m_grid->GetGridCursorRow(), 4 );
    BOOST_CHECK_EQUAL( m_grid->GetNumberRows(), 5 );

    wxGridCellEditor* editor = m_grid->GetCellEditor( 4, 1 );
    wxTextCtrl*       control = dynamic_cast<wxTextCtrl*>( editor->GetWindow() );
    BOOST_REQUIRE( control );
    BOOST_CHECK_EQUAL( control->GetValue(), wxT( "4,1" ) );
    control->SetValue( wxT( "entered" ) );
    PressKey( control, WXK_RETURN );
    editor->DecRef();

    BOOST_CHECK( !m_grid->IsCellEditControlEnabled() );
    BOOST_CHECK_EQUAL( m_grid->GetGridCursorRow(), 4 );

    for( int row = 2; row < 5; ++row )
        BOOST_CHECK_EQUAL( m_grid->GetCellValue( row, 1 ), wxT( "entered" ) );
}


BOOST_AUTO_TEST_CASE( EnterOpensBothDropdownEditors )
{
    SKIP_IF_HEADLESS();

    for( bool icons : { false, true } )
    {
        SetChoiceColumn( icons );
        PressKey( m_grid->GetGridWindow(), WXK_NUMPAD_ENTER );
        BOOST_REQUIRE( m_grid->IsCellEditControlEnabled() );
        BOOST_CHECK_EQUAL( m_grid->GetGridCursorRow(), 0 );
        wxComboBox* control = GetCombo();
        BOOST_REQUIRE( control );
        PressKey( control, WXK_DOWN );
        SelectChoice( control, 1 );
        BOOST_REQUIRE( m_grid->IsCellEditControlEnabled() );
        PressKey( control, WXK_NUMPAD_ENTER );

        BOOST_CHECK( !m_grid->IsCellEditControlEnabled() );
        BOOST_CHECK_EQUAL( m_grid->GetGridCursorRow(), 3 );
        BOOST_CHECK_EQUAL( m_grid->GetGridCursorCol(), 1 );
        BOOST_CHECK( m_grid->IsInSelection( 3, 1 ) );

        for( int row = 0; row < 3; ++row )
            BOOST_CHECK_EQUAL( m_grid->GetCellValue( row, 1 ), wxT( "Output" ) );
    }
}


BOOST_AUTO_TEST_CASE( EnterOpensDropdownWithoutWaitingForIdleAfterTextEdit )
{
    SKIP_IF_HEADLESS();

    for( bool icons : { false, true } )
    {
        SetChoiceColumn( icons );
        m_frame->Show();
        wxTheApp->Yield();
        m_grid->EnableCellEditControl();
        wxComboBox* control = GetCombo();
        BOOST_REQUIRE( control );
        m_grid->CancelPendingChanges();

        auto opened = std::make_shared<int>( 0 );
        control->GetEventHandler()->Bind( wxEVT_COMBOBOX_DROPDOWN,
                                          [opened]( wxCommandEvent& aEvent )
                                          {
                                              ++*opened;
                                              aEvent.Skip();
                                          } );

        for( int cycle = 0; cycle < 3; ++cycle )
        {
            m_grid->SetGridCursor( 0, 0 );
            m_grid->SelectBlock( 0, 0, 0, 0 );
            PressKey( m_grid->GetGridWindow(), WXK_RETURN, wxEVT_CHAR_HOOK );
            BOOST_REQUIRE( m_grid->IsCellEditControlEnabled() );
            wxGridCellEditor* editor = m_grid->GetCellEditor( 0, 0 );
            wxTextCtrl*       text = dynamic_cast<wxTextCtrl*>( editor->GetWindow() );
            BOOST_REQUIRE( text );
            text->SetValue( wxString::Format( "Pin %d", cycle ) );
            PressKey( text, WXK_RETURN, wxEVT_CHAR_HOOK );
            editor->DecRef();
            BOOST_REQUIRE( !m_grid->IsCellEditControlEnabled() );

            m_grid->SetGridCursor( 0, 1 );
            m_grid->SelectBlock( 0, 1, 0, 1 );
            PressKey( m_grid->GetGridWindow(), WXK_RETURN, wxEVT_CHAR_HOOK );
            BOOST_REQUIRE( m_grid->IsCellEditControlEnabled() );
            BOOST_CHECK_EQUAL( *opened, cycle + 1 );
            m_grid->CancelPendingChanges();
            wxTheApp->ProcessPendingEvents();
        }
    }
}


BOOST_AUTO_TEST_CASE( EnterMovesBelowContiguousSelectionContainingCursor )
{
    SKIP_IF_HEADLESS();

    m_grid->SetGridCursor( 1, 1 );
    m_grid->SelectBlock( 0, 1, 2, 1 );
    m_grid->SelectBlock( 4, 1, 4, 1, true );
    PressKey( m_grid->GetGridWindow(), WXK_RETURN );
    BOOST_REQUIRE( m_grid->IsCellEditControlEnabled() );

    wxGridCellEditor* editor = m_grid->GetCellEditor( 1, 1 );
    wxTextCtrl*       control = dynamic_cast<wxTextCtrl*>( editor->GetWindow() );
    BOOST_REQUIRE( control );
    control->SetValue( wxT( "entered" ) );
    PressKey( control, WXK_RETURN );
    editor->DecRef();

    BOOST_CHECK( !m_grid->IsCellEditControlEnabled() );
    BOOST_CHECK_EQUAL( m_grid->GetGridCursorRow(), 3 );
    BOOST_CHECK_EQUAL( m_grid->GetGridCursorCol(), 1 );

    for( int row = 0; row < 5; ++row )
    {
        BOOST_CHECK_EQUAL( m_grid->GetCellValue( row, 1 ), row == 3 ? wxT( "3,1" ) : wxT( "entered" ) );
        BOOST_CHECK_EQUAL( m_grid->IsInSelection( row, 1 ), row == 3 );
    }
}


BOOST_AUTO_TEST_CASE( EnterMovesDownAfterSingleCellEditAndSkipsHiddenRows )
{
    SKIP_IF_HEADLESS();

    m_grid->SetGridCursor( 1, 1 );
    m_grid->HideRow( 2 );
    m_grid->EnableCellEditControl();
    wxGridCellEditor* editor = m_grid->GetCellEditor( 1, 1 );
    wxTextCtrl*       control = dynamic_cast<wxTextCtrl*>( editor->GetWindow() );
    BOOST_REQUIRE( control );
    control->SetValue( wxT( "entered" ) );
    PressKey( control, WXK_RETURN, wxEVT_CHAR_HOOK );
    editor->DecRef();

    BOOST_CHECK( !m_grid->IsCellEditControlEnabled() );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 1, 1 ), wxT( "entered" ) );
    BOOST_CHECK_EQUAL( m_grid->GetGridCursorRow(), 3 );
    BOOST_CHECK_EQUAL( m_grid->GetGridCursorCol(), 1 );
    BOOST_CHECK( m_grid->IsInSelection( 3, 1 ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 3, 1 ), wxT( "3,1" ) );
}


BOOST_AUTO_TEST_CASE( EnterAllowsApplyingOriginalTextToMixedSelection )
{
    SKIP_IF_HEADLESS();

    m_grid->SetCellValue( 0, 1, wxT( "7" ) );
    m_grid->SetGridCursor( 0, 1 );
    m_grid->SelectBlock( 0, 1, 2, 1 );
    PressKey( m_grid->GetGridWindow(), WXK_RETURN );
    BOOST_REQUIRE( m_grid->IsCellEditControlEnabled() );
    wxGridCellEditor* editor = m_grid->GetCellEditor( 0, 1 );
    wxTextCtrl*       control = dynamic_cast<wxTextCtrl*>( editor->GetWindow() );
    BOOST_REQUIRE( control );
    control->SetValue( wxT( "7" ) );
    PressKey( control, WXK_RETURN );
    editor->DecRef();

    for( int row = 0; row < 3; ++row )
        BOOST_CHECK_EQUAL( m_grid->GetCellValue( row, 1 ), wxT( "7" ) );

    BOOST_CHECK_EQUAL( m_grid->GetGridCursorRow(), 3 );
    BOOST_CHECK_EQUAL( m_changedCells.size(), 3 );
}


BOOST_AUTO_TEST_CASE( EnterDoesNotMoveWhenBulkEditIsVetoed )
{
    SKIP_IF_HEADLESS();

    m_grid->SetGridCursor( 1, 1 );
    m_grid->SelectBlock( 0, 1, 2, 1 );
    m_grid->Bind( wxEVT_GRID_CELL_CHANGING,
                  []( wxGridEvent& aEvent )
                  {
                      if( aEvent.GetRow() == 2 )
                          aEvent.Veto();
                      else
                          aEvent.Skip();
                  } );

    PressKey( m_grid->GetGridWindow(), WXK_RETURN );
    BOOST_REQUIRE( m_grid->IsCellEditControlEnabled() );
    wxGridCellEditor* editor = m_grid->GetCellEditor( 1, 1 );
    wxTextCtrl*       control = dynamic_cast<wxTextCtrl*>( editor->GetWindow() );
    BOOST_REQUIRE( control );
    control->SetValue( wxT( "entered" ) );
    PressKey( control, WXK_RETURN );
    editor->DecRef();

    BOOST_CHECK_EQUAL( m_grid->GetGridCursorRow(), 1 );
    BOOST_CHECK_EQUAL( m_grid->GetGridCursorCol(), 1 );
    BOOST_CHECK( m_changedCells.empty() );

    for( int row = 0; row < 5; ++row )
        BOOST_CHECK_EQUAL( m_grid->GetCellValue( row, 1 ), wxString::Format( "%d,1", row ) );
}


#ifdef __WXMSW__
BOOST_AUTO_TEST_CASE( NativeDropdownKeyboardOpenAcceptAndCancel )
{
    SKIP_IF_HEADLESS();

    wxGUIEventLoop       loop;
    wxEventLoopActivator activeLoop( &loop );

    DIALOG_SHIM* dialog =
            new DIALOG_SHIM( nullptr, wxID_ANY, wxT( "Grid dialog test" ), wxDefaultPosition, wxSize( 640, 400 ) );
    m_grid->Reparent( dialog );
    m_frame->Destroy();
    m_frame = dialog;
    wxButton* accept = new wxButton( dialog, wxID_OK, wxT( "OK" ), wxPoint( 495, 355 ) );
    accept->SetDefault();
    auto acceptCount = std::make_shared<int>( 0 );
    dialog->Bind(
            wxEVT_BUTTON,
            [dialog, acceptCount, grid = m_grid]( wxCommandEvent& )
            {
                grid->CommitPendingChanges();
                ++*acceptCount;
                dialog->Hide();
            },
            wxID_OK );

    const auto postKey = []( HWND aWindow, int aKey )
    {
        LPARAM keyData = 1 | ( ::MapVirtualKey( aKey, MAPVK_VK_TO_VSC ) << 16 );

        if( aKey == VK_DOWN || aKey == VK_UP )
            keyData |= 1 << 24;

        BOOST_REQUIRE( ::PostMessage( aWindow, WM_KEYDOWN, aKey, keyData ) );
        BOOST_REQUIRE( ::PostMessage( aWindow, WM_KEYUP, aKey, keyData | 0xc0000000 ) );
        wxTheApp->Yield();
    };

    for( bool icons : { false, true } )
    {
        BOOST_TEST_CONTEXT( "Icon dropdown: " << icons )
        {
            *acceptCount = 0;
            SetChoiceColumn( icons );
            m_frame->Show();
            m_grid->GetGridWindow()->SetFocus();
            wxTheApp->Yield();
            const HWND window = static_cast<HWND>( m_grid->GetGridWindow()->GetHandle() );
            postKey( window, VK_DOWN );
            BOOST_REQUIRE_EQUAL( m_grid->GetGridCursorRow(), 1 );
            m_grid->SelectBlock( 0, 1, 2, 1 );
            postKey( window, VK_RETURN );

            BOOST_REQUIRE( m_grid->IsCellEditControlEnabled() );
            wxComboBox* control = GetCombo();
            BOOST_REQUIRE( control );
            const HWND comboWindow = static_cast<HWND>( control->GetHandle() );
            BOOST_REQUIRE( ::SendMessage( comboWindow, CB_GETDROPPEDSTATE, 0, 0 ) );
            BOOST_CHECK_EQUAL( m_grid->GetCellValue( 0, 1 ), wxT( "Input" ) );
            BOOST_CHECK_EQUAL( m_grid->GetCellValue( 1, 1 ), wxT( "Output" ) );
            BOOST_CHECK_EQUAL( m_grid->GetCellValue( 2, 1 ), wxT( "Input" ) );
            BOOST_CHECK( m_changedCells.empty() );
            BOOST_REQUIRE( control->IsDescendant( wxWindow::FindFocus() ) );
            postKey( ::GetFocus(), VK_DOWN );
            BOOST_REQUIRE( m_grid->IsCellEditControlEnabled() );
            BOOST_REQUIRE( ::SendMessage( comboWindow, CB_GETDROPPEDSTATE, 0, 0 ) );
            BOOST_CHECK_EQUAL( control->GetCurrentSelection(), 2 );
            BOOST_CHECK( m_changedCells.empty() );
            postKey( ::GetFocus(), VK_RETURN );

            BOOST_CHECK( dialog->IsShown() );
            BOOST_CHECK_EQUAL( *acceptCount, 0 );
            BOOST_CHECK( !m_grid->IsCellEditControlEnabled() );
            BOOST_CHECK_EQUAL( m_grid->GetGridCursorRow(), 3 );
            BOOST_CHECK_EQUAL( m_grid->GetGridCursorCol(), 1 );
            BOOST_CHECK( m_grid->IsInSelection( 3, 1 ) );

            for( int row = 0; row < 3; ++row )
                BOOST_CHECK_EQUAL( m_grid->GetCellValue( row, 1 ), wxT( "Bidirectional" ) );

            BOOST_CHECK_EQUAL( m_changedCells.size(), 3 );
            PressKey( m_grid->GetGridWindow(), WXK_RETURN, wxEVT_CHAR_HOOK );
            wxTheApp->Yield();
            BOOST_REQUIRE( m_grid->IsCellEditControlEnabled() );
            control = GetCombo();
            BOOST_REQUIRE( control );
            BOOST_REQUIRE( control->IsDescendant( wxWindow::FindFocus() ) );
            postKey( ::GetFocus(), VK_DOWN );
            BOOST_REQUIRE( m_grid->IsCellEditControlEnabled() );
            postKey( ::GetFocus(), VK_ESCAPE );

            BOOST_CHECK( !m_grid->IsCellEditControlEnabled() );
            BOOST_CHECK_EQUAL( m_grid->GetGridCursorRow(), 3 );
            BOOST_CHECK_EQUAL( m_grid->GetCellValue( 3, 1 ), wxT( "Input" ) );
            BOOST_CHECK_EQUAL( m_changedCells.size(), 3 );
            BOOST_CHECK( dialog->IsShown() );

            if( !icons )
            {
                PressKey( m_grid->GetGridWindow(), WXK_RETURN, wxEVT_CHAR_HOOK );
                wxTheApp->Yield();
                BOOST_REQUIRE( m_grid->IsCellEditControlEnabled() );
                control = GetCombo();
                BOOST_REQUIRE( control );
                BOOST_REQUIRE( control->IsDescendant( wxWindow::FindFocus() ) );
                postKey( ::GetFocus(), VK_DOWN );
                BOOST_REQUIRE( m_grid->IsCellEditControlEnabled() );

                {
                    std::array<BYTE, 256> keyboardState;
                    BOOST_REQUIRE( ::GetKeyboardState( keyboardState.data() ) );
                    wxON_BLOCK_EXIT1( ::SetKeyboardState, keyboardState.data() );
                    auto shiftedState = keyboardState;
                    shiftedState[VK_SHIFT] |= 0x80;
                    shiftedState[VK_LSHIFT] |= 0x80;
                    BOOST_REQUIRE( ::SetKeyboardState( shiftedState.data() ) );
                    postKey( ::GetFocus(), VK_RETURN );
                }

                BOOST_CHECK( !dialog->IsShown() );
                BOOST_CHECK_EQUAL( *acceptCount, 1 );
            }
        }
    }
}
#endif


BOOST_AUTO_TEST_CASE( TypingPreservesFirstCharacterInPadTextEditor )
{
    SKIP_IF_HEADLESS();

    wxGridCellAttr* attr = new wxGridCellAttr;
    attr->SetEditor( new GRID_CELL_TEXT_EDITOR );
    m_grid->SetColAttr( 1, attr );
    m_grid->SetGridCursor( 0, 1 );
    m_grid->SelectBlock( 0, 1, 2, 1 );
    PressKey( m_grid->GetGridWindow(), '7', wxEVT_CHAR );
    BOOST_REQUIRE( m_grid->IsCellEditControlEnabled() );

    wxGridCellEditor* editor = m_grid->GetCellEditor( 0, 1 );
    wxTextCtrl*       control = dynamic_cast<wxTextCtrl*>( editor->GetWindow() );
    BOOST_REQUIRE( control );
    BOOST_CHECK_EQUAL( control->GetValue(), wxT( "7" ) );
    control->WriteText( wxT( "5" ) );
    PressKey( control, WXK_RETURN );
    editor->DecRef();

    for( int row = 0; row < 3; ++row )
        BOOST_CHECK_EQUAL( m_grid->GetCellValue( row, 1 ), wxT( "75" ) );
}


BOOST_AUTO_TEST_CASE( BulkNumericEditsEvaluateAndFormatEverySelectedCell )
{
    SKIP_IF_HEADLESS();

    m_unitsProvider = std::make_unique<UNITS_PROVIDER>( pcbIUScale, EDA_UNITS::MM );
    m_grid->SetUnitsProvider( m_unitsProvider.get(), 1 );
    m_grid->SetAutoEvalCols( { 1 } );
    wxGridCellAttr* attr = new wxGridCellAttr;
    attr->SetEditor( new GRID_CELL_TEXT_EDITOR );
    m_grid->SetColAttr( 1, attr );

    for( int row = 0; row < 5; ++row )
        m_grid->SetCellValue( row, 1, wxT( "1 mm" ) );

    m_grid->SetReadOnly( 1, 1 );
    m_grid->SetGridCursor( 0, 1 );
    m_grid->SelectBlock( 0, 1, 2, 1 );
    m_grid->EnableCellEditControl();
    wxGridCellEditor* editor = m_grid->GetCellEditor( 0, 1 );
    wxTextCtrl*       control = dynamic_cast<wxTextCtrl*>( editor->GetWindow() );
    BOOST_REQUIRE( control );
    control->SetValue( wxT( "2+3" ) );
    PressKey( control, WXK_RETURN );
    editor->DecRef();
    wxTheApp->ProcessPendingEvents();

    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 0, 1 ), wxT( "5 mm" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 1, 1 ), wxT( "1 mm" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 2, 1 ), wxT( "5 mm" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 3, 1 ), wxT( "1 mm" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 4, 1 ), wxT( "1 mm" ) );
}


BOOST_AUTO_TEST_CASE( BulkEditingRequiresOptIn )
{
    SKIP_IF_HEADLESS();

    m_tricks->EnableColumnBulkEdit( false );
    m_grid->SelectBlock( 0, 1, 2, 1 );
    BOOST_REQUIRE( Edit( 0, 1, wxT( "single" ) ) );

    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 0, 1 ), wxT( "single" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 1, 1 ), wxT( "1,1" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 2, 1 ), wxT( "2,1" ) );
}


BOOST_AUTO_TEST_CASE( NativeSelectionPreservesEarlierSelectionHandlers )
{
    SKIP_IF_HEADLESS();

    int     selectionEvents = 0;
    WX_GRID grid( m_frame, wxID_ANY );
    grid.CreateGrid( 0, 3 );
    grid.Bind( wxEVT_GRID_SELECT_CELL,
               [&]( wxGridEvent& aEvent )
               {
                   ++selectionEvents;
                   aEvent.Skip();
               } );
    grid.SetTable( new wxGridStringTable( 5, 3 ), true );
    grid.SetUseNativeSelection( true );
    selectionEvents = 0;
    grid.SetGridCursor( 1, 1 );
    BOOST_CHECK_EQUAL( selectionEvents, 1 );

    grid.SelectBlock( 0, 1, 0, 1 );
    grid.SelectBlock( 2, 1, 2, 1, true );
    grid.SetGridCursor( 2, 1 );
    BOOST_CHECK_EQUAL( selectionEvents, 2 );
    BOOST_CHECK( grid.IsInSelection( 0, 1 ) );
    BOOST_CHECK( grid.IsInSelection( 2, 1 ) );
}


BOOST_AUTO_TEST_CASE( ChangedEventVetoRestoresEverySelectedCell )
{
    SKIP_IF_HEADLESS();

    class VETO_HANDLER : public wxEvtHandler
    {
    public:
        void OnChanged( wxGridEvent& aEvent )
        {
            if( aEvent.GetRow() == Row )
                aEvent.Veto();
            else
                aEvent.Skip();
        }

        int Row = 0;
    } validator;

    m_grid->Bind( wxEVT_GRID_CELL_CHANGED, &VETO_HANDLER::OnChanged, &validator );

    for( int vetoRow : { 0, 1 } )
    {
        BOOST_TEST_CONTEXT( "Veto row: " << vetoRow )
        {
            validator.Row = vetoRow;

            for( int row = 0; row < 5; ++row )
                m_grid->SetCellValue( row, 1, wxString::Format( "%d,1", row ) );

            m_grid->SetGridCursor( 0, 1 );
            m_grid->SelectBlock( 0, 1, 2, 1 );
            m_grid->EnableCellEditControl();
            wxGridCellEditor* editor = m_grid->GetCellEditor( 0, 1 );
            wxTextCtrl*       control = dynamic_cast<wxTextCtrl*>( editor->GetWindow() );
            BOOST_REQUIRE( control );
            control->SetValue( wxT( "rejected" ) );
            BOOST_CHECK( !m_grid->CommitPendingChanges() );
            editor->DecRef();

            for( int row = 0; row < 5; ++row )
                BOOST_CHECK_EQUAL( m_grid->GetCellValue( row, 1 ), wxString::Format( "%d,1", row ) );
        }
    }
}


BOOST_AUTO_TEST_CASE( DisjointSelectionDeleteAndPastePreserveGaps )
{
    SKIP_IF_HEADLESS();

    class PASTE_TRICKS : public GRID_TRICKS
    {
    public:
        using GRID_TRICKS::GRID_TRICKS;

        void Paste( const wxString& aText )
        {
            getSelectedArea();
            paste_text( aText );
        }
    };

    m_grid->PopEventHandler( true );
    PASTE_TRICKS* tricks = new PASTE_TRICKS( m_grid );
    m_tricks = tricks;
    tricks->EnableColumnBulkEdit();
    m_grid->PushEventHandler( tricks );

    m_grid->SetGridCursor( 0, 1 );
    m_grid->SelectBlock( 0, 1, 0, 1 );
    m_grid->SelectBlock( 2, 1, 2, 1, true );
    PressKey( m_grid, WXK_DELETE );
    BOOST_CHECK( m_grid->GetCellValue( 0, 1 ).empty() );
    BOOST_CHECK( m_grid->GetCellValue( 2, 1 ).empty() );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 1, 1 ), wxT( "1,1" ) );

    tricks->Paste( wxT( "pasted" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 0, 1 ), wxT( "pasted" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 2, 1 ), wxT( "pasted" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 1, 1 ), wxT( "1,1" ) );

    tricks->Paste( wxT( "first\nskipped\nlast" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 0, 1 ), wxT( "first" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 2, 1 ), wxT( "last" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 1, 1 ), wxT( "1,1" ) );

    PressKey( m_grid, WXK_DELETE );
    BOOST_CHECK( m_grid->GetCellValue( 0, 1 ).empty() );
    BOOST_CHECK( m_grid->GetCellValue( 2, 1 ).empty() );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 1, 1 ), wxT( "1,1" ) );
}


BOOST_AUTO_TEST_CASE( SelectedRowIndicesIncludeEveryBlockAndWholeColumn )
{
    SKIP_IF_HEADLESS();

    m_grid->SetGridCursor( 0, 1 );
    m_grid->SelectBlock( 0, 1, 0, 1 );
    m_grid->SelectBlock( 2, 2, 3, 2, true );
    const std::vector<int> expected = { 0, 2, 3 };
    std::vector<int>       rows = m_grid->GetSelectedRowIndices();
    BOOST_CHECK_EQUAL_COLLECTIONS( rows.begin(), rows.end(), expected.begin(), expected.end() );

    m_grid->ClearSelection();
    m_grid->SelectCol( 1 );
    rows = m_grid->GetSelectedRowIndices();
    BOOST_REQUIRE_EQUAL( rows.size(), 5 );
    BOOST_CHECK_EQUAL( rows.front(), 0 );
    BOOST_CHECK_EQUAL( rows.back(), 4 );

    m_grid->ClearSelection();
    rows = m_grid->GetSelectedRowIndices();
    BOOST_REQUIRE_EQUAL( rows.size(), 1 );
    BOOST_CHECK_EQUAL( rows.front(), 0 );
}


BOOST_AUTO_TEST_CASE( WholeRowSelectionSurvivesOpeningAnEditor )
{
    SKIP_IF_HEADLESS();

    m_grid->SetSelectionMode( wxGrid::wxGridSelectRows );
    m_grid->SelectRow( 1 );
    m_grid->SelectRow( 3, true );
    ClickCell( 3, 1 );
    BOOST_REQUIRE( m_grid->IsCellEditControlEnabled() );
    wxGridCellEditor* editor = m_grid->GetCellEditor( 3, 1 );
    wxTextCtrl*       control = dynamic_cast<wxTextCtrl*>( editor->GetWindow() );
    BOOST_REQUIRE( control );
    control->SetValue( wxT( "selected rows" ) );
    editor->DecRef();
    BOOST_REQUIRE( m_grid->CommitPendingChanges() );

    for( int row = 0; row < 5; ++row )
    {
        const wxString expected =
                row == 1 || row == 3 ? wxString( wxT( "selected rows" ) ) : wxString::Format( "%d,1", row );
        BOOST_CHECK_EQUAL( m_grid->GetCellValue( row, 1 ), expected );
        BOOST_CHECK_EQUAL( m_grid->GetCellValue( row, 0 ), wxString::Format( "%d,0", row ) );
    }
}


BOOST_AUTO_TEST_CASE( LibraryTableKeyboardOverrideRunsOnce )
{
    SKIP_IF_HEADLESS();

    class COUNTING_LIB_TRICKS : public LIB_TABLE_GRID_TRICKS
    {
    public:
        explicit COUNTING_LIB_TRICKS( WX_GRID* aGrid ) :
                LIB_TABLE_GRID_TRICKS( aGrid )
        {
        }

        int Calls = 0;

    protected:
        void onCharHook( wxKeyEvent& aEvent ) override
        {
            ++Calls;
            LIB_TABLE_GRID_TRICKS::onCharHook( aEvent );
        }

        void     optionsEditor( int ) override {}
        void     openTable( const LIBRARY_TABLE_ROW& ) override {}
        bool     supportsVisibilityColumn() override { return false; }
        wxString getTablePreamble() override { return wxT( "(test_lib_table" ); }
    };

    m_tricks->EnableColumnBulkEdit( false );
    m_grid->PopEventHandler( true );
    COUNTING_LIB_TRICKS* tricks = new COUNTING_LIB_TRICKS( m_grid );
    m_tricks = tricks;
    m_grid->PushEventHandler( m_tricks );
    PressKey( m_grid, WXK_F1, wxEVT_CHAR_HOOK );
    BOOST_CHECK_EQUAL( tricks->Calls, 1 );
}


BOOST_AUTO_TEST_CASE( ContextMenuCommitsTheBulkEditBeforeOpening )
{
    SKIP_IF_HEADLESS();

    class POPUP_TRICKS : public GRID_TRICKS
    {
    public:
        explicit POPUP_TRICKS( WX_GRID* aGrid ) :
                GRID_TRICKS( aGrid )
        {
        }
        bool Shown = false;

    protected:
        void showPopupMenu( wxMenu&, wxGridEvent& ) override { Shown = true; }
    };

    m_grid->PopEventHandler( true );
    POPUP_TRICKS* tricks = new POPUP_TRICKS( m_grid );
    m_tricks = tricks;
    m_tricks->EnableColumnBulkEdit();
    m_grid->PushEventHandler( m_tricks );
    m_grid->SetGridCursor( 0, 1 );
    m_grid->SelectBlock( 0, 1, 2, 1 );
    m_grid->EnableCellEditControl();
    wxGridCellEditor* editor = m_grid->GetCellEditor( 0, 1 );
    wxTextCtrl*       control = dynamic_cast<wxTextCtrl*>( editor->GetWindow() );
    BOOST_REQUIRE( control );
    control->SetValue( wxT( "before popup" ) );
    editor->DecRef();
    wxGridEvent click( m_grid->GetId(), wxEVT_GRID_CELL_RIGHT_CLICK, m_grid, 0, 1 );
    m_grid->GetEventHandler()->ProcessEvent( click );
    BOOST_CHECK( tricks->Shown );

    for( int row = 0; row < 3; ++row )
        BOOST_CHECK_EQUAL( m_grid->GetCellValue( row, 1 ), wxT( "before popup" ) );

    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 3, 1 ), wxT( "3,1" ) );

    m_grid->Bind( wxEVT_GRID_CELL_CHANGING,
                  []( wxGridEvent& aEvent )
                  {
                      if( aEvent.GetRow() == 1 )
                          aEvent.Veto();
                      else
                          aEvent.Skip();
                  } );
    tricks->Shown = false;
    m_grid->EnableCellEditControl();
    editor = m_grid->GetCellEditor( 0, 1 );
    control = dynamic_cast<wxTextCtrl*>( editor->GetWindow() );
    BOOST_REQUIRE( control );
    control->SetValue( wxT( "rejected" ) );
    editor->DecRef();
    m_grid->GetEventHandler()->ProcessEvent( click );
    BOOST_CHECK( !tricks->Shown );

    for( int row = 0; row < 3; ++row )
        BOOST_CHECK_EQUAL( m_grid->GetCellValue( row, 1 ), wxT( "before popup" ) );

    wxGridEvent outside( m_grid->GetId(), wxEVT_GRID_CELL_RIGHT_CLICK, m_grid, 4, 1 );
    m_grid->GetEventHandler()->ProcessEvent( outside );
    BOOST_CHECK( tricks->Shown );
    BOOST_CHECK( m_grid->IsInSelection( 4, 1 ) );
    BOOST_CHECK( !m_grid->IsInSelection( 0, 1 ) );
    BOOST_CHECK( !m_grid->IsInSelection( 2, 1 ) );
}


BOOST_AUTO_TEST_CASE( DisjointSelectedRowsAreDeletedInReverseOrder )
{
    SKIP_IF_HEADLESS();

    m_grid->SetGridCursor( 0, 1 );
    m_grid->SelectBlock( 0, 1, 0, 1 );
    m_grid->SelectBlock( 2, 2, 3, 2, true );
    std::vector<int> removed;
    m_grid->OnDeleteRows(
            [&]( int aRow )
            {
                removed.push_back( aRow );
                m_grid->DeleteRows( aRow );
            } );
    const std::vector<int> expected = { 3, 2, 0 };
    BOOST_CHECK_EQUAL_COLLECTIONS( removed.begin(), removed.end(), expected.begin(), expected.end() );
    BOOST_REQUIRE_EQUAL( m_grid->GetNumberRows(), 2 );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 0, 1 ), wxT( "1,1" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 1, 1 ), wxT( "4,1" ) );
}


BOOST_AUTO_TEST_CASE( DisabledBulkEditRetainsMultiColumnCheckboxToggling )
{
    SKIP_IF_HEADLESS();

    m_tricks->EnableColumnBulkEdit( false );

    for( int col : { 0, 1 } )
    {
        wxGridCellAttr* attr = new wxGridCellAttr;
        attr->SetRenderer( new wxGridCellBoolRenderer );
        attr->SetEditor( new wxGridCellBoolEditor );
        m_grid->SetColAttr( col, attr );

        for( int row = 0; row < 5; ++row )
            m_grid->SetCellValue( row, col, wxT( "0" ) );
    }

    m_grid->SetGridCursor( 0, 0 );
    m_grid->SelectBlock( 0, 0, 1, 1 );
    PressKey( m_grid, ' ' );

    for( int row = 0; row < 2; ++row )
    {
        for( int col = 0; col < 2; ++col )
            BOOST_CHECK_EQUAL( m_grid->GetCellValue( row, col ), wxT( "1" ) );
    }

    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 2, 0 ), wxT( "0" ) );
    BOOST_CHECK_EQUAL( m_grid->GetCellValue( 2, 1 ), wxT( "0" ) );
}


BOOST_AUTO_TEST_CASE( DestroyingTricksDisconnectsLiveEditorControls )
{
    SKIP_IF_HEADLESS();

    SetChoiceColumn( false );
    m_grid->EnableCellEditControl();
    wxComboBox* control = GetCombo();
    BOOST_REQUIRE( control );
    m_grid->CancelPendingChanges();
    m_grid->PopEventHandler( true );
    m_tricks = new GRID_TRICKS( m_grid );
    m_grid->PushEventHandler( m_tricks );
    m_changedCells.clear();
    SelectChoice( control, 2 );
    BOOST_CHECK( m_changedCells.empty() );
}


BOOST_AUTO_TEST_SUITE_END()
