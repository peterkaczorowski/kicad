/*
 * This program source code file is part of KICAD, a free EDA CAD application.
 *
 * Copyright (C) 2018 jean-pierre.charras
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

#include <wx/dcclient.h>
#include <wx/gbsizer.h>
#include <wx/msgdlg.h>
#include <bitmap_base.h>
#include <pcb_base_edit_frame.h>
#include <tool/actions.h>
#include <confirm.h>
#include <units_provider.h>
#include <dialogs/panel_image_editor.h>

#include <algorithm>


PANEL_IMAGE_EDITOR::PANEL_IMAGE_EDITOR( UNITS_PROVIDER* aUnitsProvider, wxWindow* aParent, const BITMAP_BASE& aItem ) :
        PANEL_IMAGE_EDITOR_BASE( aParent ),
        m_scale( aUnitsProvider, aParent, m_staticTextScale, m_textCtrlScale, nullptr ),
        m_workingImage( std::make_unique<BITMAP_BASE>( aItem ) )
{
    m_scale.SetUnits( EDA_UNITS::UNSCALED );
}


bool PANEL_IMAGE_EDITOR::TransferDataToWindow()
{
    // Use ChangeDoubleValue (not SetDoubleValue) so loading the field does not emit
    // wxEVT_TEXT.  The owning dialog binds that event to a handler that applies a
    // *uniform* scale, which would clobber an independent X/Y scale on the working image.
    m_scale.ChangeDoubleValue( m_workingImage->GetScale() );

    m_stPPI_Value->SetLabel( wxString::Format( wxT( "%d" ), m_workingImage->GetPPI() ) );

    return true;
}


void PANEL_IMAGE_EDITOR::OnGreyScaleConvert( wxCommandEvent& event )
{
    m_workingImage->ConvertToGreyscale();
    m_panelDraw->Refresh();
}


/*
 * Test params values correctness
 * Currently scale value must give an actual image > MIN_SIZE pixels (mandatory to be able to
 * see the image) and < MAX_SIZE pixels (if bigger, a confirmation will be asked)
 * Note: The image definition is 300ppi in drawing routines.
 */
bool PANEL_IMAGE_EDITOR::CheckValues()
{
    wxWindow* host = wxGetTopLevelParent( this );

#define MIN_SIZE 15   // Min size in pixels after scaling (50 mils)
#define MAX_SIZE 6000 // Max size in pixels after scaling (20 inches)
    const double scaleX = m_workingImage->GetScaleX();
    const double scaleY = m_workingImage->GetScaleY();

    // Test number correctness
    if( scaleX <= 0.0 || scaleY <= 0.0 )
    {
        DisplayErrorMessage( host, _( "Scale must be a positive number." ) );
        return false;
    }

    // Test value correctness (each axis is scaled independently)
    VECTOR2I psize = m_workingImage->GetSizePixels();
    int      size_min = (int) std::min( ( psize.x * scaleX ), ( psize.y * scaleY ) );

    if( size_min < MIN_SIZE ) // if the size is too small, the image will be hard to locate
    {
        DisplayErrorMessage( host, wxString::Format( _( "This scale results in an image which is too small "
                                                        "(%.2f mm or %.1f mil)." ),
                                                     25.4 / 300 * size_min,
                                                     1000.0 / 300.0 * size_min ) );
        return false;
    }

    int size_max = (int) std::max( ( psize.x * scaleX ), ( psize.y * scaleY ) );

    if( size_max > MAX_SIZE )
    {
        // the actual size is 25.4/300 * size_max in mm
        if( !IsOK( host, wxString::Format( _( "This scale results in an image which is very large "
                                              "(%.1f mm or %.2f in). Are you sure?" ),
                                           25.4 / 300 * size_max,
                                           size_max / 300.0 ) ) )
        {
            return false;
        }
    }

    return true;
}


bool PANEL_IMAGE_EDITOR::TransferDataFromWindow()
{
    return CheckValues();
}


double PANEL_IMAGE_EDITOR::GetScale() const
{
    return m_scale.GetDoubleValue();
}


void PANEL_IMAGE_EDITOR::SetScale( double aScale )
{
    m_scale.ChangeDoubleValue( aScale );
    UpdateImageScale( aScale );
}


void PANEL_IMAGE_EDITOR::UpdateImageScale( double aScale )
{
    m_workingImage->SetScale( aScale );
    m_panelDraw->Refresh();
}


double PANEL_IMAGE_EDITOR::GetScaleX() const
{
    return m_workingImage->GetScaleX();
}


double PANEL_IMAGE_EDITOR::GetScaleY() const
{
    return m_workingImage->GetScaleY();
}


void PANEL_IMAGE_EDITOR::SetScaleX( double aScale )
{
    m_workingImage->SetScaleX( aScale );

    // Keep the uniform scale field showing the X scale for reference.
    m_scale.ChangeDoubleValue( m_workingImage->GetScaleX() );
    m_panelDraw->Refresh();
}


void PANEL_IMAGE_EDITOR::SetScaleY( double aScale )
{
    m_workingImage->SetScaleY( aScale );
    m_panelDraw->Refresh();
}


VECTOR2I PANEL_IMAGE_EDITOR::GetImageSize() const
{
    return m_workingImage->GetSize();
}


void PANEL_IMAGE_EDITOR::HideScaleControl()
{
    m_staticTextScale->Hide();
    m_textCtrlScale->Hide();
    Layout();
}


void PANEL_IMAGE_EDITOR::SetImageColumnProportion( int aImageProp, int aSideProp )
{
    // The image preview (m_panelDraw) and the right-hand control column (m_sideSizer)
    // both live in the same horizontal sizer; adjust their proportions to control the split.
    if( wxSizer* row = m_panelDraw->GetContainingSizer() )
    {
        if( wxSizerItem* imgItem = row->GetItem( m_panelDraw ) )
            imgItem->SetProportion( aImageProp );

        if( wxSizerItem* sideItem = row->GetItem( m_sideSizer ) )
            sideItem->SetProportion( aSideProp );

        Layout();
    }
}


void PANEL_IMAGE_EDITOR::SetGreyscaleButtonWidthFraction( int aNum, int aDen )
{
    // The button sits in the right-column grid-bag sizer spanning the full width.  Detach
    // it and re-add it wrapped in a horizontal sizer where the button takes aNum parts and
    // a stretch spacer takes the rest, so the button occupies aNum/aDen of the column.
    wxGridBagSizer* gb = dynamic_cast<wxGridBagSizer*>( m_buttonGrey->GetContainingSizer() );

    if( !gb )
        return;

    wxGBPosition pos = gb->GetItemPosition( m_buttonGrey );
    wxGBSpan     span = gb->GetItemSpan( m_buttonGrey );

    gb->Detach( m_buttonGrey );

    wxBoxSizer* wrap = new wxBoxSizer( wxHORIZONTAL );
    wrap->Add( m_buttonGrey, aNum, wxEXPAND, 0 );
    wrap->AddStretchSpacer( aDen - aNum );

    gb->Add( wrap, pos, span, wxEXPAND, 0 );
    Layout();
}


void PANEL_IMAGE_EDITOR::OnRedrawPanel( wxPaintEvent& event )
{
    wxPaintDC dc( m_panelDraw );
    wxSize    display_size = m_panelDraw->GetClientSize();

    double img_scale = 1.0 / m_workingImage->GetScalingFactorX();
    VECTOR2I img_size_pixels = m_workingImage->GetSizePixels();

    // Adjust the display scale to use the full available display area
    double scale_X = (double)display_size.x/img_size_pixels.x;
    double scale_Y = (double)display_size.y/img_size_pixels.y;

    double display_scale = img_scale * std::min( scale_X, scale_Y );

    dc.SetUserScale( display_scale, display_scale );
    m_workingImage->DrawBitmap( &dc, VECTOR2I( m_workingImage->GetSize()/2 ) );
}


void PANEL_IMAGE_EDITOR::TransferToImage( BITMAP_BASE& aItem )
{
    // The working image already holds the final per-axis scales (kept in sync by the
    // owning dialog through SetScale / SetScaleX / SetScaleY), so just copy it over.
    aItem.ImportData( *m_workingImage );
}
