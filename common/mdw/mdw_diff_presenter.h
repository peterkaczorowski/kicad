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
 * @file mdw_diff_presenter.h
 * @brief Presents a visual comparison of two revisions through the mainline diff.
 *
 * The framework owns no diff engine. Once two revisions are downloaded locally,
 * the presenter derives the document kind from the file extension and forwards
 * to KICAD_DIFF::DispatchOpenDiffDialog. It is presentation, not workflow: it
 * proposes no merge and mutates neither revision.
 */

#ifndef MDW_DIFF_PRESENTER_H
#define MDW_DIFF_PRESENTER_H

#include <kicommon.h>
#include <diff_merge/diff_doc_kind.h>

#include <wx/string.h>

class KIWAY;
class REPORTER;
class wxWindow;

class KICOMMON_API MDW_DIFF_PRESENTER
{
public:
    MDW_DIFF_PRESENTER( KIWAY* aKiway, wxWindow* aParent = nullptr );

    virtual ~MDW_DIFF_PRESENTER() = default;

    /**
     * Open the diff dialog on two downloaded revision files, labelled for the
     * user (for example "board.kicad_pcb @ r3"). Returns true when the dialog
     * was dispatched.
     */
    bool Compare( const wxString& aFileA, const wxString& aFileB, const wxString& aLabelA,
                  const wxString& aLabelB );

protected:
    /// The dispatch seam, virtual so the extension-to-DOC_KIND routing can be
    /// exercised without a live kiface.
    virtual int dispatchDiff( KICAD_DIFF::DOC_KIND aKind, const wxString& aFileA,
                              const wxString& aFileB, const wxString& aLabelA,
                              const wxString& aLabelB );

private:
    KIWAY*    m_kiway;
    wxWindow* m_parent;
};

#endif // MDW_DIFF_PRESENTER_H
