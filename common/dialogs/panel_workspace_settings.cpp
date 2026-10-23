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

#include <dialogs/panel_workspace_settings.h>

#include <pgm_base.h>
#include <settings/common_settings.h>
#include <mdw/mdw_framework.h>
#include <mdw/mdw_backend_registry.h>
#include <widgets/ui_common.h>

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/sizer.h>
#include <wx/stattext.h>


PANEL_WORKSPACE_SETTINGS::PANEL_WORKSPACE_SETTINGS( wxWindow* aParent ) :
        RESETTABLE_PANEL( aParent ),
        m_enableIntegration( nullptr ),
        m_defaultBackend( nullptr )
{
    const int margin = FromDIP( KIUI::GetStdMargin() );
    const int panelMargin = 2 * margin;

    wxBoxSizer* topSizer = new wxBoxSizer( wxVERTICAL );

    wxStaticText* intro =
            new wxStaticText( this, wxID_ANY,
                              _( "A Managed Design Workspace binds a project to a backend such as a "
                                 "Git repository or a PLM system, and surfaces its lock, revision "
                                 "and lifecycle state inside KiCad." ) );
    intro->Wrap( FromDIP( 640 ) );
    topSizer->Add( intro, 0, wxALL, panelMargin );

    m_enableIntegration =
            new wxCheckBox( this, wxID_ANY, _( "Enable workspace integration" ) );
    topSizer->Add( m_enableIntegration, 0, wxLEFT | wxRIGHT | wxTOP, panelMargin );

    wxBoxSizer* backendSizer = new wxBoxSizer( wxHORIZONTAL );

    wxStaticText* backendLabel = new wxStaticText( this, wxID_ANY, _( "Default backend:" ) );
    backendSizer->Add( backendLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, margin );

    m_defaultBackend = new wxChoice( this, wxID_ANY );
    backendSizer->Add( m_defaultBackend, 1, wxALIGN_CENTER_VERTICAL, 0 );

    topSizer->Add( backendSizer, 0, wxALL | wxEXPAND, panelMargin );

    SetSizer( topSizer );
    Layout();
}


void PANEL_WORKSPACE_SETTINGS::loadBackendChoices()
{
    m_defaultBackend->Clear();
    m_defaultBackend->Append( _( "(none)" ), new wxStringClientData( wxEmptyString ) );

    if( MDW_FRAMEWORK* framework = Pgm().MdwFrameworkOrNull() )
    {
        for( const MDW_BACKEND_MANIFEST& manifest : framework->Registry().Manifests() )
        {
            m_defaultBackend->Append( manifest.display_name,
                                      new wxStringClientData( manifest.backend_id ) );
        }
    }

    m_defaultBackend->SetSelection( 0 );
}


bool PANEL_WORKSPACE_SETTINGS::TransferDataToWindow()
{
    COMMON_SETTINGS* settings = Pgm().GetCommonSettings();

    loadBackendChoices();

    m_enableIntegration->SetValue( settings->m_Workspace.enable_integration );

    for( unsigned i = 0; i < m_defaultBackend->GetCount(); ++i )
    {
        auto* data = static_cast<wxStringClientData*>( m_defaultBackend->GetClientObject( i ) );

        if( data && data->GetData() == settings->m_Workspace.default_backend_id )
        {
            m_defaultBackend->SetSelection( i );
            break;
        }
    }

    return true;
}


bool PANEL_WORKSPACE_SETTINGS::TransferDataFromWindow()
{
    COMMON_SETTINGS* settings = Pgm().GetCommonSettings();

    settings->m_Workspace.enable_integration = m_enableIntegration->GetValue();

    int selection = m_defaultBackend->GetSelection();

    if( selection != wxNOT_FOUND )
    {
        auto* data = static_cast<wxStringClientData*>( m_defaultBackend->GetClientObject( selection ) );
        settings->m_Workspace.default_backend_id = data ? data->GetData() : wxString();
    }

    return true;
}


void PANEL_WORKSPACE_SETTINGS::ResetPanel()
{
    m_enableIntegration->SetValue( false );

    loadBackendChoices();
}
