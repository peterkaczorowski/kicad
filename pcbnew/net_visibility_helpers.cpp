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

#include <algorithm>

#include <board.h>
#include <tool/tool_manager.h>
#include <tools/pcb_actions.h>
#include "net_visibility_helpers.h"

namespace NET_VISIBILITY_HELPERS
{

NET_VISIBILITY_STATE GetVisibilityState( BOARD* aBoard, const std::set<int>& aSelectedCodes,
                                        const std::set<int>& aHiddenNets )
{
    NET_VISIBILITY_STATE state;

    std::set<int> allCodes;

    for( NETINFO_ITEM* net : aBoard->GetNetInfo() )
        allCodes.insert( net->GetNetCode() );

    state.anySelected = !aSelectedCodes.empty();

    if( state.anySelected )
    {
        state.allSelectedVisible = std::all_of( aSelectedCodes.begin(), aSelectedCodes.end(),
                [&]( int c ) { return aHiddenNets.count( c ) == 0; } );
        state.allSelectedHidden  = std::all_of( aSelectedCodes.begin(), aSelectedCodes.end(),
                [&]( int c ) { return aHiddenNets.count( c ) > 0; } );

        std::set<int> othersHidden;

        for( int c : allCodes )
        {
            if( !aSelectedCodes.count( c ) )
                othersHidden.insert( c );
        }

        state.onlySelectedShown = aHiddenNets == othersHidden;
    }

    state.allShown  = aHiddenNets.empty();
    state.allHidden = aHiddenNets.size() >= allCodes.size();

    return state;
}


NET_VISIBILITY_MENU_STATE GetMenuState( const NET_VISIBILITY_STATE& aState )
{
    NET_VISIBILITY_MENU_STATE menu;

    menu.enableShowSelected     = aState.anySelected && !aState.allSelectedVisible;
    menu.enableHideSelected     = aState.anySelected && !aState.allSelectedHidden;
    menu.enableShowOnlySelected = aState.anySelected && !aState.onlySelectedShown;
    menu.enableShowAll          = !aState.allShown;
    menu.enableHideAll          = !aState.allHidden;

    return menu;
}


void ShowNets( TOOL_MANAGER* aToolManager, const std::set<int>& aNetCodes )
{
    for( int netCode : aNetCodes )
    {
        aToolManager->RunAction( PCB_ACTIONS::showNetInRatsnest, netCode );
    }
}


void HideNets( TOOL_MANAGER* aToolManager, const std::set<int>& aNetCodes )
{
    for( int netCode : aNetCodes )
    {
        aToolManager->RunAction( PCB_ACTIONS::hideNetInRatsnest, netCode );
    }
}


void ShowAllNets( TOOL_MANAGER* aToolManager, BOARD* aBoard )
{
    std::set<int> allNetCodes;

    for( NETINFO_ITEM* net : aBoard->GetNetInfo() )
    {
        allNetCodes.insert( net->GetNetCode() );
    }

    ShowNets( aToolManager, allNetCodes );
}


void HideAllNets( TOOL_MANAGER* aToolManager, BOARD* aBoard )
{
    std::set<int> allNetCodes;

    for( NETINFO_ITEM* net : aBoard->GetNetInfo() )
    {
        allNetCodes.insert( net->GetNetCode() );
    }

    HideNets( aToolManager, allNetCodes );
}


void ShowOnlyNets( TOOL_MANAGER* aToolManager, BOARD* aBoard, const std::set<int>& aSelectedCodes )
{
    // Show selected nets
    ShowNets( aToolManager, aSelectedCodes );

    // Hide all others
    std::set<int> otherNetCodes;

    for( NETINFO_ITEM* net : aBoard->GetNetInfo() )
    {
        int netCode = net->GetNetCode();

        if( aSelectedCodes.find( netCode ) == aSelectedCodes.end() )
        {
            otherNetCodes.insert( netCode );
        }
    }

    HideNets( aToolManager, otherNetCodes );
}

} // namespace NET_VISIBILITY_HELPERS
