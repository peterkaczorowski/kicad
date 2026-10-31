/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <boost/test/unit_test.hpp>
#include <tool/tool_manager.h>
#include <tool/tool_interactive.h>
#include <tools/pcb_actions.h>
#include <pcbnew_utils/board_test_utils.h>
#include <board.h>
#include <netinfo.h>
#include <pcb_painter.h>
#include "../../../pcbnew/net_visibility_helpers.h"


/**
 * Test tool that handles showNetInRatsnest / hideNetInRatsnest actions
 * and modifies a PCB_RENDER_SETTINGS directly, without needing a frame.
 */
class TEST_NET_VISIBILITY_TOOL : public TOOL_INTERACTIVE
{
public:
    TEST_NET_VISIBILITY_TOOL( KIGFX::PCB_RENDER_SETTINGS* aRenderSettings ) :
        TOOL_INTERACTIVE( "test.NetVisibilityTool" ),
        m_renderSettings( aRenderSettings )
    {
    }

    bool Init() override
    {
        return true;
    }

    void Reset( RESET_REASON aReason ) override {}

    int ShowNet( const TOOL_EVENT& aEvent )
    {
        m_renderSettings->GetHiddenNets().erase( aEvent.Parameter<int>() );
        return 0;
    }

    int HideNet( const TOOL_EVENT& aEvent )
    {
        m_renderSettings->GetHiddenNets().insert( aEvent.Parameter<int>() );
        return 0;
    }

    void setTransitions() override
    {
        Go( &TEST_NET_VISIBILITY_TOOL::ShowNet, PCB_ACTIONS::showNetInRatsnest.MakeEvent() );
        Go( &TEST_NET_VISIBILITY_TOOL::HideNet, PCB_ACTIONS::hideNetInRatsnest.MakeEvent() );
    }

private:
    KIGFX::PCB_RENDER_SETTINGS* m_renderSettings;
};


/**
 * Test fixture: BOARD with nets + TOOL_MANAGER with our test tool registered.
 * The test tool modifies renderSettings.GetHiddenNets() directly.
 */
struct NET_VISIBILITY_HELPERS_FIXTURE
{
    NET_VISIBILITY_HELPERS_FIXTURE()
    {
        // Create a board with 3 nets
        board.Add( new NETINFO_ITEM( &board, wxT( "Net1" ), 1 ) );
        board.Add( new NETINFO_ITEM( &board, wxT( "Net2" ), 2 ) );
        board.Add( new NETINFO_ITEM( &board, wxT( "Net3" ), 3 ) );

        // Wire tool manager to board
        toolManager.SetEnvironment( &board, nullptr, nullptr, nullptr, nullptr );

        // Register our test tool that handles the ratsnest actions
        testTool = new TEST_NET_VISIBILITY_TOOL( &renderSettings );
        toolManager.RegisterTool( testTool );

        // Bind actions to our tool's handlers
        toolManager.InitTools();
    }

    ~NET_VISIBILITY_HELPERS_FIXTURE()
    {
        // TOOL_MANAGER takes ownership of registered tools
    }

    // Helper: is net code hidden in render settings?
    bool IsHidden( int aNetCode ) const
    {
        return renderSettings.GetHiddenNets().count( aNetCode ) > 0;
    }

    BOARD                           board;
    KIGFX::PCB_RENDER_SETTINGS      renderSettings;
    TOOL_MANAGER                    toolManager;
    TEST_NET_VISIBILITY_TOOL*       testTool = nullptr;
};


BOOST_FIXTURE_TEST_SUITE( NetVisibilityHelpers, NET_VISIBILITY_HELPERS_FIXTURE )


// ============================================================================
// HideSingleNet: hide net 2, verify it's in the hidden set
// ============================================================================
BOOST_AUTO_TEST_CASE( HideSingleNet )
{
    // Initially nothing hidden
    BOOST_CHECK( !IsHidden( 2 ) );

    // Hide net 2 via the helper
    NET_VISIBILITY_HELPERS::HideNets( &toolManager, { 2 } );

    // Verify net 2 is now hidden, others are not
    BOOST_CHECK( IsHidden( 2 ) );
    BOOST_CHECK( !IsHidden( 1 ) );
    BOOST_CHECK( !IsHidden( 3 ) );
}


// ============================================================================
// ShowSingleNet: hide net 1, then show it, verify it's removed from hidden set
// ============================================================================
BOOST_AUTO_TEST_CASE( ShowSingleNet )
{
    // Hide net 1 first
    NET_VISIBILITY_HELPERS::HideNets( &toolManager, { 1 } );
    BOOST_CHECK( IsHidden( 1 ) );

    // Now show it
    NET_VISIBILITY_HELPERS::ShowNets( &toolManager, { 1 } );

    // Verify net 1 is no longer hidden
    BOOST_CHECK( !IsHidden( 1 ) );
}


// ============================================================================
// HideMultipleNets: hide nets 1 and 3, verify both hidden, net 2 visible
// ============================================================================
BOOST_AUTO_TEST_CASE( HideMultipleNets )
{
    NET_VISIBILITY_HELPERS::HideNets( &toolManager, { 1, 3 } );

    BOOST_CHECK( IsHidden( 1 ) );
    BOOST_CHECK( !IsHidden( 2 ) );
    BOOST_CHECK( IsHidden( 3 ) );
}


// ============================================================================
// ShowMultipleNets: hide 1,2 then show 1,2 -> all visible
// ============================================================================
BOOST_AUTO_TEST_CASE( ShowMultipleNets )
{
    NET_VISIBILITY_HELPERS::HideNets( &toolManager, { 1, 2 } );
    BOOST_CHECK( IsHidden( 1 ) );
    BOOST_CHECK( IsHidden( 2 ) );

    NET_VISIBILITY_HELPERS::ShowNets( &toolManager, { 1, 2 } );
    BOOST_CHECK( !IsHidden( 1 ) );
    BOOST_CHECK( !IsHidden( 2 ) );
    BOOST_CHECK( !IsHidden( 3 ) );
}


// ============================================================================
// ShowAllNets: hide all, then show all, verify hidden set is empty
// ============================================================================
BOOST_AUTO_TEST_CASE( ShowAllNets )
{
    // Hide all nets (including net 0)
    renderSettings.GetHiddenNets().clear();
    NET_VISIBILITY_HELPERS::HideNets( &toolManager, { 0, 1, 2, 3 } );
    BOOST_CHECK( IsHidden( 0 ) );
    BOOST_CHECK( IsHidden( 1 ) );
    BOOST_CHECK( IsHidden( 2 ) );
    BOOST_CHECK( IsHidden( 3 ) );
    BOOST_CHECK( renderSettings.GetHiddenNets().size() == 4 );

    // Show all nets on the board
    NET_VISIBILITY_HELPERS::ShowAllNets( &toolManager, &board );

    // Verify all nets are now visible (hidden set is empty)
    BOOST_CHECK( renderSettings.GetHiddenNets().empty() );
    BOOST_CHECK( !IsHidden( 0 ) );
    BOOST_CHECK( !IsHidden( 1 ) );
    BOOST_CHECK( !IsHidden( 2 ) );
    BOOST_CHECK( !IsHidden( 3 ) );
}


// ============================================================================
// HideAllNets: all nets hidden
// ============================================================================
BOOST_AUTO_TEST_CASE( HideAllNets )
{
    renderSettings.GetHiddenNets().clear();
    BOOST_CHECK( renderSettings.GetHiddenNets().empty() );

    NET_VISIBILITY_HELPERS::HideAllNets( &toolManager, &board );

    BOOST_CHECK( IsHidden( 0 ) );
    BOOST_CHECK( IsHidden( 1 ) );
    BOOST_CHECK( IsHidden( 2 ) );
    BOOST_CHECK( IsHidden( 3 ) );
    BOOST_CHECK( renderSettings.GetHiddenNets().size() == 4 );
}


// ============================================================================
// ShowOnlyNets: select net 1 -> only net 1 visible, all others hidden
// ============================================================================
BOOST_AUTO_TEST_CASE( ShowOnlyNets )
{
    renderSettings.GetHiddenNets().clear();

    // "Show Only Selected" with net 1 selected: net 1 visible, all others hidden
    NET_VISIBILITY_HELPERS::ShowOnlyNets( &toolManager, &board, { 1 } );

    BOOST_CHECK( !IsHidden( 1 ) );     // selected -> visible
    BOOST_CHECK( IsHidden( 0 ) );      // others -> hidden
    BOOST_CHECK( IsHidden( 2 ) );
    BOOST_CHECK( IsHidden( 3 ) );
}


// ============================================================================
// ShowOnlyNets with all nets selected: all visible
// ============================================================================
BOOST_AUTO_TEST_CASE( ShowOnlyNetsAllSelected )
{
    renderSettings.GetHiddenNets().clear();
    NET_VISIBILITY_HELPERS::HideNets( &toolManager, { 0, 1, 2, 3 } );
    BOOST_CHECK( renderSettings.GetHiddenNets().size() == 4 );

    // All nets selected -> all visible
    NET_VISIBILITY_HELPERS::ShowOnlyNets( &toolManager, &board, { 0, 1, 2, 3 } );
    BOOST_CHECK( renderSettings.GetHiddenNets().empty() );
}


// ============================================================================
// ShowOnlyNets with empty selection: all hidden
// ============================================================================
BOOST_AUTO_TEST_CASE( ShowOnlyNetsEmptySelection )
{
    renderSettings.GetHiddenNets().clear();
    BOOST_CHECK( renderSettings.GetHiddenNets().empty() );

    // Empty selection -> nothing shown, all hidden
    NET_VISIBILITY_HELPERS::ShowOnlyNets( &toolManager, &board, {} );
    BOOST_CHECK( IsHidden( 0 ) );
    BOOST_CHECK( IsHidden( 1 ) );
    BOOST_CHECK( IsHidden( 2 ) );
    BOOST_CHECK( IsHidden( 3 ) );
}


// ============================================================================
// EmptySetHandling: empty sets are no-ops for Show/Hide
// ============================================================================
BOOST_AUTO_TEST_CASE( EmptySetHandling )
{
    std::set<int> empty;

    renderSettings.GetHiddenNets().clear();

    NET_VISIBILITY_HELPERS::ShowNets( &toolManager, empty );
    NET_VISIBILITY_HELPERS::HideNets( &toolManager, empty );
    BOOST_CHECK( renderSettings.GetHiddenNets().empty() );
}


// ============================================================================
// SequentialOperations: hide 1, hide 2, show 1 -> only 2 remains hidden
// ============================================================================
BOOST_AUTO_TEST_CASE( SequentialOperations )
{
    NET_VISIBILITY_HELPERS::HideNets( &toolManager, { 1 } );
    BOOST_CHECK( IsHidden( 1 ) );
    BOOST_CHECK( !IsHidden( 2 ) );

    NET_VISIBILITY_HELPERS::HideNets( &toolManager, { 2 } );
    BOOST_CHECK( IsHidden( 1 ) );
    BOOST_CHECK( IsHidden( 2 ) );

    NET_VISIBILITY_HELPERS::ShowNets( &toolManager, { 1 } );
    BOOST_CHECK( !IsHidden( 1 ) );     // shown again
    BOOST_CHECK( IsHidden( 2 ) );      // still hidden

    NET_VISIBILITY_HELPERS::ShowAllNets( &toolManager, &board );
    BOOST_CHECK( renderSettings.GetHiddenNets().empty() );
}


// ============================================================================
// GetVisibilityState + GetMenuState: verify state and menu flags in various scenarios
// ============================================================================
BOOST_AUTO_TEST_CASE( VisibilityStateAllShown )
{
    renderSettings.GetHiddenNets().clear();

    auto vis = NET_VISIBILITY_HELPERS::GetVisibilityState( &board, { 1 }, renderSettings.GetHiddenNets() );

    BOOST_CHECK( vis.anySelected );
    BOOST_CHECK( vis.allSelectedVisible );
    BOOST_CHECK( !vis.allSelectedHidden );
    BOOST_CHECK( !vis.onlySelectedShown );
    BOOST_CHECK( vis.allShown );
    BOOST_CHECK( !vis.allHidden );

    auto menu = NET_VISIBILITY_HELPERS::GetMenuState( vis );
    BOOST_CHECK( !menu.enableShowSelected );   // already visible
    BOOST_CHECK( menu.enableHideSelected );
    BOOST_CHECK( menu.enableShowOnlySelected );
    BOOST_CHECK( !menu.enableShowAll );        // already all shown
    BOOST_CHECK( menu.enableHideAll );
}


BOOST_AUTO_TEST_CASE( VisibilityStateAllHidden )
{
    renderSettings.GetHiddenNets().clear();
    NET_VISIBILITY_HELPERS::HideAllNets( &toolManager, &board );

    auto vis = NET_VISIBILITY_HELPERS::GetVisibilityState( &board, { 1 }, renderSettings.GetHiddenNets() );

    BOOST_CHECK( vis.anySelected );
    BOOST_CHECK( !vis.allSelectedVisible );
    BOOST_CHECK( vis.allSelectedHidden );
    BOOST_CHECK( !vis.onlySelectedShown );
    BOOST_CHECK( !vis.allShown );
    BOOST_CHECK( vis.allHidden );

    auto menu = NET_VISIBILITY_HELPERS::GetMenuState( vis );
    BOOST_CHECK( menu.enableShowSelected );
    BOOST_CHECK( !menu.enableHideSelected );   // already hidden
    BOOST_CHECK( menu.enableShowOnlySelected );
    BOOST_CHECK( menu.enableShowAll );
    BOOST_CHECK( !menu.enableHideAll );        // already all hidden
}


BOOST_AUTO_TEST_CASE( VisibilityStateOnlySelectedShown )
{
    renderSettings.GetHiddenNets().clear();
    NET_VISIBILITY_HELPERS::ShowOnlyNets( &toolManager, &board, { 1 } );

    auto vis = NET_VISIBILITY_HELPERS::GetVisibilityState( &board, { 1 }, renderSettings.GetHiddenNets() );

    BOOST_CHECK( vis.anySelected );
    BOOST_CHECK( vis.allSelectedVisible );
    BOOST_CHECK( !vis.allSelectedHidden );
    BOOST_CHECK( vis.onlySelectedShown );
    BOOST_CHECK( !vis.allShown );
    BOOST_CHECK( !vis.allHidden );

    auto menu = NET_VISIBILITY_HELPERS::GetMenuState( vis );
    BOOST_CHECK( !menu.enableShowSelected );
    BOOST_CHECK( menu.enableHideSelected );
    BOOST_CHECK( !menu.enableShowOnlySelected );  // already in this state
    BOOST_CHECK( menu.enableShowAll );
    BOOST_CHECK( menu.enableHideAll );
}


BOOST_AUTO_TEST_CASE( VisibilityStateEmptySelection )
{
    renderSettings.GetHiddenNets().clear();

    auto vis = NET_VISIBILITY_HELPERS::GetVisibilityState( &board, {}, renderSettings.GetHiddenNets() );

    BOOST_CHECK( !vis.anySelected );
    BOOST_CHECK( !vis.allSelectedVisible );
    BOOST_CHECK( !vis.allSelectedHidden );
    BOOST_CHECK( !vis.onlySelectedShown );
    BOOST_CHECK( vis.allShown );
    BOOST_CHECK( !vis.allHidden );

    auto menu = NET_VISIBILITY_HELPERS::GetMenuState( vis );
    BOOST_CHECK( !menu.enableShowSelected );   // no selection
    BOOST_CHECK( !menu.enableHideSelected );
    BOOST_CHECK( !menu.enableShowOnlySelected );
    BOOST_CHECK( !menu.enableShowAll );
    BOOST_CHECK( menu.enableHideAll );
}


BOOST_AUTO_TEST_SUITE_END()
