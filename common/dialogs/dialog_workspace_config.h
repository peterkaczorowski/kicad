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

#ifndef DIALOG_WORKSPACE_CONFIG_H
#define DIALOG_WORKSPACE_CONFIG_H

#include <dialog_shim.h>
#include <mdw/mdw_session.h>

#include <vector>

#include <wx/string.h>

class MDW_FRAMEWORK;
class wxChoice;
class wxTextCtrl;
class wxStaticText;


/**
 * Bind the active project to a Design Workspace backend. The user picks a
 * discovered backend, names the workspace and supplies the opaque binding blob
 * and any credentials; Test spawns the backend once to prove the settings reach
 * a live session. On OK the resulting binding is returned for the caller to
 * persist to the project file.
 */
class DIALOG_WORKSPACE_CONFIG : public DIALOG_SHIM
{
public:
    DIALOG_WORKSPACE_CONFIG( wxWindow* aParent, MDW_FRAMEWORK* aFramework,
                             const wxString& aProjectPath, const MDW_PROJECT_BINDING& aBinding );

    /// The binding the user configured. Bound only when a backend was chosen.
    const MDW_PROJECT_BINDING& GetBinding() const { return m_result; }

private:
    bool TransferDataToWindow() override;
    bool TransferDataFromWindow() override;

    void onTestConnection( wxCommandEvent& aEvent );

    /// Read the form into a binding, storing any supplied credentials.
    MDW_PROJECT_BINDING collectBinding();

    MDW_FRAMEWORK*      m_framework;
    wxString            m_projectPath;
    MDW_PROJECT_BINDING m_binding;
    MDW_PROJECT_BINDING m_result;

    wxChoice*     m_backendChoice;
    wxTextCtrl*   m_workspaceIdCtrl;
    wxTextCtrl*   m_bindingCtrl;
    wxTextCtrl*   m_usernameCtrl;
    wxTextCtrl*   m_passwordCtrl;
    wxStaticText* m_statusText;

    std::vector<wxString> m_backendIds;                 ///< Parallel to m_backendChoice entries
};

#endif // DIALOG_WORKSPACE_CONFIG_H
