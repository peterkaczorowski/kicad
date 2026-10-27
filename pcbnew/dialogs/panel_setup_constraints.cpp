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

#include <algorithm>
#include <functional>
#include <map>

#include <board_design_settings.h>
#include <board.h>
#include <math/util.h>
#include <panel_setup_constraints.h>
#include <panel_setup_constraints_base.h>
#include <pcb_edit_frame.h>
#include <widgets/field_warning_icon.h>
#include <dialogs/constraint_rules_list.h>
#include <dialogs/panel_setup_constraint_rules.h>
#include <drc/rule_editor/drc_rule_document.h>
#include <widgets/paged_dialog.h>
#include <wx/treebook.h>
#include <wx/hyperlink.h>
#include <wx/popupwin.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <bitmaps.h>
#include <advanced_config.h>
#include <netclass.h>
#include <project.h>
#include <project/net_settings.h>
#include <project/project_file.h>


namespace
{

/// The height a popup grows to before its list starts scrolling, before DPI scaling
constexpr int MAX_POPUP_HEIGHT = 360;


/**
 * The rules overriding one board constraint, each one a link that opens it in its editor.
 */
class OVERRIDING_RULES_POPUP : public wxPopupTransientWindow
{
public:
    OVERRIDING_RULES_POPUP( wxWindow* aParent ) :
            wxPopupTransientWindow( aParent, wxBORDER_SIMPLE )
    {
        m_panel = new wxPanel( this, wxID_ANY );
        m_panel->SetBackgroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW ) );

        m_scrolled = new wxScrolledWindow( m_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL );
        m_scrolled->SetScrollRate( 0, 10 );
        m_scrolled->SetBackgroundColour( m_panel->GetBackgroundColour() );

        m_rows = new wxBoxSizer( wxVERTICAL );
        m_scrolled->SetSizer( m_rows );

        m_outer = new wxBoxSizer( wxVERTICAL );
        m_outer->Add( m_scrolled, 1, wxEXPAND | wxALL, FromDIP( 8 ) );
        m_panel->SetSizer( m_outer );
    }

    void AddHeading( const wxString& aText )
    {
        wxStaticText* heading = new wxStaticText( m_scrolled, wxID_ANY, aText );

        heading->SetFont( heading->GetFont().Bold() );
        m_rows->Add( heading, 0, wxTOP, m_rows->GetItemCount() ? FromDIP( 10 ) : 0 );
    }

    void AddLine( const wxString& aText )
    {
        m_rows->Add( new wxStaticText( m_scrolled, wxID_ANY, aText ), 0, wxTOP, FromDIP( 3 ) );
    }

    void AddRule( const wxString& aName, const wxString& aDetail, std::function<void()> aOpen )
    {
        wxBoxSizer*      row = new wxBoxSizer( wxHORIZONTAL );
        wxHyperlinkCtrl* link = new wxHyperlinkCtrl( m_scrolled, wxID_ANY, aName, wxEmptyString );

        link->Bind( wxEVT_HYPERLINK,
                    [this, aOpen]( wxHyperlinkEvent& )
                    {
                        Dismiss();
                        aOpen();
                    } );

        row->Add( link, 0, wxALIGN_CENTER_VERTICAL );
        row->Add( new wxStaticText( m_scrolled, wxID_ANY, aDetail ), 0, wxALIGN_CENTER_VERTICAL | wxLEFT,
                  FromDIP( 8 ) );

        m_rows->Add( row, 0, wxTOP, FromDIP( 3 ) );
    }

    /// Destroyed on dismissal so repeated clicks do not leave hidden popups behind
    void OnDismiss() override { Destroy(); }

    void PopupUnder( wxWindow* aAnchor )
    {
        wxSize content = m_rows->GetMinSize();
        int    width = content.x;
        int    height = content.y;

        if( height > FromDIP( MAX_POPUP_HEIGHT ) )
        {
            height = FromDIP( MAX_POPUP_HEIGHT );
            width += wxSystemSettings::GetMetric( wxSYS_VSCROLL_X, m_scrolled );
        }

        m_scrolled->SetVirtualSize( content );
        m_scrolled->SetMinSize( wxSize( width, height ) );

        m_outer->Fit( m_panel );
        SetClientSize( m_panel->GetSize() );

        Position( aAnchor->GetScreenPosition(), wxSize( 0, aAnchor->GetSize().y ) );
        Popup();
    }

private:
    wxPanel*          m_panel;
    wxScrolledWindow* m_scrolled;
    wxBoxSizer*       m_rows;
    wxBoxSizer*       m_outer;
};


/**
 * Selects the tree page holding a constraint's rules and opens one of them in its editor.
 */
void openRuleInEditor( wxWindow* aContext, const wxString& aTitle, DRC_RULE_EDITOR_CONSTRAINT_NAME aType, int aIndex )
{
    PAGED_DIALOG* dialog = PAGED_DIALOG::GetDialog( aContext );

    if( !dialog )
        return;

    if( auto* page = dynamic_cast<PANEL_SETUP_CONSTRAINT_RULES*>( dialog->ShowPage( aTitle ) ) )
        page->ShowRule( aType, aIndex );
}

} // namespace


PANEL_SETUP_CONSTRAINTS::PANEL_SETUP_CONSTRAINTS( wxWindow* aParentWindow, PCB_EDIT_FRAME* aFrame,
                                                  DRC_RULE_DOCUMENT* aDocument ) :
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
        m_document( aDocument ),
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

    wxSize ctrlSize = m_minResolvedSpokeCountCtrl->GetSize();
    ctrlSize.x = KIUI::GetTextSize( wxT( "XXX" ), m_minResolvedSpokeCountCtrl ).x;
    m_minResolvedSpokeCountCtrl->SetSize( ctrlSize );

    if( !ADVANCED_CFG::GetCfg().m_EnableCreepageSlot )
    {
        m_bitmapMinGrooveWidth->Show( false );
        m_minGrooveWidthLabel->Show( false );
        m_minGrooveWidthCtrl->Show( false );
        m_minGrooveWidthUnits->Show( false );
    }

    m_fields = { { wxS( "min_clearance" ), m_clearanceCtrl },
                 { wxS( "min_connection" ), m_MinConnCtrl },
                 { wxS( "min_track_width" ), m_TrackMinWidthCtrl },
                 { wxS( "min_via_annular_width" ), m_ViaMinAnnulusCtrl },
                 { wxS( "min_via_diameter" ), m_SetViasMinSizeCtrl },
                 { wxS( "min_through_hole_diameter" ), m_MinDrillCtrl },
                 { wxS( "min_microvia_diameter" ), m_uviaMinSizeCtrl },
                 { wxS( "min_microvia_drill" ), m_uviaMinDrillCtrl },
                 { wxS( "min_hole_to_hole" ), m_SetHoleToHoleCtrl },
                 { wxS( "min_hole_clearance" ), m_HoleClearanceCtrl },
                 { wxS( "min_silk_clearance" ), m_silkClearanceCtrl },
                 { wxS( "min_groove_width" ), m_minGrooveWidthCtrl },
                 { wxS( "min_text_height" ), m_textHeightCtrl },
                 { wxS( "min_text_thickness" ), m_textThicknessCtrl },
                 { wxS( "min_copper_edge_clearance" ), m_EdgeClearanceCtrl },
                 { wxS( "max_error" ), m_maxErrorCtrl } };

    for( CONSTRAINT_FIELD& field : m_fields )
    {
        field.m_Warning = new FIELD_WARNING_ICON( field.m_Ctrl );

        if( m_document && PANEL_SETUP_CONSTRAINT_RULES::RulesCanOverride( field.m_Setting, nullptr, nullptr ) )
        {
            field.m_Override = new FIELD_WARNING_ICON( field.m_Ctrl, BITMAPS::small_new_window, 1 );
            field.m_Override->SetCursor( wxCURSOR_HAND );
            field.m_Override->Bind( wxEVT_LEFT_DOWN,
                                    [this, setting = field.m_Setting, icon = field.m_Override]( wxMouseEvent& )
                                    {
                                        showOverridingRules( setting, icon );
                                    } );
        }

        // Debounced so a half-typed value does not flash a warning
        field.m_Ctrl->Bind( wxEVT_TEXT,
                            [this]( wxCommandEvent& aEvent )
                            {
                                m_validationTimer.StartOnce( 250 );
                                aEvent.Skip();
                            } );
    }

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

    m_maxError.SetValue( m_BrdSettings->m_MaxError );

    m_allowExternalFilletsOpt->SetValue( m_BrdSettings->m_ZoneKeepExternalFillets );
    m_minResolvedSpokeCountCtrl->SetValue( m_BrdSettings->m_MinResolvedSpokes );

    m_minClearance.SetValue( m_BrdSettings->m_MinClearance );
    m_minConn.SetValue( m_BrdSettings->m_MinConn );
    m_trackMinWidth.SetValue( m_BrdSettings->m_TrackMinWidth );
    m_viaMinAnnulus.SetValue( m_BrdSettings->m_ViasMinAnnularWidth );
    m_viaMinSize.SetValue(m_BrdSettings->m_ViasMinSize );
    m_holeClearance.SetValue( m_BrdSettings->m_HoleClearance );
    m_edgeClearance.SetValue( m_BrdSettings->m_CopperEdgeClearance );
    m_minGrooveWidth.SetValue( m_BrdSettings->m_MinGrooveWidth );

    m_throughHoleMin.SetValue( m_BrdSettings->m_MinThroughDrill );
    m_holeToHoleMin.SetValue( m_BrdSettings->m_HoleToHoleMin );

    m_uviaMinSize.SetValue( m_BrdSettings->m_MicroViasMinSize );
    m_uviaMinDrill.SetValue( m_BrdSettings->m_MicroViasMinDrill );

    m_silkClearance.SetValue( m_BrdSettings->m_SilkClearance );
    m_minTextHeight.SetValue( m_BrdSettings->m_MinSilkTextHeight );
    m_minTextThickness.SetValue( m_BrdSettings->m_MinSilkTextThickness );

    updateWarnings();
    RefreshOverrides();

    return true;
}


bool PANEL_SETUP_CONSTRAINTS::TransferDataFromWindow()
{
    // These are all stored in project file, not board, so no need for OnModify()

    BOARD_DESIGN_SETTINGS temp( *m_BrdSettings );
    applyValues( temp );

    std::vector<BOARD_DESIGN_SETTINGS::VALIDATION_ERROR> errors = temp.ValidateDesignRules( m_Frame->GetUserUnits() );

    if( !errors.empty() )
    {
        const BOARD_DESIGN_SETTINGS::VALIDATION_ERROR& error = errors.front();

        const std::map<wxString, wxWindow*> fieldToControl = {
            { wxS( "min_clearance" ),             m_clearanceCtrl },
            { wxS( "min_connection" ),            m_MinConnCtrl },
            { wxS( "min_track_width" ),           m_TrackMinWidthCtrl },
            { wxS( "min_via_annular_width" ),     m_ViaMinAnnulusCtrl },
            { wxS( "min_via_diameter" ),          m_SetViasMinSizeCtrl },
            { wxS( "min_through_hole_diameter" ), m_MinDrillCtrl },
            { wxS( "min_microvia_diameter" ),     m_uviaMinSizeCtrl },
            { wxS( "min_microvia_drill" ),        m_uviaMinDrillCtrl },
            { wxS( "min_hole_to_hole" ),          m_SetHoleToHoleCtrl },
            { wxS( "min_hole_clearance" ),        m_HoleClearanceCtrl },
            { wxS( "min_silk_clearance" ),        m_silkClearanceCtrl },
            { wxS( "min_groove_width" ),          m_minGrooveWidthCtrl },
            { wxS( "min_text_height" ),           m_textHeightCtrl },
            { wxS( "min_text_thickness" ),        m_textThicknessCtrl },
            { wxS( "min_copper_edge_clearance" ), m_EdgeClearanceCtrl },
            { wxS( "max_error" ),                 m_maxErrorCtrl },
            { wxS( "min_resolved_spokes" ),       m_minResolvedSpokeCountCtrl }
        };

        wxWindow* control = nullptr;

        if( auto it = fieldToControl.find( error.setting_name ); it != fieldToControl.end() )
            control = it->second;

        PAGED_DIALOG::GetDialog( this )->SetError( error.error_message, this, control ? control : this );
        return false;
    }

    applyValues( *m_BrdSettings );
    return true;
}


void PANEL_SETUP_CONSTRAINTS::applyValues( BOARD_DESIGN_SETTINGS& aSettings ) const
{
    aSettings.m_UseHeightForLengthCalcs = m_useHeightForLengthCalcs->GetValue();

    aSettings.m_MaxError = m_maxError.GetValue();

    aSettings.m_ZoneKeepExternalFillets = m_allowExternalFilletsOpt->GetValue();
    aSettings.m_MinResolvedSpokes = m_minResolvedSpokeCountCtrl->GetValue();

    aSettings.m_MinClearance = m_minClearance.GetValue();
    aSettings.m_MinConn = m_minConn.GetValue();
    aSettings.m_TrackMinWidth = m_trackMinWidth.GetValue();
    aSettings.m_ViasMinAnnularWidth = m_viaMinAnnulus.GetValue();
    aSettings.m_ViasMinSize = m_viaMinSize.GetValue();
    aSettings.m_HoleClearance = m_holeClearance.GetValue();
    aSettings.m_CopperEdgeClearance = m_edgeClearance.GetValue();
    aSettings.m_MinGrooveWidth = m_minGrooveWidth.GetValue();

    aSettings.m_MinThroughDrill = m_throughHoleMin.GetValue();
    aSettings.m_HoleToHoleMin = m_holeToHoleMin.GetValue();

    aSettings.m_MicroViasMinSize = m_uviaMinSize.GetValue();
    aSettings.m_MicroViasMinDrill = m_uviaMinDrill.GetValue();

    aSettings.m_SilkClearance = m_silkClearance.GetValue();
    aSettings.m_MinSilkTextHeight = m_minTextHeight.GetValue();
    aSettings.m_MinSilkTextThickness = m_minTextThickness.GetValue();
}


void PANEL_SETUP_CONSTRAINTS::RefreshOverrides()
{
    if( !m_document )
        return;

    for( CONSTRAINT_FIELD& field : m_fields )
    {
        if( !field.m_Override )
            continue;

        DRC_RULE_EDITOR_CONSTRAINT_NAME type;

        if( !PANEL_SETUP_CONSTRAINT_RULES::RulesCanOverride( field.m_Setting, &type, nullptr ) )
            continue;

        const std::vector<int> indices = m_document->IndicesFor( type );

        if( !indices.empty() )
        {
            field.m_Override->SetWarning(
                    wxString::Format( wxPLURAL( "%zu rule overrides this. Click to see it.",
                                                "%zu rules override this. Click to see them.", indices.size() ),
                                      indices.size() ) );
        }
        else if( !PANEL_SETUP_CONSTRAINT_RULES::NetclassValuesFor( field.m_Setting,
                                                                   *m_Frame->Prj().GetProjectFile().NetSettings() )
                          .empty() )
        {
            field.m_Override->SetWarning( _( "Net classes set their own value for this. Click to see them." ) );
        }
        else
        {
            field.m_Override->SetWarning( wxEmptyString );
        }
    }
}


void PANEL_SETUP_CONSTRAINTS::showOverridingRules( const wxString& aSetting, wxWindow* aAnchor )
{
    DRC_RULE_EDITOR_CONSTRAINT_NAME type;
    wxString                        page;

    if( !m_document || !PANEL_SETUP_CONSTRAINT_RULES::RulesCanOverride( aSetting, &type, &page ) )
        return;

    const std::vector<int>                     indices = m_document->IndicesFor( type );
    const std::map<int, std::vector<wxString>> netclasses =
            PANEL_SETUP_CONSTRAINT_RULES::NetclassValuesFor( aSetting, *m_Frame->Prj().GetProjectFile().NetSettings() );

    if( indices.empty() && netclasses.empty() )
        return;

    OVERRIDING_RULES_POPUP* popup = new OVERRIDING_RULES_POPUP( this );

    if( !indices.empty() )
        popup->AddHeading( _( "Overridden by user rules" ) );

    for( int index : indices )
    {
        const DRC_RE_LOADED_PANEL_ENTRY& entry = m_document->Entries()[index];
        wxString                         value = CONSTRAINT_RULES_LIST::ConstraintText( entry, type, m_Frame );
        wxString                         detail;

        if( entry.condition.IsEmpty() && entry.layerSource.IsEmpty() )
            detail = wxString::Format( _( "%s, everywhere" ), value );
        else if( entry.condition.IsEmpty() )
            detail = wxString::Format( _( "%s, on %s" ), value, entry.layerSource );
        else if( entry.layerSource.IsEmpty() )
            detail = wxString::Format( _( "%s, where %s" ), value, entry.condition );
        else
            detail = wxString::Format( _( "%s, on %s where %s" ), value, entry.layerSource, entry.condition );

        popup->AddRule( entry.ruleName, detail,
                        [this, page, type, index]()
                        {
                            openRuleInEditor( this, page, type, index );
                        } );
    }

    // A net class sets its own value rather than displacing the board one, which stays the
    // minimum, so these are listed apart from the rules and without links to a page that edits
    // them here.
    if( !netclasses.empty() )
    {
        popup->AddHeading( _( "Net class values" ) );

        for( const auto& [value, names] : netclasses )
        {
            wxString classes;

            for( const wxString& name : names )
                classes << ( classes.IsEmpty() ? wxS( "" ) : wxS( ", " ) ) << name;

            popup->AddLine( wxString::Format( wxS( "%s: %s" ), m_Frame->StringFromValue( value, true ), classes ) );
        }
    }

    popup->PopupUnder( aAnchor );
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
