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

#ifndef DIALOG_WORKSPACE_HISTORY_H
#define DIALOG_WORKSPACE_HISTORY_H

#include <dialog_shim.h>
#include <mdw/mdw_revision.h>
#include <mdw/mdw_types.h>

#include <vector>

#include <wx/string.h>

class MDW_FRAMEWORK;
class MDW_SESSION;
class MDW_WORKSPACE_SERVICE;
class wxButton;
class wxListCtrl;
class wxListEvent;
class wxStaticText;


/**
 * Show one item's revision history and compare two revisions. The list is filled
 * from the workspace service; Compare downloads the two selected revisions to
 * temporary files and hands them to the mainline diff through MDW_DIFF_PRESENTER.
 */
class DIALOG_WORKSPACE_HISTORY : public DIALOG_SHIM
{
public:
    DIALOG_WORKSPACE_HISTORY( wxWindow* aParent, MDW_FRAMEWORK* aFramework,
                              const wxString& aProjectPath, const wxString& aItemId );

    ~DIALOG_WORKSPACE_HISTORY() override;

private:
    void onSelectionChanged( wxListEvent& aEvent );
    void onCompare( wxCommandEvent& aEvent );

    /// Fill the list from the item's revision history, newest first.
    void populateHistory();

    /// Enable Compare only when exactly two revisions are selected.
    void updateButtons();

    /// Report a failed service call without throwing.
    void reportError( MDW_ERROR_CODE aCode, const wxString& aContext );

    MDW_FRAMEWORK*         m_framework;
    wxString               m_projectPath;
    wxString               m_itemId;
    MDW_SESSION*           m_session;
    MDW_WORKSPACE_SERVICE* m_svc;

    wxListCtrl*   m_list;
    wxButton*     m_compareButton;
    wxStaticText* m_statusText;

    std::vector<MDW_REVISION> m_revisions;              ///< Parallel to the list rows
    std::vector<wxString>     m_tempFiles;              ///< Downloaded revisions, removed on close
};

#endif // DIALOG_WORKSPACE_HISTORY_H
