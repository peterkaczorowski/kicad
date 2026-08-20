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

#include <mdw/mdw_diff_presenter.h>

#include <diff_merge/merge_dispatch.h>


MDW_DIFF_PRESENTER::MDW_DIFF_PRESENTER( KIWAY* aKiway, wxWindow* aParent ) :
        m_kiway( aKiway ),
        m_parent( aParent )
{
}


bool MDW_DIFF_PRESENTER::Compare( const wxString& aFileA, const wxString& aFileB,
                                  const wxString& aLabelA, const wxString& aLabelB )
{
    KICAD_DIFF::DOC_KIND kind = KICAD_DIFF::DocKindFromExtension( aFileA );

    if( kind == KICAD_DIFF::DOC_KIND::UNKNOWN )
        return false;

    return dispatchDiff( kind, aFileA, aFileB, aLabelA, aLabelB ) == 0;
}


int MDW_DIFF_PRESENTER::dispatchDiff( KICAD_DIFF::DOC_KIND aKind, const wxString& aFileA,
                                      const wxString& aFileB, const wxString& aLabelA,
                                      const wxString& aLabelB )
{
    if( !m_kiway )
        return -1;

    return KICAD_DIFF::DispatchOpenDiffDialog( *m_kiway, aKind, aFileA, aFileB, aLabelA, aLabelB,
                                               m_parent );
}
