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

#include <dialogs/dialog_workspace_config.h>

#include <build_version.h>
#include <pgm_base.h>
#include <settings/common_settings.h>
#include <mdw/mdw_framework.h>
#include <mdw/mdw_backend.h>
#include <mdw/mdw_backend_registry.h>
#include <mdw/mdw_credential_store.h>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>


namespace
{
constexpr int TEST_TIMEOUT_MS = 8000;
}


DIALOG_WORKSPACE_CONFIG::DIALOG_WORKSPACE_CONFIG( wxWindow* aParent, MDW_FRAMEWORK* aFramework,
                                                  const wxString&            aProjectPath,
                                                  const MDW_PROJECT_BINDING& aBinding ) :
        DIALOG_SHIM( aParent, wxID_ANY, _( "Configure Design Workspace" ), wxDefaultPosition,
                     wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_framework( aFramework ),
        m_projectPath( aProjectPath ),
        m_binding( aBinding )
{
    wxBoxSizer* topSizer = new wxBoxSizer( wxVERTICAL );

    wxFlexGridSizer* grid = new wxFlexGridSizer( 0, 2, 5, 8 );
    grid->AddGrowableCol( 1 );
    grid->SetFlexibleDirection( wxHORIZONTAL );

    auto addRow =
            [&]( const wxString& aLabel, wxWindow* aControl )
            {
                grid->Add( new wxStaticText( this, wxID_ANY, aLabel ), 0,
                           wxALIGN_CENTER_VERTICAL );
                grid->Add( aControl, 1, wxEXPAND );
            };

    m_backendChoice = new wxChoice( this, wxID_ANY );
    addRow( _( "Backend:" ), m_backendChoice );

    m_workspaceIdCtrl = new wxTextCtrl( this, wxID_ANY );
    addRow( _( "Workspace ID:" ), m_workspaceIdCtrl );

    m_bindingCtrl = new wxTextCtrl( this, wxID_ANY );
    addRow( _( "Binding:" ), m_bindingCtrl );

    m_usernameCtrl = new wxTextCtrl( this, wxID_ANY );
    addRow( _( "Username:" ), m_usernameCtrl );

    m_passwordCtrl = new wxTextCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                     wxDefaultSize, wxTE_PASSWORD );
    addRow( _( "Password:" ), m_passwordCtrl );

    topSizer->Add( grid, 0, wxEXPAND | wxALL, 10 );

    wxBoxSizer* testRow = new wxBoxSizer( wxHORIZONTAL );
    wxButton*   testButton = new wxButton( this, wxID_ANY, _( "Test Connection" ) );
    m_statusText = new wxStaticText( this, wxID_ANY, wxEmptyString );
    testRow->Add( testButton, 0, wxRIGHT, 8 );
    testRow->Add( m_statusText, 1, wxALIGN_CENTER_VERTICAL );
    topSizer->Add( testRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10 );

    topSizer->Add( new wxStaticLine( this ), 0, wxEXPAND | wxLEFT | wxRIGHT, 10 );

    wxStdDialogButtonSizer* buttons = new wxStdDialogButtonSizer();
    buttons->AddButton( new wxButton( this, wxID_OK ) );
    buttons->AddButton( new wxButton( this, wxID_CANCEL ) );
    buttons->Realize();
    topSizer->Add( buttons, 0, wxEXPAND | wxALL, 10 );

    SetSizer( topSizer );

    testButton->Bind( wxEVT_BUTTON, &DIALOG_WORKSPACE_CONFIG::onTestConnection, this );

    finishDialogSettings();
}


bool DIALOG_WORKSPACE_CONFIG::TransferDataToWindow()
{
    m_backendChoice->Clear();
    m_backendIds.clear();

    if( m_framework )
    {
        for( const MDW_BACKEND_MANIFEST& manifest : m_framework->Registry().Manifests() )
        {
            m_backendChoice->Append( manifest.display_name.IsEmpty() ? manifest.backend_id
                                                                     : manifest.display_name );
            m_backendIds.push_back( manifest.backend_id );
        }
    }

    // An unbound project has no backend to preselect, so fall back to the
    // configured default before the binding, when present, overrides it.
    const wxString preferred = m_binding.backend_id.IsEmpty()
                                       ? Pgm().GetCommonSettings()->m_Workspace.default_backend_id
                                       : m_binding.backend_id;

    int selected = 0;

    for( size_t ii = 0; ii < m_backendIds.size(); ++ii )
    {
        if( m_backendIds[ii] == preferred )
            selected = static_cast<int>( ii );
    }

    if( !m_backendIds.empty() )
        m_backendChoice->SetSelection( selected );

    m_workspaceIdCtrl->SetValue( m_binding.workspace_id );
    m_bindingCtrl->SetValue( m_binding.binding );

    // Credentials already saved for this binding belong in the fields that wrote
    // them, or reconfiguring a bound workspace looks like it has none.
    if( m_framework && m_binding.IsBound() )
    {
        MDW_CREDENTIAL_STORE& store = m_framework->CredentialStore();

        wxString username;
        wxString password;

        if( store.Load( m_binding.backend_id, m_binding.workspace_id, wxS( "username" ), username )
                    .IsOk() )
        {
            m_usernameCtrl->SetValue( username );
        }

        if( store.Load( m_binding.backend_id, m_binding.workspace_id, wxS( "password" ), password )
                    .IsOk() )
        {
            m_passwordCtrl->SetValue( password );
        }
    }

    return true;
}


MDW_PROJECT_BINDING DIALOG_WORKSPACE_CONFIG::collectBinding()
{
    MDW_PROJECT_BINDING binding;

    int sel = m_backendChoice->GetSelection();

    if( sel >= 0 && sel < static_cast<int>( m_backendIds.size() ) )
        binding.backend_id = m_backendIds[sel];

    binding.workspace_id = m_workspaceIdCtrl->GetValue();
    binding.binding = m_bindingCtrl->GetValue();

    if( m_framework && binding.IsBound() )
    {
        MDW_CREDENTIAL_STORE& store = m_framework->CredentialStore();

        if( !m_usernameCtrl->GetValue().IsEmpty() )
            store.Store( binding.backend_id, binding.workspace_id, wxS( "username" ),
                         m_usernameCtrl->GetValue() );

        if( !m_passwordCtrl->GetValue().IsEmpty() )
            store.Store( binding.backend_id, binding.workspace_id, wxS( "password" ),
                         m_passwordCtrl->GetValue() );
    }

    return binding;
}


bool DIALOG_WORKSPACE_CONFIG::TransferDataFromWindow()
{
    m_result = collectBinding();

    if( !m_result.IsBound() )
    {
        m_statusText->SetLabel( _( "Select a backend to bind this project." ) );
        return false;
    }

    return true;
}


void DIALOG_WORKSPACE_CONFIG::onTestConnection( wxCommandEvent& aEvent )
{
    if( !m_framework )
        return;

    MDW_PROJECT_BINDING binding = collectBinding();

    if( !binding.IsBound() )
    {
        m_statusText->SetLabel( _( "Select a backend first." ) );
        return;
    }

    m_statusText->SetLabel( _( "Connecting..." ) );
    m_statusText->Update();

    // Remember whether a real session was already open so the test does not tear
    // down a session it did not create.
    bool         preexisting = m_framework->Session( m_projectPath ) != nullptr;
    MDW_SESSION* session = nullptr;

    {
        wxBusyCursor busy;
        session = m_framework->OpenSession( m_projectPath, binding, GetBuildVersion(),
                                            TEST_TIMEOUT_MS );
    }

    if( session && session->Backend() )
    {
        m_statusText->SetLabel(
                wxString::Format( _( "Connected to %s." ),
                                  session->Backend()->Info().display_name ) );
    }
    else
    {
        m_statusText->SetLabel( _( "Connection failed." ) );
    }

    // Only close a session this test opened; leave any pre-existing one intact.
    if( !preexisting )
        m_framework->CloseSession( m_projectPath );
}
