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
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#ifndef DIALOG_WORKSPACE_BROWSER_H
#define DIALOG_WORKSPACE_BROWSER_H

#include <dialog_shim.h>
#include <mdw/mdw_item.h>

#include <vector>

#include <wx/string.h>
#include <wx/treebase.h>

class MDW_FRAMEWORK;
class MDW_SESSION;
class MDW_WORKSPACE_SERVICE;
class MDW_PROPERTY_EDITOR;
class wxButton;
class wxListCtrl;
class wxListEvent;
class wxStaticText;
class wxTreeCtrl;
class wxTreeEvent;


/**
 * Browse a bound workspace. The folder tree drives an item listing, the selected
 * item's typed attributes render in an embedded property editor, and the checkout
 * and checkin buttons run the matching workspace-service operations. The dialog
 * degrades to a disabled read-only shell when the project has no live session.
 */
class DIALOG_WORKSPACE_BROWSER : public DIALOG_SHIM
{
public:
    DIALOG_WORKSPACE_BROWSER( wxWindow* aParent, MDW_FRAMEWORK* aFramework,
                              const wxString& aProjectPath );

private:
    void onFolderSelected( wxTreeEvent& aEvent );
    void onFolderExpanding( wxTreeEvent& aEvent );
    void onItemSelected( wxListEvent& aEvent );
    void onCheckout( wxCommandEvent& aEvent );
    void onCheckin( wxCommandEvent& aEvent );
    void onRefresh( wxCommandEvent& aEvent );

    /// Fill the tree root level from the workspace root folders.
    void populateRoots();

    /// Replace a tree node's placeholder children with its real child folders.
    void populateChildren( const wxTreeItemId& aParent, const wxString& aFolderId );

    /// List a folder's items into the center list, clearing any prior selection.
    void loadFolderItems( const wxString& aFolderId );

    /// Load one listed item's attributes into the property editor. The lock and lifecycle
    /// state come from the cached listing entry rather than a fresh metadata call.
    void loadItem( const MDW_ITEM_INFO& aInfo );

    /// Enable or disable the action buttons for the current selection state.
    void updateButtons();

    /// Report a failed service call without throwing.
    void reportError( MDW_ERROR_CODE aCode, const wxString& aContext );

    MDW_FRAMEWORK*         m_framework;
    wxString               m_projectPath;
    MDW_SESSION*           m_session;
    MDW_WORKSPACE_SERVICE* m_svc;

    wxTreeCtrl*         m_tree;
    wxListCtrl*        m_list;
    MDW_PROPERTY_EDITOR* m_props;
    wxButton*          m_checkoutButton;
    wxButton*          m_checkinButton;
    wxButton*          m_refreshButton;
    wxStaticText*      m_statusText;

    wxString                   m_currentFolderId;
    std::vector<MDW_ITEM_INFO> m_items;                 ///< Parallel to the list rows
    wxString                   m_selectedItemId;
};

#endif // DIALOG_WORKSPACE_BROWSER_H
