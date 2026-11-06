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
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <map>

#include <board_design_settings.h>
#include <board.h>
#include <math/util.h>
#include <panel_setup_constraints.h>
#include <panel_setup_constraints_base.h>
#include <pcb_edit_frame.h>
#include <widgets/field_warning_icon.h>
#include <dialogs/panel_setup_constraint_rules.h>
#include <widgets/paged_dialog.h>
#include <wx/settings.h>
#include <wx/textctrl.h>
#include <wx/treebook.h>
#include <bitmaps.h>
#include <advanced_config.h>


PANEL_SETUP_CONSTRAINTS::PANEL_SETUP_CONSTRAINTS( wxWindow* aParentWindow, PCB_EDIT_FRAME* aFrame ) :
        PANEL_SETUP_CONSTRAINTS_BASE( aParentWindow ),
        m_minClearance( aFrame, m_clearanceTitle, m_clearanceCtrl, m_clearanceUnits ),
        m_minConn( aFrame, m_MinConnTitle, m_MinConnCtrl, m_MinConnUnits ),
        m_trackMinWidth( aFrame, m_TrackMinWidthTitle, m_TrackMinWidthCtrl, m_TrackMinWidthUnits ),
        m_viaMinAnnulus( aFrame, m_ViaMinAnnulusTitle, m_ViaMinAnnulusCtrl, m_ViaMinAnnulusUnits ),
        m_viaMinSize( aFrame, m_ViaMinTitle, m_SetViasMinSizeCtrl, m_ViaMinUnits ),
        m_throughHoleMin( aFrame, m_MinDrillTitle, m_MinDrillCtrl, m_MinDrillUnits ),
        m_uviaMinSize( aFrame, m_uviaMinSizeLabel, m_uviaMinSizeCtrl, m_uviaMinSizeUnits ),
        m_uviaMinDrill( aFrame, m_uviaMinDrillLabel, m_uviaMinDrillCtrl, m_uviaMinDrillUnits ),
        m_holeToHoleMin( aFrame, m_HoleToHoleTitle, m_SetHoleToHoleCtrl, m_HoleToHoleUnits ),
        m_holeClearance( aFrame, m_HoleClearanceLabel, m_HoleClearanceCtrl, m_HoleClearanceUnits ),
        m_edgeClearance( aFrame, m_EdgeClearanceLabel, m_EdgeClearanceCtrl, m_EdgeClearanceUnits ),
        m_silkClearance( aFrame, m_silkClearanceLabel, m_silkClearanceCtrl, m_silkClearanceUnits ),
        m_minGrooveWidth( aFrame, m_minGrooveWidthLabel, m_minGrooveWidthCtrl, m_minGrooveWidthUnits ),
        m_minTextHeight( aFrame, m_textHeightLabel, m_textHeightCtrl, m_textHeightUnits ),
        m_minTextThickness( aFrame, m_textThicknessLabel, m_textThicknessCtrl, m_textThicknessUnits ),
        m_maxError( aFrame, m_maxErrorTitle, m_maxErrorCtrl, m_maxErrorUnits ),
        m_validationTimer( this )
{
    m_Frame = aFrame;
    m_BrdSettings = &m_Frame->GetBoard()->GetDesignSettings();

    m_filletBitmap->SetBitmap( KiBitmapBundle( BITMAPS::zone_fillet, 24 ) );
    m_spokeBitmap->SetBitmap( KiBitmapBundle( BITMAPS::thermal_spokes, 24 ) );
    m_bitmapClearance->SetBitmap( KiBitmapBundle( BITMAPS::ps_diff_pair_gap, 24 ) );
    m_bitmapMinTrackWidth->SetBitmap( KiBitmapBundle( BITMAPS::width_track, 24 ) );
    m_bitmapMinConn->SetBitmap( KiBitmapBundle( BITMAPS::width_conn, 24 ) );
    m_bitmapMinViaAnnulus->SetBitmap( KiBitmapBundle( BITMAPS::via_annulus, 24 ) );
    m_bitmapMinViaDiameter->SetBitmap( KiBitmapBundle( BITMAPS::via_diameter, 24 ) );
    m_bitmapMinViaDrill->SetBitmap( KiBitmapBundle( BITMAPS::via_hole_diameter, 24 ) );
    m_bitmapMinuViaDiameter->SetBitmap( KiBitmapBundle( BITMAPS::via_diameter, 24 ) );
    m_bitmapMinuViaDrill->SetBitmap( KiBitmapBundle( BITMAPS::via_hole_diameter, 24 ) );
    m_bitmapHoleClearance->SetBitmap( KiBitmapBundle( BITMAPS::hole_to_copper_clearance, 24 ) );
    m_bitmapMinHoleClearance->SetBitmap( KiBitmapBundle( BITMAPS::hole_to_hole_clearance, 24 ) );
    m_bitmapEdgeClearance->SetBitmap( KiBitmapBundle( BITMAPS::edge_to_copper_clearance, 24 ) );

    m_stCircleToPolyWarning->SetFont( KIUI::GetSmallInfoFont( this ).Italic() );

    // The spoke count is a spin control, so it is not one of the bound fields below
    m_spokeCountPage = PANEL_SETUP_CONSTRAINT_RULES::PageEditingDefault( wxS( "min_resolved_spokes" ) );

    wxSize ctrlSize = m_minResolvedSpokeCountCtrl->GetSize();
    ctrlSize.x = KIUI::GetTextSize( wxT( "XXX" ), m_minResolvedSpokeCountCtrl ).x;

    // The link sits inside the field, and this one is otherwise only as wide as its count
    if( !m_spokeCountPage.IsEmpty() )
        ctrlSize.x = m_minResolvedSpokeCountCtrl->GetBestSize().x + FromDIP( 24 );

    m_minResolvedSpokeCountCtrl->SetMinSize( ctrlSize );
    m_minResolvedSpokeCountCtrl->SetSize( ctrlSize );

    if( !ADVANCED_CFG::GetCfg().m_EnableCreepageSlot )
    {
        m_bitmapMinGrooveWidth->Show( false );
        m_minGrooveWidthLabel->Show( false );
        m_minGrooveWidthCtrl->Show( false );
        m_minGrooveWidthUnits->Show( false );
    }

    using BDS = BOARD_DESIGN_SETTINGS;

    m_fields = {
        { wxS( "min_clearance" ), m_clearanceCtrl, &m_minClearance, &BDS::m_MinClearance },
        { wxS( "min_connection" ), m_MinConnCtrl, &m_minConn, &BDS::m_MinConn },
        { wxS( "min_track_width" ), m_TrackMinWidthCtrl, &m_trackMinWidth, &BDS::m_TrackMinWidth },
        { wxS( "min_via_annular_width" ), m_ViaMinAnnulusCtrl, &m_viaMinAnnulus, &BDS::m_ViasMinAnnularWidth },
        { wxS( "min_via_diameter" ), m_SetViasMinSizeCtrl, &m_viaMinSize, &BDS::m_ViasMinSize },
        { wxS( "min_through_hole_diameter" ), m_MinDrillCtrl, &m_throughHoleMin, &BDS::m_MinThroughDrill },
        { wxS( "min_microvia_diameter" ), m_uviaMinSizeCtrl, &m_uviaMinSize, &BDS::m_MicroViasMinSize },
        { wxS( "min_microvia_drill" ), m_uviaMinDrillCtrl, &m_uviaMinDrill, &BDS::m_MicroViasMinDrill },
        { wxS( "min_hole_to_hole" ), m_SetHoleToHoleCtrl, &m_holeToHoleMin, &BDS::m_HoleToHoleMin },
        { wxS( "min_hole_clearance" ), m_HoleClearanceCtrl, &m_holeClearance, &BDS::m_HoleClearance },
        { wxS( "min_silk_clearance" ), m_silkClearanceCtrl, &m_silkClearance, &BDS::m_SilkClearance },
        { wxS( "min_groove_width" ), m_minGrooveWidthCtrl, &m_minGrooveWidth, &BDS::m_MinGrooveWidth },
        { wxS( "min_text_height" ), m_textHeightCtrl, &m_minTextHeight, &BDS::m_MinSilkTextHeight },
        { wxS( "min_text_thickness" ), m_textThicknessCtrl, &m_minTextThickness, &BDS::m_MinSilkTextThickness },
        { wxS( "min_copper_edge_clearance" ), m_EdgeClearanceCtrl, &m_edgeClearance, &BDS::m_CopperEdgeClearance },
        { wxS( "max_error" ), m_maxErrorCtrl, &m_maxError, &BDS::m_MaxError }
    };

    for( CONSTRAINT_FIELD& field : m_fields )
    {
        field.m_Warning = new FIELD_WARNING_ICON( field.m_Ctrl );
        field.m_AlsoOn = PANEL_SETUP_CONSTRAINT_RULES::PageEditingDefault( field.m_Setting );

        // The value can be set here or with the rules that are read against it, so the field
        // stays live and only carries a way to reach the other page
        if( !field.m_AlsoOn.IsEmpty() )
            PANEL_SETUP_CONSTRAINT_RULES::LinkToPage( field.m_Ctrl, field.m_AlsoOn );

        // Debounced so a half-typed value does not flash a warning
        field.m_Ctrl->Bind( wxEVT_TEXT,
                            [this]( wxCommandEvent& aEvent )
                            {
                                m_validationTimer.StartOnce( 250 );
                                aEvent.Skip();
                            } );
    }

    if( !m_spokeCountPage.IsEmpty() )
        PANEL_SETUP_CONSTRAINT_RULES::LinkToPage( m_minResolvedSpokeCountCtrl, m_spokeCountPage, 0 );

    Bind( wxEVT_TIMER,
          [this]( wxTimerEvent& )
          {
              updateWarnings();
          },
          m_validationTimer.GetId() );
}


bool PANEL_SETUP_CONSTRAINTS::TransferDataToWindow()
{
    wxString msg;
    msg.Printf( m_stCircleToPolyWarning->GetLabel(), m_Frame->StringFromValue( ARC_HIGH_DEF, true ) );
    m_stCircleToPolyWarning->SetLabel( msg );

    m_useHeightForLengthCalcs->SetValue( m_BrdSettings->m_UseHeightForLengthCalcs );
    m_allowExternalFilletsOpt->SetValue( m_BrdSettings->m_ZoneKeepExternalFillets );

    // Every bound field is read there, which also records what was read
    RefreshDefaults();

    return true;
}


bool PANEL_SETUP_CONSTRAINTS::TransferDataFromWindow()
{
    // These are all stored in project file, not board, so no need for OnModify()

    BOARD_DESIGN_SETTINGS temp( *m_BrdSettings );

    applyValues( temp );

    std::map<wxString, wxWindow*> fieldToControl = { { wxS( "min_resolved_spokes" ), m_minResolvedSpokeCountCtrl } };

    for( const CONSTRAINT_FIELD& field : m_fields )
        fieldToControl[field.m_Setting] = field.m_Ctrl;

    for( const BOARD_DESIGN_SETTINGS::VALIDATION_ERROR& error : temp.ValidateDesignRules( m_Frame->GetUserUnits() ) )
    {
        auto it = fieldToControl.find( error.setting_name );

        if( it == fieldToControl.end() )
            continue;

        PAGED_DIALOG::GetDialog( this )->SetError( error.error_message, this, it->second );
        return false;
    }

    m_BrdSettings->m_UseHeightForLengthCalcs = m_useHeightForLengthCalcs->GetValue();
    m_BrdSettings->m_ZoneKeepExternalFillets = m_allowExternalFilletsOpt->GetValue();

    // A constraint can also be set with the rules read against it, so a field the user did not
    // touch here must not write back over an edit made on that page
    if( m_minResolvedSpokeCountCtrl->GetValue() != m_spokeCountLoaded )
    {
        m_spokeCountLoaded = m_minResolvedSpokeCountCtrl->GetValue();
        m_BrdSettings->m_MinResolvedSpokes = m_spokeCountLoaded;
    }

    for( CONSTRAINT_FIELD& field : m_fields )
    {
        int value = field.m_Binder->GetValue();

        if( value != field.m_Loaded )
        {
            field.m_Loaded = value;
            m_BrdSettings->*( field.m_Member ) = value;
        }
    }

    return true;
}


void PANEL_SETUP_CONSTRAINTS::applyValues( BOARD_DESIGN_SETTINGS& aSettings ) const
{
    aSettings.m_UseHeightForLengthCalcs = m_useHeightForLengthCalcs->GetValue();
    aSettings.m_ZoneKeepExternalFillets = m_allowExternalFilletsOpt->GetValue();

    aSettings.m_MinResolvedSpokes = m_minResolvedSpokeCountCtrl->GetValue();

    for( const CONSTRAINT_FIELD& field : m_fields )
        aSettings.*( field.m_Member ) = field.m_Binder->GetValue();
}


void PANEL_SETUP_CONSTRAINTS::RefreshDefaults()
{
    for( CONSTRAINT_FIELD& field : m_fields )
    {
        field.m_Loaded = m_BrdSettings->*( field.m_Member );
        field.m_Binder->SetValue( field.m_Loaded );
    }

    m_spokeCountLoaded = m_BrdSettings->m_MinResolvedSpokes;
    m_minResolvedSpokeCountCtrl->SetValue( m_spokeCountLoaded );

    updateWarnings();
}


void PANEL_SETUP_CONSTRAINTS::updateWarnings()
{
    m_validationTimer.Stop();

    BOARD_DESIGN_SETTINGS candidate( *m_BrdSettings );
    applyValues( candidate );

    EDA_UNITS                    units = m_Frame->GetUserUnits();
    std::map<wxString, wxString> messages;

    for( const std::vector<BOARD_DESIGN_SETTINGS::VALIDATION_ERROR>& issues :
         { candidate.ValidateDesignRules( units ), candidate.GetDesignRuleWarnings( units ) } )
    {
        for( const BOARD_DESIGN_SETTINGS::VALIDATION_ERROR& issue : issues )
        {
            wxString& msg = messages[issue.setting_name];

            if( !msg.IsEmpty() )
                msg += wxS( "\n" );

            msg += issue.error_message;
        }
    }

    for( const CONSTRAINT_FIELD& field : m_fields )
    {
        auto it = messages.find( field.m_Setting );
        bool show = it != messages.end() && field.m_Ctrl->IsShown();

        field.m_Warning->SetWarning( show ? it->second : wxString() );
    }
}


void PANEL_SETUP_CONSTRAINTS::ImportSettingsFrom( BOARD* aBoard )
{
    BOARD_DESIGN_SETTINGS* savedSettings = m_BrdSettings;

    m_BrdSettings = &aBoard->GetDesignSettings();
    TransferDataToWindow();

    m_BrdSettings = savedSettings;
}
