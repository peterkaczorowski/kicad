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

#ifndef NET_VISIBILITY_HELPERS_H
#define NET_VISIBILITY_HELPERS_H

#include <set>

class BOARD;
class TOOL_MANAGER;

/**
 * Compound visibility state of the ratsnest, derived from the hidden-nets set
 * and the current selection. Shared by both panels (Net Inspector, Appearance Manager).
 */
struct NET_VISIBILITY_STATE
{
    bool anySelected        = false;  ///< at least one net is selected
    bool allSelectedVisible = false;  ///< all selected nets are currently visible
    bool allSelectedHidden  = false;  ///< all selected nets are currently hidden
    bool onlySelectedShown  = false;  ///< only the selected nets are visible, all others hidden
    bool allShown           = false;  ///< no nets are hidden (all visible)
    bool allHidden          = false;  ///< all nets are hidden (none visible)
};

/**
 * Enable/disable flags for the ratsnest visibility context menu items,
 * derived from NET_VISIBILITY_STATE.
 */
struct NET_VISIBILITY_MENU_STATE
{
    bool enableShowSelected    = false;
    bool enableHideSelected    = false;
    bool enableShowOnlySelected = false;
    bool enableShowAll         = false;
    bool enableHideAll         = false;
};

/**
 * Helper functions for managing net visibility in the ratsnest.
 *
 * These functions centralize the logic for showing and hiding nets to prevent
 * divergence between Appearance Manager and Net Inspector panels.
 */
namespace NET_VISIBILITY_HELPERS
{
    /**
     * Compute the compound ratsnest visibility state.
     *
     * @param aBoard The board containing all nets
     * @param aSelectedCodes Currently selected net codes
     * @param aHiddenNets Currently hidden net codes (from PCB_RENDER_SETTINGS::GetHiddenNets)
     */
    NET_VISIBILITY_STATE GetVisibilityState( BOARD* aBoard, const std::set<int>& aSelectedCodes,
                                             const std::set<int>& aHiddenNets );

    /**
     * Derive menu enable/disable flags from the visibility state.
     */
    NET_VISIBILITY_MENU_STATE GetMenuState( const NET_VISIBILITY_STATE& aState );

    /**
     * Show the specified nets in the ratsnest.
     *
     * @param aToolManager The tool manager to run actions through
     * @param aNetCodes Set of net codes to show
     */
    void ShowNets( TOOL_MANAGER* aToolManager, const std::set<int>& aNetCodes );

    /**
     * Hide the specified nets in the ratsnest.
     *
     * @param aToolManager The tool manager to run actions through
     * @param aNetCodes Set of net codes to hide
     */
    void HideNets( TOOL_MANAGER* aToolManager, const std::set<int>& aNetCodes );

    /**
     * Show all nets in the ratsnest.
     *
     * @param aToolManager The tool manager to run actions through
     * @param aBoard The board containing all nets
     */
    void ShowAllNets( TOOL_MANAGER* aToolManager, BOARD* aBoard );

    /**
     * Hide all nets in the ratsnest.
     *
     * @param aToolManager The tool manager to run actions through
     * @param aBoard The board containing all nets
     */
    void HideAllNets( TOOL_MANAGER* aToolManager, BOARD* aBoard );

    /**
     * Show only the specified nets, hiding all others.
     *
     * @param aToolManager The tool manager to run actions through
     * @param aBoard The board containing all nets
     * @param aSelectedCodes Set of net codes to show; all others get hidden
     */
    void ShowOnlyNets( TOOL_MANAGER* aToolManager, BOARD* aBoard, const std::set<int>& aSelectedCodes );
};

#endif // NET_VISIBILITY_HELPERS_H
