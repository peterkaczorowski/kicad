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

#ifndef DIALOG_IMAGE_PROPERTIES_H
#define DIALOG_IMAGE_PROPERTIES_H

#include <dialogs/dialog_image_properties_base.h>
#include <widgets/unit_binder.h>


class SCH_EDIT_FRAME;
class SCH_BITMAP;
class PANEL_IMAGE_EDITOR;
class wxChoice;
class wxRadioButton;
class wxCheckBox;


class DIALOG_IMAGE_PROPERTIES : public DIALOG_IMAGE_PROPERTIES_BASE
{
public:
    DIALOG_IMAGE_PROPERTIES( SCH_EDIT_FRAME* aParent, SCH_BITMAP& aBitmap );
    ~DIALOG_IMAGE_PROPERTIES() override {}

private:
    bool TransferDataToWindow() override;
    bool TransferDataFromWindow() override;
    void onWidthChanged( wxCommandEvent& aEvent );
    void onHeightChanged( wxCommandEvent& aEvent );
    void onUnitChanged( wxCommandEvent& aEvent );

    ///< True when the Width/Height fields are showing a percentage of the native size.
    bool isPercentMode() const;

    ///< Apply the unit selector choice to the Width/Height unit binders.
    void applyUnitSelection();

    ///< Refresh the Width and Height fields from the working image (in the current units).
    void updateSizeFields();

    ///< Refresh only the Width (resp. Height) field from the working image.
    void setWidthField();
    void setHeightField();

    ///< Currently selected anchor (index into the 3x3 radio-button grid == ANCHOR_POINT).
    int  getAnchorSelection() const;
    void setAnchorSelection( int aIndex );

private:
    SCH_EDIT_FRAME*     m_frame;
    SCH_BITMAP&         m_bitmap;
    PANEL_IMAGE_EDITOR* m_imageEditor;

    UNIT_BINDER         m_posX;
    UNIT_BINDER         m_posY;
    UNIT_BINDER         m_width;
    UNIT_BINDER         m_height;

    wxChoice*           m_unitSelector;
    wxCheckBox*         m_cbLockRatio;

    ///< 3x3 grid of anchor radio buttons (no labels); index order matches ANCHOR_POINT.
    wxRadioButton*      m_anchorBtns[9];
};

#endif
