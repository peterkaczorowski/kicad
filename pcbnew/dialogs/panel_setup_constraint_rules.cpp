/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "panel_setup_constraint_rules.h"

#include <map>

#include <drc/drc_constraint_types.h>

#include <board.h>
#include <board_design_settings.h>
#include <netclass.h>
#include <pcb_edit_frame.h>
#include <project.h>
#include <project/net_settings.h>
#include <project/project_file.h>
#include <bitmaps.h>
#include <widgets/field_warning_icon.h>
#include <widgets/paged_dialog.h>
#include <wx/hyperlink.h>
#include <widgets/ui_common.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include "constraint_rules_list.h"


PANEL_SETUP_CONSTRAINT_RULES::PANEL_SETUP_CONSTRAINT_RULES( wxWindow* aParent, PCB_EDIT_FRAME* aFrame,
                                                            DRC_RULE_DOCUMENT*        aDocument,
                                                            const CONSTRAINT_SECTION& aSection ) :
        wxPanel( aParent, wxID_ANY ),
        m_frame( aFrame ),
        m_settings( &aFrame->GetBoard()->GetDesignSettings() ),
        m_document( aDocument ),
        m_section( aSection ),
        m_note( nullptr ),
        m_validationTimer( this ),
        m_list( nullptr )
{
    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    m_scrolled = new wxScrolledWindow( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL );
    m_scrolled->SetScrollRate( 0, 10 );
    m_scrolled->SetSizer( new wxBoxSizer( wxVERTICAL ) );

    // The note describes the rule list, so it belongs wherever there is one. Most pages carry
    // rules without carrying a board default.
    if( m_section.ruleType )
    {
        m_noteText = _( "The rules below are read from top to bottom and the last one that matches "
                        "decides the value. A value a net class sets is read before any of them." );

        m_note = new wxStaticText( m_scrolled, wxID_ANY, m_noteText );
        m_note->SetFont( KIUI::GetInfoFont( this ).Italic() );
        m_scrolled->GetSizer()->Add( m_note, 0, wxALL, 8 );
    }

    buildSection();

    mainSizer->Add( m_scrolled, 1, wxEXPAND );
    SetSizer( mainSizer );

    m_scrolled->Bind( wxEVT_SIZE,
                      [this]( wxSizeEvent& aEvent )
                      {
                          wrapLabels();
                          aEvent.Skip();
                      } );

    Bind(
            wxEVT_TIMER,
            [this]( wxTimerEvent& )
            {
                updateWarnings();
            },
            m_validationTimer.GetId() );
}


void PANEL_SETUP_CONSTRAINT_RULES::buildSection()
{
    wxSizer*      box = m_scrolled->GetSizer();
    wxWindow*     parent = m_scrolled;
    wxStaticText* heading = new wxStaticText( parent, wxID_ANY, m_section.title );
    wxBoxSizer*   settings = new wxBoxSizer( wxVERTICAL );

    heading->SetFont( heading->GetFont().Bold() );
    box->Add( heading, 0, wxLEFT | wxTOP | wxBOTTOM, 8 );
    box->Add( settings, 0, wxEXPAND | wxLEFT, 8 );

    if( !m_section.defaults.empty() )
    {
        wxFlexGridSizer* grid = new wxFlexGridSizer( 0, 3, 4, 8 );

        for( const CONSTRAINT_DEFAULT_FIELD& field : m_section.defaults )
        {
            wxStaticText* label = new wxStaticText( parent, wxID_ANY, field.label );
            wxTextCtrl*   value = new wxTextCtrl( parent, wxID_ANY );
            wxStaticText* units = new wxStaticText( parent, wxID_ANY, wxEmptyString );

            label->SetFont( KIUI::GetInfoFont( this ) );
            units->SetFont( KIUI::GetInfoFont( this ) );

            // Room for the longest value the field takes, in any of the units it can be shown in
            value->SetMinSize( wxSize( KIUI::GetTextSize( wxT( "XXXXXXXXXX" ), value ).x, -1 ) );

            grid->Add( label, 0, wxALIGN_CENTER_VERTICAL );
            grid->Add( value, 0, wxALIGN_CENTER_VERTICAL );
            grid->Add( units, 0, wxALIGN_CENTER_VERTICAL );

            DEFAULT_FIELD entry;
            entry.settingName = field.settingName;
            entry.member = field.member;
            entry.ctrl = value;
            entry.warning = new FIELD_WARNING_ICON( value );
            entry.binder = std::make_unique<UNIT_BINDER>( m_frame, label, value, units );
            entry.editedOn = field.editedOn;

            // UNIT_BINDER leaves an unscaled field alone through a change of units
            if( field.value == CONSTRAINT_VALUE::COUNT )
                entry.binder->SetUnits( EDA_UNITS::UNSCALED );

            if( entry.editedOn.IsEmpty() )
            {
                // Debounced so a half-typed value does not flash a warning
                value->Bind( wxEVT_TEXT,
                             [this]( wxCommandEvent& aEvent )
                             {
                                 m_validationTimer.StartOnce( 250 );
                                 aEvent.Skip();
                             } );
            }
            else
            {
                ShowEditedElsewhere( value, entry.editedOn );
            }

            m_defaults.push_back( std::move( entry ) );
        }

        settings->Add( grid, 0, wxEXPAND | wxALL, 5 );
    }

    if( m_section.netclassValue )
    {
        // The values themselves are listed with the rules they are read before
        wxBoxSizer*      row = new wxBoxSizer( wxHORIZONTAL );
        wxHyperlinkCtrl* link = new wxHyperlinkCtrl( parent, wxID_ANY, _( "Edit net classes" ), wxEmptyString );

        link->SetFont( KIUI::GetInfoFont( this ) );
        link->Bind( wxEVT_HYPERLINK,
                    [this]( wxHyperlinkEvent& )
                    {
                        showPage( _( "Net Classes" ) );
                    } );

        row->AddStretchSpacer();
        row->Add( link, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 12 );
        settings->Add( row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5 );
    }

    if( m_section.ruleType )
    {
        if( !m_section.defaults.empty() )
            box->Add( new wxStaticLine( parent ), 0, wxEXPAND | wxLEFT | wxRIGHT, 5 );

        CONSTRAINT_RULES_LIST* list =
                new CONSTRAINT_RULES_LIST( parent, m_frame, m_document, *m_section.ruleType, m_section.title );
        list->SetTitleWindow( heading );

        if( !m_section.defaults.empty() )
        {
            std::vector<std::pair<wxString, std::function<wxString()>>> defaults;

            for( size_t d = 0; d < m_section.defaults.size(); ++d )
            {
                // One default is named by its section; several each need their own name
                wxString name = m_section.title;

                if( m_section.defaults.size() > 1 )
                {
                    name = m_section.defaults[d].label;

                    if( name.EndsWith( wxS( ":" ) ) )
                        name.RemoveLast();
                }

                // Read on demand, since a note quoting a default is rebuilt as the default is typed
                defaults.emplace_back( name,
                                       [this, d]()
                                       {
                                           return m_frame->StringFromValue( m_defaults[d].binder->GetValue(), true );
                                       } );
            }

            list->SetDefaults( std::move( defaults ) );
        }

        if( m_section.netclassValue )
            list->SetNetclasses( m_section.netclassValue, m_section.netclassPart );

        list->SetChangeCallback(
                [this]()
                {
                    RefreshFromDocument();
                } );
        box->Add( list, 1, wxEXPAND | wxALL, 8 );
        m_list = list;
    }
}


void PANEL_SETUP_CONSTRAINT_RULES::showPage( const wxString& aTitle )
{
    if( PAGED_DIALOG* dialog = PAGED_DIALOG::GetDialog( this ) )
        dialog->ShowPage( aTitle );
}


void PANEL_SETUP_CONSTRAINT_RULES::wrapLabels()
{
    // The page has not been laid out yet while it is this narrow
    if( m_scrolled->GetClientSize().x - 180 < 200 )
        return;

    if( m_note )
    {
        m_note->SetLabel( m_noteText );
        m_note->Wrap( GetClientSize().x - 32 );
    }

    m_scrolled->Layout();
    m_scrolled->FitInside();
    m_scrolled->Layout();
}


void PANEL_SETUP_CONSTRAINT_RULES::RefreshDefaults()
{
    for( DEFAULT_FIELD& field : m_defaults )
        field.binder->SetValue( m_settings->*( field.member ) );

    updateWarnings();
}


bool PANEL_SETUP_CONSTRAINT_RULES::applyDefaults()
{
    if( m_defaults.empty() )
        return true;

    BOARD_DESIGN_SETTINGS candidate( *m_settings );

    writeDefaults( candidate );

    // A page reports only the values it edits; the rest are reported where they are edited.
    for( const BOARD_DESIGN_SETTINGS::VALIDATION_ERROR& error :
         candidate.ValidateDesignRules( m_frame->GetUserUnits() ) )
    {
        for( const DEFAULT_FIELD& field : m_defaults )
        {
            if( field.settingName != error.setting_name || !field.editedOn.IsEmpty() )
                continue;

            if( PAGED_DIALOG* dialog = PAGED_DIALOG::GetDialog( this ) )
                dialog->SetError( error.error_message, this, field.ctrl );

            return false;
        }
    }

    writeDefaults( *m_settings );

    return true;
}


void PANEL_SETUP_CONSTRAINT_RULES::writeDefaults( BOARD_DESIGN_SETTINGS& aSettings ) const
{
    for( const DEFAULT_FIELD& field : m_defaults )
    {
        if( field.editedOn.IsEmpty() )
            aSettings.*( field.member ) = field.binder->GetValue();
    }
}


void PANEL_SETUP_CONSTRAINT_RULES::updateWarnings()
{
    m_validationTimer.Stop();

    if( m_defaults.empty() )
        return;

    // A warning can weigh two defaults against each other, and the page holding the other one
    // wrote it to the board on its way out.
    BOARD_DESIGN_SETTINGS candidate( *m_settings );

    writeDefaults( candidate );

    EDA_UNITS                    units = m_frame->GetUserUnits();
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

    for( const DEFAULT_FIELD& field : m_defaults )
    {
        auto it = messages.find( field.settingName );

        field.warning->SetWarning( it != messages.end() ? it->second : wxString() );
    }

    // A note on an open rule quotes the default, so it is rebuilt as the default is typed.
    if( m_list )
        m_list->RefreshOverrideNote();
}


bool PANEL_SETUP_CONSTRAINT_RULES::confirmPendingRule()
{
    return !m_list || m_list->ConfirmPendingRule();
}


bool PANEL_SETUP_CONSTRAINT_RULES::Validate()
{
    return confirmPendingRule();
}


bool PANEL_SETUP_CONSTRAINT_RULES::TransferDataFromWindow()
{
    return applyDefaults() && confirmPendingRule();
}


bool PANEL_SETUP_CONSTRAINT_RULES::TransferDataToWindow()
{
    RefreshDefaults();
    RefreshFromDocument();
    return true;
}


void PANEL_SETUP_CONSTRAINT_RULES::RefreshFromDocument()
{
    if( m_list )
        m_list->RefreshRules();
}


std::vector<std::pair<wxString, CONSTRAINT_DEFAULT_FIELD>> PANEL_SETUP_CONSTRAINT_RULES::EditedDefaults()
{
    std::vector<std::pair<wxString, CONSTRAINT_DEFAULT_FIELD>> fields;

    for( FAMILY family : { FAMILY::CLEARANCE, FAMILY::TRACKS_AND_VIAS, FAMILY::HOLES, FAMILY::SILKSCREEN,
                           FAMILY::SOLDER_MASK, FAMILY::HIGH_SPEED, FAMILY::FOOTPRINT_RULES } )
    {
        for( const CONSTRAINT_SECTION& section : SectionsFor( family ) )
        {
            wxString group = section.settingsPage.IsEmpty() ? _( "Constraints" ) : section.settingsPage;

            for( const CONSTRAINT_DEFAULT_FIELD& field : section.defaults )
            {
                // A value another page edits is imported by that page, with the rest of its own
                if( field.editedOn.IsEmpty() )
                    fields.emplace_back( group, field );
            }
        }
    }

    return fields;
}


std::optional<CONSTRAINT_SECTION> PANEL_SETUP_CONSTRAINT_RULES::sectionForSetting( const wxString& aSetting )
{
    for( FAMILY family : { FAMILY::CLEARANCE, FAMILY::TRACKS_AND_VIAS, FAMILY::HOLES, FAMILY::SILKSCREEN,
                           FAMILY::SOLDER_MASK, FAMILY::HIGH_SPEED, FAMILY::FOOTPRINT_RULES } )
    {
        for( CONSTRAINT_SECTION& section : SectionsFor( family ) )
        {
            for( const CONSTRAINT_DEFAULT_FIELD& field : section.defaults )
            {
                if( field.settingName == aSetting )
                    return std::move( section );
            }
        }
    }

    return std::nullopt;
}


bool PANEL_SETUP_CONSTRAINT_RULES::RulesCanOverride( const wxString& aSetting, DRC_CONSTRAINT_T* aConstraint )
{
    static const std::map<wxString, DRC_CONSTRAINT_T> constraints = {
        { wxS( "min_clearance" ), CLEARANCE_CONSTRAINT },
        { wxS( "min_hole_clearance" ), HOLE_CLEARANCE_CONSTRAINT },
        { wxS( "min_copper_edge_clearance" ), EDGE_CLEARANCE_CONSTRAINT },
        { wxS( "min_track_width" ), TRACK_WIDTH_CONSTRAINT },
        { wxS( "min_connection" ), CONNECTION_WIDTH_CONSTRAINT },
        { wxS( "min_via_annular_width" ), ANNULAR_WIDTH_CONSTRAINT },
        { wxS( "min_via_diameter" ), VIA_DIAMETER_CONSTRAINT },
        { wxS( "min_through_hole_diameter" ), HOLE_SIZE_CONSTRAINT },
        { wxS( "min_hole_to_hole" ), HOLE_TO_HOLE_CONSTRAINT },
        { wxS( "min_silk_clearance" ), SILK_CLEARANCE_CONSTRAINT },
        { wxS( "min_text_height" ), TEXT_HEIGHT_CONSTRAINT },
        { wxS( "min_text_thickness" ), TEXT_THICKNESS_CONSTRAINT },
        { wxS( "solder_mask_expansion" ), SOLDER_MASK_EXPANSION_CONSTRAINT },
        { wxS( "solder_mask_min_width" ), SOLDER_MASK_SLIVER_CONSTRAINT },
        { wxS( "solder_paste_margin" ), SOLDER_PASTE_ABS_MARGIN_CONSTRAINT }
    };

    auto it = constraints.find( aSetting );

    if( it == constraints.end() )
        return false;

    if( aConstraint )
        *aConstraint = it->second;

    return true;
}


wxString PANEL_SETUP_CONSTRAINT_RULES::PageEditingDefault( const wxString& aSetting )
{
    std::optional<CONSTRAINT_SECTION> section = sectionForSetting( aSetting );

    if( !section )
        return wxString();

    for( const CONSTRAINT_DEFAULT_FIELD& field : section->defaults )
    {
        if( field.settingName == aSetting )
            return field.editedOn.IsEmpty() ? section->title : wxString();
    }

    return wxString();
}


void PANEL_SETUP_CONSTRAINT_RULES::ShowEditedElsewhere( wxWindow* aField, const wxString& aPage, int aSlot )
{
    // A background of its own is not enough: a dark theme draws a field and the panel behind
    // it alike, so the field is greyed instead
    aField->Disable();

    // The link is a sibling of the field rather than a child, so it stays live
    FIELD_WARNING_ICON* link = new FIELD_WARNING_ICON( aField, BITMAPS::small_new_window, aSlot );

    link->SetWarning( wxString::Format( _( "Set on the %s page. Click to open it." ), aPage ) );
    link->SetCursor( wxCURSOR_HAND );
    link->Bind( wxEVT_LEFT_DOWN,
                [aField, aPage]( wxMouseEvent& )
                {
                    if( PAGED_DIALOG* dialog = PAGED_DIALOG::GetDialog( aField ) )
                        dialog->ShowPage( aPage );
                } );
}


wxString PANEL_SETUP_CONSTRAINT_RULES::PageTitleFor( DRC_RULE_EDITOR_CONSTRAINT_NAME aType )
{
    for( FAMILY family : { FAMILY::CLEARANCE, FAMILY::TRACKS_AND_VIAS, FAMILY::HOLES, FAMILY::SILKSCREEN,
                           FAMILY::SOLDER_MASK, FAMILY::HIGH_SPEED, FAMILY::FOOTPRINT_RULES } )
    {
        for( const CONSTRAINT_SECTION& section : SectionsFor( family ) )
        {
            if( section.ruleType == aType )
                return section.title;
        }
    }

    return wxEmptyString;
}


std::vector<CONSTRAINT_SECTION> PANEL_SETUP_CONSTRAINT_RULES::SectionsFor( FAMILY aFamily )
{
    using BDS = BOARD_DESIGN_SETTINGS;

    auto clearance = []( const NETCLASS& aNetclass )
    {
        return aNetclass.GetClearanceOpt();
    };
    auto trackWidth = []( const NETCLASS& aNetclass )
    {
        return aNetclass.GetTrackWidthOpt();
    };
    auto viaDiameter = []( const NETCLASS& aNetclass )
    {
        return aNetclass.GetViaDiameterOpt();
    };
    auto viaDrill = []( const NETCLASS& aNetclass )
    {
        return aNetclass.GetViaDrillOpt();
    };

    switch( aFamily )
    {
    case FAMILY::CLEARANCE:
        return { { _( "Minimum clearance" ),
                   { { _( "Board default:" ), &BDS::m_MinClearance, wxS( "min_clearance" ) } },
                   MINIMUM_CLEARANCE,
                   clearance },
                 { _( "Copper to hole clearance" ),
                   { { _( "Board default:" ), &BDS::m_HoleClearance, wxS( "min_hole_clearance" ) } },
                   COPPER_TO_HOLE_CLEARANCE,
                   {} },
                 { _( "Copper to edge clearance" ),
                   { { _( "Board default:" ), &BDS::m_CopperEdgeClearance, wxS( "min_copper_edge_clearance" ) } },
                   COPPER_TO_EDGE_CLEARANCE,
                   {} },
                 { _( "Physical clearance" ), {}, PHYSICAL_CLEARANCE, {} },
                 { _( "Creepage distance" ), {}, CREEPAGE_DISTANCE, {} },
                 { _( "Minimum thermal relief spoke count" ),
                   { { _( "Board default:" ), &BDS::m_MinResolvedSpokes, wxS( "min_resolved_spokes" ), wxEmptyString,
                       CONSTRAINT_VALUE::COUNT } },
                   MINIMUM_THERMAL_RELIEF_SPOKE_COUNT,
                   {} } };

    case FAMILY::TRACKS_AND_VIAS:
        return { { _( "Minimum track width" ),
                   { { _( "Board default:" ), &BDS::m_TrackMinWidth, wxS( "min_track_width" ) } },
                   ROUTING_WIDTH,
                   trackWidth,
                   wxEmptyString,
                   NETCLASS_CONSTRAINT_PART::OPTIMUM },
                 { _( "Minimum connection width" ),
                   { { _( "Board default:" ), &BDS::m_MinConn, wxS( "min_connection" ) } },
                   MINIMUM_CONNECTION_WIDTH,
                   {} },
                 { _( "Minimum annular width" ),
                   { { _( "Board default:" ), &BDS::m_ViasMinAnnularWidth, wxS( "min_via_annular_width" ) } },
                   MINIMUM_ANNULAR_WIDTH,
                   {} },
                 { _( "Minimum via diameter" ),
                   { { _( "Board default:" ), &BDS::m_ViasMinSize, wxS( "min_via_diameter" ) },
                     { _( "Microvia default:" ), &BDS::m_MicroViasMinSize, wxS( "min_microvia_diameter" ) } },
                   MINIMUM_VIA_DIAMETER,
                   viaDiameter,
                   wxEmptyString,
                   NETCLASS_CONSTRAINT_PART::OPTIMUM },
                 { _( "Via style" ), {}, VIA_STYLE, {} },
                 { _( "Maximum via count" ), {}, MAXIMUM_VIA_COUNT, {} } };

    case FAMILY::HOLES:
        return { { _( "Minimum drill size" ),
                   { { _( "Board default:" ), &BDS::m_MinThroughDrill, wxS( "min_through_hole_diameter" ) },
                     { _( "Microvia default:" ), &BDS::m_MicroViasMinDrill, wxS( "min_microvia_drill" ) } },
                   MINIMUM_DRILL_SIZE,
                   viaDrill,
                   wxEmptyString,
                   NETCLASS_CONSTRAINT_PART::OPTIMUM },
                 { _( "Hole size" ), {}, HOLE_SIZE, {} },
                 { _( "Hole to hole clearance" ),
                   { { _( "Board default:" ), &BDS::m_HoleToHoleMin, wxS( "min_hole_to_hole" ) } },
                   HOLE_TO_HOLE_DISTANCE,
                   {} },
                 { _( "Microvia stack depth" ), {}, MICROVIA_STACK_DEPTH, {} },
                 { _( "Hole aspect ratio" ), {}, HOLE_ASPECT_RATIO, {} } };

    case FAMILY::SILKSCREEN:
        return { { _( "Silk to silk clearance" ),
                   { { _( "Board default:" ), &BDS::m_SilkClearance, wxS( "min_silk_clearance" ) } },
                   SILK_TO_SILK_CLEARANCE,
                   {} },
                 { _( "Minimum text height and thickness" ),
                   { { _( "Minimum text height:" ), &BDS::m_MinSilkTextHeight, wxS( "min_text_height" ) },
                     { _( "Minimum text thickness:" ), &BDS::m_MinSilkTextThickness, wxS( "min_text_thickness" ) } },
                   MINIMUM_TEXT_HEIGHT_AND_THICKNESS,
                   {} },
                 { _( "Silk to solder mask clearance" ), {}, SILK_TO_SOLDERMASK_CLEARANCE, {} } };

    case FAMILY::HIGH_SPEED:
        return { { _( "Matched length for differential pairs" ), {}, MATCHED_LENGTH_DIFF_PAIR, {} },
                 { _( "Absolute length" ), {}, ABSOLUTE_LENGTH, {} },
                 { _( "Differential pair routing" ), {}, ROUTING_DIFF_PAIR, {} } };

    case FAMILY::SOLDER_MASK:
        return { { _( "Solder mask expansion" ),
                   { { _( "Board default:" ), &BDS::m_SolderMaskExpansion, wxS( "solder_mask_expansion" ) } },
                   SOLDERMASK_EXPANSION,
                   {},
                   _( "Solder Mask/Paste" ) },
                 { _( "Minimum solder mask sliver" ),
                   { { _( "Board default:" ), &BDS::m_SolderMaskMinWidth, wxS( "solder_mask_min_width" ) } },
                   MINIMUM_SOLDERMASK_SLIVER,
                   {},
                   _( "Solder Mask/Paste" ) },
                 { _( "Solder paste expansion" ),
                   { { _( "Board default:" ), &BDS::m_SolderPasteMargin, wxS( "solder_paste_margin" ),
                       _( "Solder Mask/Paste" ) } },
                   SOLDERPASTE_EXPANSION,
                   {},
                   _( "Solder Mask/Paste" ) } };

    case FAMILY::FOOTPRINT_RULES:
        return { { _( "Permitted layers" ), {}, PERMITTED_LAYERS, {} },
                 { _( "Allowed orientation" ), {}, ALLOWED_ORIENTATION, {} },
                 { _( "Vias under SMD pads" ), {}, VIAS_UNDER_SMD, {} },
                 { _( "Courtyard clearance" ), {}, COURTYARD_CLEARANCE, {} } };
    }

    return {};
}
