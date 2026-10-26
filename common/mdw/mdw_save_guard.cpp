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

#include <mdw/mdw_save_guard.h>
#include <mdw/mdw_framework.h>
#include <mdw/mdw_session.h>
#include <mdw/mdw_state_mapper.h>
#include <mdw/mdw_workspace_service.h>

#include <pgm_base.h>
#include <project.h>
#include <project/project_file.h>
#include <settings/common_settings.h>

#include <wx/filename.h>
#include <wx/msgdlg.h>


bool MDW_SAVE_GUARD::NeedsWarning( MDW_LIFECYCLE_CATEGORY aCategory )
{
    return MDW_STATE_MAPPER::CategoryBlocksWrites( aCategory );
}


bool MDW_SAVE_GUARD::ConfirmSave( MDW_LIFECYCLE_CATEGORY aCategory, const wxString& aItemName,
                                  wxWindow* aParent )
{
    if( !NeedsWarning( aCategory ) )
        return true;

    wxString category = MDW_STATE_MAPPER::CategoryDisplayName( aCategory );

    wxString message = wxString::Format(
            _( "\"%s\" is %s in the Design Workspace and may not accept edits.\n\n"
               "Save the local changes anyway?" ),
            aItemName, category );

    wxMessageDialog dialog( aParent, message, _( "Design Workspace" ),
                            wxYES_NO | wxNO_DEFAULT | wxICON_WARNING );
    dialog.SetYesNoLabels( _( "Save Anyway" ), _( "Cancel" ) );

    return dialog.ShowModal() == wxID_YES;
}


bool MDW_SAVE_GUARD::ConfirmSaveOf( PROJECT& aProject, const wxString& aLocalPath,
                                    wxWindow* aParent )
{
    if( !Pgm().GetCommonSettings()->m_Workspace.enable_integration )
        return true;

    const wxString itemId = aProject.GetProjectFile().GetWorkspaceItemId( aLocalPath );

    if( itemId.IsEmpty() )
        return true;

    MDW_FRAMEWORK* framework = Pgm().MdwFrameworkOrNull();
    MDW_SESSION*   session = framework ? framework->Session( aProject.GetProjectFullName() )
                                       : nullptr;
    MDW_WORKSPACE_SERVICE* service = session ? session->Service() : nullptr;

    // A bound item whose backend is unreachable is not a reason to block a local
    // save; the user still owns their working copy.
    if( !service )
        return true;

    MDW_LIFECYCLE_CATEGORY category = MDW_LIFECYCLE_CATEGORY::EDITABLE;
    wxString               stateId;

    if( service->GetLifecycleCategory( itemId, category, stateId ) != MDW_ERROR_CODE::SUCCESS )
        return true;

    if( !ConfirmSave( category, wxFileName( aLocalPath ).GetFullName(), aParent ) )
        return false;

    // Writing the file is what makes the working copy differ from the revision
    // it was checked out at, and it stays that way until a check-in, which is
    // where the service clears this again.
    session->DirtyTracker().SetDirty( itemId, true );

    return true;
}
