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

#include <dialogs/dialog_workspace_history.h>

#include <mdw/mdw_framework.h>
#include <mdw/mdw_session.h>
#include <mdw/mdw_workspace_service.h>
#include <mdw/mdw_diff_presenter.h>

#include <wx/button.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>


namespace
{
/// Human-readable one-liner for a framework error code.
wxString describeError( MDW_ERROR_CODE aCode )
{
    switch( aCode )
    {
    case MDW_ERROR_CODE::NOT_SUPPORTED:      return _( "The backend does not support revisions." );
    case MDW_ERROR_CODE::NOT_AUTHENTICATED:  return _( "Not authenticated to the workspace." );
    case MDW_ERROR_CODE::PERMISSION_DENIED:  return _( "Permission denied." );
    case MDW_ERROR_CODE::NETWORK:            return _( "A network error occurred." );
    case MDW_ERROR_CODE::TIMEOUT:            return _( "The operation timed out." );
    case MDW_ERROR_CODE::ITEM_NOT_FOUND:     return _( "The item was not found." );
    case MDW_ERROR_CODE::REVISION_NOT_FOUND: return _( "The revision was not found." );
    case MDW_ERROR_CODE::BACKEND_LOST:       return _( "The workspace backend is not available." );
    default:                                 return _( "The workspace operation failed." );
    }
}
}


DIALOG_WORKSPACE_HISTORY::DIALOG_WORKSPACE_HISTORY( wxWindow* aParent, MDW_FRAMEWORK* aFramework,
                                                    const wxString& aProjectPath,
                                                    const wxString& aItemId ) :
        DIALOG_SHIM( aParent, wxID_ANY, _( "Revision History" ), wxDefaultPosition,
                     wxSize( 640, 420 ), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_framework( aFramework ),
        m_projectPath( aProjectPath ),
        m_itemId( aItemId ),
        m_session( nullptr ),
        m_svc( nullptr )
{
    m_session = m_framework ? m_framework->Session( m_projectPath ) : nullptr;
    m_svc = m_session ? m_session->Service() : nullptr;

    wxBoxSizer* topSizer = new wxBoxSizer( wxVERTICAL );

    m_list = new wxListCtrl( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT );
    m_list->AppendColumn( _( "Revision" ), wxLIST_FORMAT_LEFT, 110 );
    m_list->AppendColumn( _( "Author" ), wxLIST_FORMAT_LEFT, 120 );
    m_list->AppendColumn( _( "Date" ), wxLIST_FORMAT_LEFT, 140 );
    m_list->AppendColumn( _( "Comment" ), wxLIST_FORMAT_LEFT, 240 );
    topSizer->Add( m_list, 1, wxEXPAND | wxALL, 10 );

    wxBoxSizer* actionRow = new wxBoxSizer( wxHORIZONTAL );
    m_compareButton = new wxButton( this, wxID_ANY, _( "Compare" ) );
    m_statusText = new wxStaticText( this, wxID_ANY, wxEmptyString );
    actionRow->Add( m_compareButton, 0, wxRIGHT, 12 );
    actionRow->Add( m_statusText, 1, wxALIGN_CENTER_VERTICAL );
    topSizer->Add( actionRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10 );

    topSizer->Add( new wxStaticLine( this ), 0, wxEXPAND | wxLEFT | wxRIGHT, 10 );

    wxStdDialogButtonSizer* buttons = new wxStdDialogButtonSizer();
    buttons->AddButton( new wxButton( this, wxID_CLOSE ) );
    buttons->Realize();
    topSizer->Add( buttons, 0, wxEXPAND | wxALL, 10 );

    SetSizer( topSizer );

    m_list->Bind( wxEVT_LIST_ITEM_SELECTED, &DIALOG_WORKSPACE_HISTORY::onSelectionChanged, this );
    m_list->Bind( wxEVT_LIST_ITEM_DESELECTED, &DIALOG_WORKSPACE_HISTORY::onSelectionChanged, this );
    m_compareButton->Bind( wxEVT_BUTTON, &DIALOG_WORKSPACE_HISTORY::onCompare, this );

    Bind( wxEVT_BUTTON,
          [this]( wxCommandEvent& )
          {
              Close();
          },
          wxID_CLOSE );

    if( m_svc )
    {
        populateHistory();
    }
    else
    {
        m_list->Enable( false );
        m_statusText->SetLabel( _( "No active workspace session." ) );
    }

    updateButtons();

    finishDialogSettings();
}


DIALOG_WORKSPACE_HISTORY::~DIALOG_WORKSPACE_HISTORY()
{
    for( const wxString& path : m_tempFiles )
    {
        if( wxFileExists( path ) )
            wxRemoveFile( path );
    }
}


void DIALOG_WORKSPACE_HISTORY::populateHistory()
{
    m_list->DeleteAllItems();
    m_revisions.clear();

    std::vector<MDW_REVISION> revs;
    MDW_ERROR_CODE            code = m_svc->GetRevisionHistory( m_itemId, revs );

    if( code != MDW_ERROR_CODE::SUCCESS )
    {
        reportError( code, _( "Reading the revision history" ) );
        return;
    }

    for( const MDW_REVISION& rev : revs )
    {
        long row = m_list->InsertItem( m_list->GetItemCount(), rev.revision_id );
        m_list->SetItem( row, 1, rev.author );
        m_list->SetItem( row, 2, rev.timestamp.IsValid() ? rev.timestamp.FormatISOCombined( ' ' )
                                                          : wxString( wxEmptyString ) );
        m_list->SetItem( row, 3, rev.comment );

        m_revisions.push_back( rev );
    }
}


void DIALOG_WORKSPACE_HISTORY::onSelectionChanged( wxListEvent& aEvent )
{
    updateButtons();
}


void DIALOG_WORKSPACE_HISTORY::updateButtons()
{
    m_compareButton->Enable( m_svc && m_list->GetSelectedItemCount() == 2 );
}


void DIALOG_WORKSPACE_HISTORY::onCompare( wxCommandEvent& aEvent )
{
    if( !m_svc )
        return;

    long first = m_list->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );

    if( first < 0 )
        return;

    long second = m_list->GetNextItem( first, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );

    if( second < 0 || first >= static_cast<long>( m_revisions.size() )
        || second >= static_cast<long>( m_revisions.size() ) )
    {
        return;
    }

    const wxString& revA = m_revisions[first].revision_id;
    const wxString& revB = m_revisions[second].revision_id;

    wxString pathA = wxFileName::CreateTempFileName( wxT( "mdw_rev_" ) );
    wxString pathB = wxFileName::CreateTempFileName( wxT( "mdw_rev_" ) );

    // The diff presenter routes on file extension, so give the temp copies the
    // item's own extension and drop the empty placeholders the temp names left.
    wxString ext = wxFileName( m_itemId ).GetExt();

    if( !ext.IsEmpty() )
    {
        wxRemoveFile( pathA );
        wxRemoveFile( pathB );
        pathA += wxT( "." ) + ext;
        pathB += wxT( "." ) + ext;
    }

    m_tempFiles.push_back( pathA );
    m_tempFiles.push_back( pathB );

    MDW_ERROR_CODE code = m_svc->DownloadRevision( m_itemId, revA, pathA );

    if( code != MDW_ERROR_CODE::SUCCESS )
    {
        reportError( code, _( "Downloading the first revision" ) );
        return;
    }

    code = m_svc->DownloadRevision( m_itemId, revB, pathB );

    if( code != MDW_ERROR_CODE::SUCCESS )
    {
        reportError( code, _( "Downloading the second revision" ) );
        return;
    }

    wxString labelA = wxString::Format( wxT( "%s @ %s" ), m_itemId, revA );
    wxString labelB = wxString::Format( wxT( "%s @ %s" ), m_itemId, revB );

    MDW_DIFF_PRESENTER presenter( &Kiway(), this );
    presenter.Compare( pathA, pathB, labelA, labelB );
}


void DIALOG_WORKSPACE_HISTORY::reportError( MDW_ERROR_CODE aCode, const wxString& aContext )
{
    wxString message = aContext + wxT( "\n\n" ) + describeError( aCode );

    m_statusText->SetLabel( describeError( aCode ) );
    wxMessageBox( message, _( "Workspace Error" ), wxOK | wxICON_ERROR, this );
}
