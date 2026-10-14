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

/**
 * @file mdw_save_guard.h
 * @brief A save warning for managed items whose lifecycle blocks edits.
 *
 * When the open document is a workspace item in a write-blocking category the
 * editor should warn before saving, because the backend may reject the change.
 * The decision to warn is the shared lifecycle rule; the confirmation is a
 * single dialog so every editor phrases it the same way.
 */

#ifndef MDW_SAVE_GUARD_H
#define MDW_SAVE_GUARD_H

#include <kicommon.h>
#include <mdw/mdw_types.h>

#include <wx/string.h>

class PROJECT;
class wxWindow;

namespace MDW_SAVE_GUARD
{
/// True when a category blocks writes and therefore warrants a save warning.
KICOMMON_API bool NeedsWarning( MDW_LIFECYCLE_CATEGORY aCategory );

/**
 * Ask the user whether to proceed with saving a managed item. Returns true to
 * continue. When the category blocks writes the user is warned and may cancel;
 * otherwise it returns true without prompting.
 */
KICOMMON_API bool ConfirmSave( MDW_LIFECYCLE_CATEGORY aCategory, const wxString& aItemName,
                               wxWindow* aParent );

/**
 * The editors' entry point. Resolves aLocalPath to the workspace item it was
 * checked out from, asks the backend for that item's lifecycle category, and
 * warns when the category blocks writes.
 *
 * Returns true to proceed with the save. Everything that is not a bound,
 * write-blocked workspace item proceeds silently, so an unmanaged project and a
 * disabled integration both cost one map lookup and nothing else.
 *
 * The category query is a blocking call to the backend, so this belongs on an
 * explicit save and not on an autosave path.
 */
KICOMMON_API bool ConfirmSaveOf( PROJECT& aProject, const wxString& aLocalPath,
                                 wxWindow* aParent );
}

#endif // MDW_SAVE_GUARD_H
