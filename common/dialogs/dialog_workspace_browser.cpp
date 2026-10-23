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

#include <dialogs/dialog_workspace_browser.h>

#include <confirm.h>

#include <mdw/mdw_framework.h>
#include <mdw/mdw_session.h>
#include <mdw/mdw_workspace_service.h>
#include <mdw/mdw_backend.h>
#include <mdw/mdw_item.h>
#include <mdw/mdw_navigation.h>
#include <mdw/mdw_state_mapper.h>
#include <widgets/mdw_property_editor.h>
#include <widgets/ui_common.h>

#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>
#include <wx/treectrl.h>


namespace
{
/// Tree node payload carrying the backend folder id.
class FOLDER_NODE : public wxTreeItemData
{
public:
    FOLDER_NODE( const wxString& aFolderId ) : m_folderId( aFolderId ) {}

    const wxString& FolderId() const { return m_folderId; }

private:
    wxString m_folderId;
};


}


DIALOG_WORKSPACE_BROWSER::DIALOG_WORKSPACE_BROWSER( wxWindow* aParent, MDW_FRAMEWORK* aFramework,
                                                    const wxString& aProjectPath ) :
        DIALOG_SHIM( aParent, wxID_ANY, _( "Design Workspace Browser" ), wxDefaultPosition,
                     wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_framework( aFramework ),
        m_projectPath( aProjectPath ),
        m_session( nullptr ),
        m_svc( nullptr )
{
    m_session = m_framework ? m_framework->Session( m_projectPath ) : nullptr;
    m_svc = m_session ? m_session->Service() : nullptr;

    const int margin = FromDIP( KIUI::GetStdMargin() );
    const int dlgMargin = 2 * margin;

    wxBoxSizer* topSizer = new wxBoxSizer( wxVERTICAL );

    wxBoxSizer* panes = new wxBoxSizer( wxHORIZONTAL );

    m_tree = new wxTreeCtrl( this, wxID_ANY, wxDefaultPosition, FromDIP( wxSize( 220, -1 ) ),
                             wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_SINGLE );
    panes->Add( m_tree, 1, wxEXPAND | wxRIGHT, margin );

    m_list = new wxListCtrl( this, wxID_ANY, wxDefaultPosition, FromDIP( wxSize( 360, -1 ) ),
                             wxLC_REPORT | wxLC_SINGLE_SEL );
    m_list->AppendColumn( _( "Name" ), wxLIST_FORMAT_LEFT, FromDIP( 170 ) );
    m_list->AppendColumn( _( "Revision" ), wxLIST_FORMAT_LEFT, FromDIP( 70 ) );
    m_list->AppendColumn( _( "State" ), wxLIST_FORMAT_LEFT, FromDIP( 70 ) );
    m_list->AppendColumn( _( "Lock" ), wxLIST_FORMAT_LEFT, FromDIP( 90 ) );
    panes->Add( m_list, 2, wxEXPAND | wxRIGHT, margin );

    m_props = new MDW_PROPERTY_EDITOR( this );
    panes->Add( m_props, 2, wxEXPAND );

    // finishDialogSettings fits the dialog to its sizer, so the wanted opening size has to
    // come from the content minimum rather than from a frame size.
    panes->SetMinSize( FromDIP( wxSize( 880, 430 ) ) );

    topSizer->Add( panes, 1, wxEXPAND | wxALL, dlgMargin );

    wxBoxSizer* actionRow = new wxBoxSizer( wxHORIZONTAL );
    m_checkoutButton = new wxButton( this, wxID_ANY, _( "Checkout" ) );
    m_checkinButton = new wxButton( this, wxID_ANY, _( "Check In" ) );
    m_refreshButton = new wxButton( this, wxID_ANY, _( "Refresh" ) );
    m_statusText = new wxStaticText( this, wxID_ANY, wxEmptyString );

    actionRow->Add( m_checkoutButton, 0, wxRIGHT, margin );
    actionRow->Add( m_checkinButton, 0, wxRIGHT, margin );
    actionRow->Add( m_refreshButton, 0, wxRIGHT, dlgMargin );
    actionRow->Add( m_statusText, 1, wxALIGN_CENTER_VERTICAL );
    topSizer->Add( actionRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, dlgMargin );

    topSizer->Add( new wxStaticLine( this ), 0, wxEXPAND | wxLEFT | wxRIGHT, dlgMargin );

    wxStdDialogButtonSizer* buttons = new wxStdDialogButtonSizer();
    buttons->AddButton( new wxButton( this, wxID_CANCEL ) );
    buttons->Realize();
    topSizer->Add( buttons, 0, wxEXPAND | wxALL, dlgMargin );

    SetSizer( topSizer );

    m_tree->Bind( wxEVT_TREE_SEL_CHANGED, &DIALOG_WORKSPACE_BROWSER::onFolderSelected, this );
    m_tree->Bind( wxEVT_TREE_ITEM_EXPANDING, &DIALOG_WORKSPACE_BROWSER::onFolderExpanding, this );
    m_list->Bind( wxEVT_LIST_ITEM_SELECTED, &DIALOG_WORKSPACE_BROWSER::onItemSelected, this );
    m_checkoutButton->Bind( wxEVT_BUTTON, &DIALOG_WORKSPACE_BROWSER::onCheckout, this );
    m_checkinButton->Bind( wxEVT_BUTTON, &DIALOG_WORKSPACE_BROWSER::onCheckin, this );
    m_refreshButton->Bind( wxEVT_BUTTON, &DIALOG_WORKSPACE_BROWSER::onRefresh, this );

    if( m_svc )
    {
        m_props->SetSchema( m_session->Backend()->Schema() );
        populateRoots();
    }
    else
    {
        m_tree->Enable( false );
        m_list->Enable( false );
        m_props->Enable( false );
        m_statusText->SetLabel( _( "No active workspace session." ) );
    }

    updateButtons();

    SetupStandardButtons( { { wxID_CANCEL, _( "Close" ) } } );

    finishDialogSettings();
}


void DIALOG_WORKSPACE_BROWSER::populateRoots()
{
    m_tree->DeleteAllItems();

    wxTreeItemId root = m_tree->AddRoot( _( "Workspace" ) );

    std::vector<MDW_FOLDER> roots;
    MDW_ERROR_CODE          code = m_svc->GetRootFolders( roots );

    if( code != MDW_ERROR_CODE::SUCCESS )
    {
        reportError( code, _( "Reading the workspace root folders" ) );
        return;
    }

    for( const MDW_FOLDER& folder : roots )
    {
        wxTreeItemId node = m_tree->AppendItem( root, folder.display_name, -1, -1,
                                                new FOLDER_NODE( folder.folder_id ) );

        if( folder.has_children )
            m_tree->AppendItem( node, wxEmptyString );
    }

    m_tree->Expand( root );
}


void DIALOG_WORKSPACE_BROWSER::populateChildren( const wxTreeItemId& aParent,
                                                 const wxString& aFolderId )
{
    std::vector<MDW_FOLDER> kids;
    MDW_ERROR_CODE          code = m_svc->GetChildFolders( aFolderId, kids );

    if( code != MDW_ERROR_CODE::SUCCESS )
    {
        reportError( code, _( "Reading child folders" ) );
        return;
    }

    for( const MDW_FOLDER& folder : kids )
    {
        wxTreeItemId node = m_tree->AppendItem( aParent, folder.display_name, -1, -1,
                                                new FOLDER_NODE( folder.folder_id ) );

        if( folder.has_children )
            m_tree->AppendItem( node, wxEmptyString );
    }
}


void DIALOG_WORKSPACE_BROWSER::onFolderExpanding( wxTreeEvent& aEvent )
{
    if( !m_svc )
        return;

    wxTreeItemId item = aEvent.GetItem();
    FOLDER_NODE* data = dynamic_cast<FOLDER_NODE*>( m_tree->GetItemData( item ) );

    if( !data )
        return;

    wxTreeItemIdValue cookie;
    wxTreeItemId      first = m_tree->GetFirstChild( item, cookie );

    // A single child with no payload is the lazy-load placeholder.
    if( first.IsOk() && m_tree->GetItemData( first ) == nullptr )
    {
        m_tree->DeleteChildren( item );
        populateChildren( item, data->FolderId() );
    }
}


void DIALOG_WORKSPACE_BROWSER::onFolderSelected( wxTreeEvent& aEvent )
{
    if( !m_svc )
        return;

    FOLDER_NODE* data = dynamic_cast<FOLDER_NODE*>( m_tree->GetItemData( aEvent.GetItem() ) );

    if( data )
        loadFolderItems( data->FolderId() );
}


void DIALOG_WORKSPACE_BROWSER::loadFolderItems( const wxString& aFolderId )
{
    m_currentFolderId = aFolderId;
    m_selectedItemId.clear();
    m_list->DeleteAllItems();
    m_items.clear();

    std::vector<MDW_ITEM_INFO> items;
    MDW_ERROR_CODE             code = m_svc->ListItems( aFolderId, items );

    if( code != MDW_ERROR_CODE::SUCCESS )
    {
        reportError( code, _( "Listing folder items" ) );
        updateButtons();
        return;
    }

    for( const MDW_ITEM_INFO& item : items )
    {
        if( item.is_folder )
            continue;

        long row = m_list->InsertItem( m_list->GetItemCount(), item.display_name );
        m_list->SetItem( row, 1, item.revision_id );
        m_list->SetItem( row, 2, MDW_STATE_MAPPER::CategoryDisplayName( item.category ) );
        m_list->SetItem( row, 3, item.is_locked ? item.lock_owner : wxString( _( "Free" ) ) );

        m_items.push_back( item );
    }

    updateButtons();
}


void DIALOG_WORKSPACE_BROWSER::onItemSelected( wxListEvent& aEvent )
{
    long index = aEvent.GetIndex();

    if( index < 0 || index >= static_cast<long>( m_items.size() ) )
        return;

    m_selectedItemId = m_items[index].item_id;
    loadItem( m_items[index] );
    updateButtons();
}


void DIALOG_WORKSPACE_BROWSER::loadItem( const MDW_ITEM_INFO& aInfo )
{
    MDW_ATTRIBUTE_SET attrs;
    MDW_ERROR_CODE    code = m_svc->GetAttributes( aInfo.item_id, attrs );

    if( code != MDW_ERROR_CODE::SUCCESS )
    {
        reportError( code, _( "Reading item attributes" ) );
        return;
    }

    // Editable only when the lifecycle allows writes and the item is not locked
    // by someone else. A lock this user holds still permits editing.
    bool lockedByOther = aInfo.is_locked && !m_session->LockTracker().LockedByMe( aInfo.item_id );
    bool readOnly = lockedByOther || MDW_STATE_MAPPER::CategoryBlocksWrites( aInfo.category );

    m_props->SetValues( attrs );
    m_props->SetReadOnly( readOnly );
}


void DIALOG_WORKSPACE_BROWSER::onCheckout( wxCommandEvent& aEvent )
{
    if( !m_svc || m_selectedItemId.IsEmpty() )
        return;

    MDW_CHECKOUT_REQUEST req;
    req.item_id = m_selectedItemId;
    req.lock = true;

    MDW_CHECKOUT_RESULT res;
    MDW_ERROR_CODE      code = m_svc->Checkout( req, res );

    if( code != MDW_ERROR_CODE::SUCCESS )
    {
        reportError( code, _( "Checking out the item" ) );
        return;
    }

    m_statusText->SetLabel( wxString::Format( _( "Checked out %s." ), res.item.display_name ) );
    loadFolderItems( m_currentFolderId );
}


void DIALOG_WORKSPACE_BROWSER::onCheckin( wxCommandEvent& aEvent )
{
    if( !m_svc || m_selectedItemId.IsEmpty() )
        return;

    MDW_ATTRIBUTE_SET vals = m_props->GetValues();

    std::vector<MDW_SCHEMA_VALIDATOR::ERROR> errs = m_props->Validate();

    if( !errs.empty() )
    {
        wxString message = _( "Correct the following before checking in:" );

        for( const MDW_SCHEMA_VALIDATOR::ERROR& err : errs )
            message << wxT( "\n" ) << err.message;

        wxMessageBox( message, _( "Invalid Attributes" ), wxOK | wxICON_WARNING, this );
        return;
    }

    wxString comment = wxGetTextFromUser( _( "Check-in comment:" ), _( "Check In" ),
                                          wxEmptyString, this );

    MDW_CHECKIN_REQUEST req;
    req.item_id = m_selectedItemId;
    req.attributes = vals;
    req.comment = comment;

    MDW_CHECKIN_RESULT res;
    MDW_ERROR_CODE     code = m_svc->CheckinSync( req, res );

    if( code != MDW_ERROR_CODE::SUCCESS )
    {
        reportError( code, _( "Checking in the item" ) );
        return;
    }

    m_statusText->SetLabel( wxString::Format( _( "Checked in revision %s." ),
                                              res.revision.revision_id ) );
    loadFolderItems( m_currentFolderId );
}


void DIALOG_WORKSPACE_BROWSER::onRefresh( wxCommandEvent& aEvent )
{
    if( !m_svc )
        return;

    populateRoots();

    if( !m_currentFolderId.IsEmpty() )
        loadFolderItems( m_currentFolderId );
}


void DIALOG_WORKSPACE_BROWSER::updateButtons()
{
    bool hasSelection = m_svc && !m_selectedItemId.IsEmpty();

    m_checkoutButton->Enable( hasSelection );
    m_checkinButton->Enable( hasSelection );
    m_refreshButton->Enable( m_svc != nullptr );
}


void DIALOG_WORKSPACE_BROWSER::reportError( MDW_ERROR_CODE aCode, const wxString& aContext )
{
    wxString message = MdwErrorMessage( aCode );

    m_statusText->SetLabel( message );
    DisplayErrorMessage( this, aContext, message );
}
