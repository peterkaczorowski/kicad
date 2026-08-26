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
#include <mdw/mdw_state_mapper.h>

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
