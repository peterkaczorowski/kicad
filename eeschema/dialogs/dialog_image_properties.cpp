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

#include <dialogs/dialog_image_properties.h>
#include <dialogs/panel_image_editor.h>

#include <sch_edit_frame.h>
#include <sch_bitmap.h>
#include <sch_commit.h>
#include <reference_image.h>
#include <anchor_point.h>
#include <eda_units.h>
#include <origin_transforms.h>

#include <wx/choice.h>
#include <wx/radiobut.h>
#include <wx/checkbox.h>
#include <wx/stattext.h>
#include <wx/statbox.h>
#include <wx/sizer.h>
#include <wx/notebook.h>
#include <wx/panel.h>


DIALOG_IMAGE_PROPERTIES::DIALOG_IMAGE_PROPERTIES( SCH_EDIT_FRAME* aParent, SCH_BITMAP& aBitmap ) :
        DIALOG_IMAGE_PROPERTIES_BASE( aParent ),
        m_frame( aParent ),
        m_bitmap( aBitmap ),
        m_posX( aParent, m_XPosLabel, m_ModPositionX, m_XPosUnit ),
        m_posY( aParent, m_YPosLabel, m_ModPositionY, m_YPosUnit ),
        m_width( aParent, m_WidthLabel, m_ModWidth, m_WidthUnit ),
        m_height( aParent, m_HeightLabel, m_ModHeight, m_HeightUnit )
{
    // Create the image editor on the notebook's "Image" page.
    m_imageEditor = new PANEL_IMAGE_EDITOR( aParent, m_PanelImage,
                                            aBitmap.GetReferenceImage().MutableImage() );

    m_imageSizer->Add( m_imageEditor, 1, wxEXPAND | wxALL, 5 );

    // Scaling is driven by the Width/Height fields (with a percent unit option), so the
    // editor's own uniform "Scale" field is redundant here.
    m_imageEditor->HideScaleControl();

    // Give the image preview 1/3 of the width and the control column the remaining 2/3.
    m_imageEditor->SetImageColumnProportion( 1, 2 );

    // Shrink the "Convert to Greyscale" button to 1/3 of its column width.
    m_imageEditor->SetGreyscaleButtonWidthFraction( 1, 3 );

    // Build an "Image Size" group and insert it at the top of the editor's right-hand
    // control column, above its PPI / Greyscale grid (image on the left, all size controls
    // on the right).  The box is split into two sub-columns: the left holds Width / Height /
    // Lock aspect ratio, the right holds the Units selector (aligned with the Width row).
    wxStaticBoxSizer* imageSizeBox =
            new wxStaticBoxSizer( wxVERTICAL, m_imageEditor, _( "Image Size" ) );
    wxWindow*         boxWin = imageSizeBox->GetStaticBox();

    // The Width/Height controls were created by the base class on the Image page; move them
    // into the static box so they render correctly inside it.
    m_WidthLabel->Reparent( boxWin );
    m_ModWidth->Reparent( boxWin );
    m_WidthUnit->Reparent( boxWin );
    m_HeightLabel->Reparent( boxWin );
    m_ModHeight->Reparent( boxWin );
    m_HeightUnit->Reparent( boxWin );

    // Two side-by-side sub-columns inside the box.
    wxBoxSizer* sizeColumns = new wxBoxSizer( wxHORIZONTAL );

    // Left sub-column: Width / Height (label, value, unit) then Lock aspect ratio.
    wxBoxSizer* leftCol = new wxBoxSizer( wxVERTICAL );

    wxFlexGridSizer* sizeSizer = new wxFlexGridSizer( 0, 3, 3, 5 );
    sizeSizer->AddGrowableCol( 1 );
    sizeSizer->SetFlexibleDirection( wxBOTH );
    sizeSizer->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_ALL );

    sizeSizer->Add( m_WidthLabel, 0, wxALIGN_CENTER_VERTICAL, 5 );
    sizeSizer->Add( m_ModWidth, 0, wxALIGN_CENTER_VERTICAL | wxEXPAND, 5 );
    sizeSizer->Add( m_WidthUnit, 0, wxALIGN_CENTER_VERTICAL, 5 );
    sizeSizer->Add( m_HeightLabel, 0, wxALIGN_CENTER_VERTICAL, 5 );
    sizeSizer->Add( m_ModHeight, 0, wxALIGN_CENTER_VERTICAL | wxEXPAND, 5 );
    sizeSizer->Add( m_HeightUnit, 0, wxALIGN_CENTER_VERTICAL, 5 );

    leftCol->Add( sizeSizer, 0, wxEXPAND, 5 );

    m_cbLockRatio = new wxCheckBox( boxWin, wxID_ANY, _( "Lock aspect ratio" ) );
    m_cbLockRatio->SetValue( true );
    leftCol->Add( m_cbLockRatio, 0, wxTOP, 5 );

    // Right sub-column: Units selector, aligned at the Width row.
    wxBoxSizer*   rightCol = new wxBoxSizer( wxHORIZONTAL );
    wxStaticText* unitLabel = new wxStaticText( boxWin, wxID_ANY, _( "Units:" ) );
    rightCol->Add( unitLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );

    m_unitSelector = new wxChoice( boxWin, wxID_ANY );
    m_unitSelector->Append( _( "Inches" ) );
    m_unitSelector->Append( _( "Mils" ) );
    m_unitSelector->Append( _( "Millimeters" ) );
    m_unitSelector->Append( _( "Percent" ) );
    rightCol->Add( m_unitSelector, 1, wxALIGN_CENTER_VERTICAL, 5 );

    sizeColumns->Add( leftCol, 1, wxEXPAND | wxRIGHT, 24 );
    sizeColumns->Add( rightCol, 1, wxALIGN_TOP | wxLEFT, 12 );

    imageSizeBox->Add( sizeColumns, 0, wxEXPAND | wxALL, 5 );

    // Keep the box at its natural (doubled) width, left-aligned, leaving a right margin
    // inside the control column rather than stretching across it.
    m_imageEditor->GetSideSizer()->Insert( 0, imageSizeBox, 0, wxBOTTOM, 5 );
    m_imageEditor->Layout();

    m_PanelImage->Layout();
    m_imageSizer->Fit( m_PanelImage );

    // 3x3 anchor selector on the right of the General page: a grid of unlabeled radio
    // buttons.  Button order is row-major (TOP_LEFT=0 .. BOTTOM_RIGHT=8) so the index
    // casts directly to/from ANCHOR_POINT.
    wxStaticBoxSizer* anchorBox =
            new wxStaticBoxSizer( wxVERTICAL, m_PanelGeneral, _( "Anchor" ) );
    wxWindow*         anchorWin = anchorBox->GetStaticBox();

    wxGridSizer* anchorGrid = new wxGridSizer( 3, 3, 4, 4 );

    for( int i = 0; i < 9; ++i )
    {
        // wxRB_GROUP on the first button makes all nine share a single selection group.
        long style = ( i == 0 ) ? wxRB_GROUP : 0;
        m_anchorBtns[i] = new wxRadioButton( anchorWin, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                             wxDefaultSize, style );
        anchorGrid->Add( m_anchorBtns[i], 0, wxALIGN_CENTER, 4 );
    }

    setAnchorSelection( (int) ANCHOR_POINT::CENTER );
    anchorBox->Add( anchorGrid, 0, wxALL, 5 );

    // Anchor box keeps its natural width; the stretch spacer creates the wide right margin.
    m_generalSizer->Add( anchorBox, 0, wxALIGN_TOP | wxALL, 5 );
    m_generalSizer->AddStretchSpacer( 5 );

    // Re-fit the General page now that we've added our controls.
    m_PanelGeneral->Layout();
    m_generalSizer->Fit( m_PanelGeneral );

    // Seed the unit selector from the frame's user units (percent is opt-in).
    switch( m_frame->GetUserUnits() )
    {
    case EDA_UNITS::INCH: m_unitSelector->SetSelection( 0 ); break;
    case EDA_UNITS::MILS: m_unitSelector->SetSelection( 1 ); break;
    default:              m_unitSelector->SetSelection( 2 ); break;  // millimeters
    }

    // Position is an absolute coordinate; Width/Height are sizes (and may be a
    // percentage), so they are never origin-transformed.
    m_posX.SetCoordType( ORIGIN_TRANSFORMS::ABS_X_COORD );
    m_posY.SetCoordType( ORIGIN_TRANSFORMS::ABS_Y_COORD );
    m_width.SetCoordType( ORIGIN_TRANSFORMS::NOT_A_COORD );
    m_height.SetCoordType( ORIGIN_TRANSFORMS::NOT_A_COORD );

    m_ModWidth->Bind( wxEVT_TEXT, &DIALOG_IMAGE_PROPERTIES::onWidthChanged, this );
    m_ModHeight->Bind( wxEVT_TEXT, &DIALOG_IMAGE_PROPERTIES::onHeightChanged, this );
    m_unitSelector->Bind( wxEVT_CHOICE, &DIALOG_IMAGE_PROPERTIES::onUnitChanged, this );

    SetupStandardButtons();

    finishDialogSettings();
}


bool DIALOG_IMAGE_PROPERTIES::TransferDataToWindow()
{
    const REFERENCE_IMAGE& refImage = m_bitmap.GetReferenceImage();

    // The Position fields show the chosen anchor point of the bounding box.
    VECTOR2I anchorPos = refImage.GetAnchorPosition();
    m_posX.SetValue( anchorPos.x );
    m_posY.SetValue( anchorPos.y );

    setAnchorSelection( (int) refImage.GetAnchor() );

    m_imageEditor->TransferDataToWindow();

    applyUnitSelection();
    updateSizeFields();

    return true;
}


bool DIALOG_IMAGE_PROPERTIES::TransferDataFromWindow()
{
    REFERENCE_IMAGE& refImage = m_bitmap.GetReferenceImage();

    if( m_imageEditor->TransferDataFromWindow() )
    {
        SCH_COMMIT commit( m_frame );

        // Save old image in undo list if not already in edit
        if( m_bitmap.GetEditFlags() == 0 )
            commit.Modify( &m_bitmap, m_frame->GetScreen() );

        // Update our bitmap from the editor (carrying its final per-axis scale) first, so the
        // bounding-box size is up to date before we pin the anchor position.
        m_imageEditor->TransferToImage( refImage.MutableImage() );

        refImage.SetAnchor( (ANCHOR_POINT) getAnchorSelection() );
        refImage.SetAnchorPosition( VECTOR2I( m_posX.GetIntValue(), m_posY.GetIntValue() ) );

        if( !commit.Empty() )
            commit.Push( _( "Image Properties" ) );

        return true;
    }

    return false;
}


bool DIALOG_IMAGE_PROPERTIES::isPercentMode() const
{
    return m_unitSelector->GetSelection() == 3;
}


void DIALOG_IMAGE_PROPERTIES::applyUnitSelection()
{
    switch( m_unitSelector->GetSelection() )
    {
    case 0:  // Inches
        m_width.SetUnits( EDA_UNITS::INCH );
        m_height.SetUnits( EDA_UNITS::INCH );
        break;
    case 1:  // Mils
        m_width.SetUnits( EDA_UNITS::MILS );
        m_height.SetUnits( EDA_UNITS::MILS );
        break;
    case 3:  // Percent of native size (100% == scale 1.0)
        m_width.SetUnits( EDA_UNITS::PERCENT );
        m_height.SetUnits( EDA_UNITS::PERCENT );
        m_width.SetPrecision( 2 );
        m_height.SetPrecision( 2 );
        break;
    case 2:  // Millimeters
    default:
        m_width.SetUnits( EDA_UNITS::MM );
        m_height.SetUnits( EDA_UNITS::MM );
        break;
    }
}


int DIALOG_IMAGE_PROPERTIES::getAnchorSelection() const
{
    for( int i = 0; i < 9; ++i )
    {
        if( m_anchorBtns[i]->GetValue() )
            return i;
    }

    return (int) ANCHOR_POINT::CENTER;
}


void DIALOG_IMAGE_PROPERTIES::setAnchorSelection( int aIndex )
{
    if( aIndex >= 0 && aIndex < 9 )
        m_anchorBtns[aIndex]->SetValue( true );
}


void DIALOG_IMAGE_PROPERTIES::setWidthField()
{
    if( isPercentMode() )
        m_width.ChangeDoubleValue( m_imageEditor->GetScaleX() * 100.0 );
    else
        m_width.ChangeValue( m_imageEditor->GetImageSize().x );
}


void DIALOG_IMAGE_PROPERTIES::setHeightField()
{
    if( isPercentMode() )
        m_height.ChangeDoubleValue( m_imageEditor->GetScaleY() * 100.0 );
    else
        m_height.ChangeValue( m_imageEditor->GetImageSize().y );
}


void DIALOG_IMAGE_PROPERTIES::updateSizeFields()
{
    setWidthField();
    setHeightField();
}


void DIALOG_IMAGE_PROPERTIES::onUnitChanged( wxCommandEvent& aEvent )
{
    applyUnitSelection();
    updateSizeFields();
}


void DIALOG_IMAGE_PROPERTIES::onWidthChanged( wxCommandEvent& aEvent )
{
    double newScaleX;

    if( isPercentMode() )
    {
        double pct = m_width.GetDoubleValue();

        if( pct <= 0 )
            return;

        newScaleX = pct / 100.0;
    }
    else
    {
        int      newWidth = m_width.GetIntValue();
        VECTOR2I size = m_imageEditor->GetImageSize();

        if( newWidth <= 0 || size.x <= 0 )
            return;

        newScaleX = m_imageEditor->GetScaleX() * (double) newWidth / size.x;
    }

    m_imageEditor->SetScaleX( newScaleX );

    // When the aspect ratio is locked, keep both axes uniform and refresh the
    // height field to reflect the proportional change.
    if( m_cbLockRatio->GetValue() )
    {
        m_imageEditor->SetScaleY( newScaleX );
        setHeightField();
    }
}


void DIALOG_IMAGE_PROPERTIES::onHeightChanged( wxCommandEvent& aEvent )
{
    double newScaleY;

    if( isPercentMode() )
    {
        double pct = m_height.GetDoubleValue();

        if( pct <= 0 )
            return;

        newScaleY = pct / 100.0;
    }
    else
    {
        int      newHeight = m_height.GetIntValue();
        VECTOR2I size = m_imageEditor->GetImageSize();

        if( newHeight <= 0 || size.y <= 0 )
            return;

        newScaleY = m_imageEditor->GetScaleY() * (double) newHeight / size.y;
    }

    m_imageEditor->SetScaleY( newScaleY );

    if( m_cbLockRatio->GetValue() )
    {
        m_imageEditor->SetScaleX( newScaleY );
        setWidthField();
    }
}
