///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version 4.2.1-0-g80c4cb6a-dirty)
// http://www.wxformbuilder.org/
//
// PLEASE DO *NOT* EDIT THIS FILE!
///////////////////////////////////////////////////////////////////////////

#include "pcb_layer_box_selector.h"

#include "dialog_reference_image_properties_base.h"

///////////////////////////////////////////////////////////////////////////

DIALOG_REFERENCE_IMAGE_PROPERTIES_BASE::DIALOG_REFERENCE_IMAGE_PROPERTIES_BASE( wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style ) : DIALOG_SHIM( parent, id, title, pos, size, style )
{
	this->SetSizeHints( wxDefaultSize, wxDefaultSize );

	wxBoxSizer* bMainSizer;
	bMainSizer = new wxBoxSizer( wxVERTICAL );

	m_Notebook = new wxNotebook( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 );

	// "General" page: position, size, layer, anchor, etc.
	m_PanelGeneral = new wxPanel( m_Notebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	// Horizontal: position/layer/locked on the left, the anchor selector (added by the
	// derived dialog) on the right.
	m_generalSizer = new wxBoxSizer( wxHORIZONTAL );

	wxGridBagSizer* gbSizer1;
	gbSizer1 = new wxGridBagSizer( 3, 5 );
	gbSizer1->SetFlexibleDirection( wxBOTH );
	gbSizer1->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_ALL );
	gbSizer1->SetEmptyCellSize( wxSize( -1,5 ) );

	m_XPosLabel = new wxStaticText( m_PanelGeneral, wxID_ANY, _("Position X:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_XPosLabel->Wrap( -1 );
	gbSizer1->Add( m_XPosLabel, wxGBPosition( 0, 0 ), wxGBSpan( 1, 1 ), wxALIGN_CENTER_VERTICAL, 5 );

	m_ModPositionX = new wxTextCtrl( m_PanelGeneral, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	gbSizer1->Add( m_ModPositionX, wxGBPosition( 0, 1 ), wxGBSpan( 1, 1 ), wxALIGN_CENTER_VERTICAL|wxEXPAND, 5 );

	m_XPosUnit = new wxStaticText( m_PanelGeneral, wxID_ANY, _("unit"), wxDefaultPosition, wxDefaultSize, 0 );
	m_XPosUnit->Wrap( -1 );
	gbSizer1->Add( m_XPosUnit, wxGBPosition( 0, 2 ), wxGBSpan( 1, 1 ), wxALIGN_CENTER_VERTICAL, 5 );

	m_YPosLabel = new wxStaticText( m_PanelGeneral, wxID_ANY, _("Position Y:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_YPosLabel->Wrap( -1 );
	gbSizer1->Add( m_YPosLabel, wxGBPosition( 1, 0 ), wxGBSpan( 1, 1 ), wxALIGN_CENTER_VERTICAL, 5 );

	m_ModPositionY = new wxTextCtrl( m_PanelGeneral, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	gbSizer1->Add( m_ModPositionY, wxGBPosition( 1, 1 ), wxGBSpan( 1, 1 ), wxALIGN_CENTER_VERTICAL|wxEXPAND, 5 );

	m_YPosUnit = new wxStaticText( m_PanelGeneral, wxID_ANY, _("unit"), wxDefaultPosition, wxDefaultSize, 0 );
	m_YPosUnit->Wrap( -1 );
	gbSizer1->Add( m_YPosUnit, wxGBPosition( 1, 2 ), wxGBSpan( 1, 1 ), wxALIGN_CENTER_VERTICAL, 5 );

	// Width/Height moved to the "Image" page (created below, positioned by the derived ctor).

	// One blank row (~field height) between the Position rows and the layer selector.
	gbSizer1->Add( 0, 20, wxGBPosition( 2, 0 ), wxGBSpan( 1, 1 ) );

	m_LayerLabel = new wxStaticText( m_PanelGeneral, wxID_ANY, _("Associated layer:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_LayerLabel->Wrap( -1 );
	gbSizer1->Add( m_LayerLabel, wxGBPosition( 3, 0 ), wxGBSpan( 1, 1 ), wxALIGN_CENTER_VERTICAL, 5 );

	m_LayerSelectionCtrl = new PCB_LAYER_BOX_SELECTOR( m_PanelGeneral, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, NULL, 0 );
	gbSizer1->Add( m_LayerSelectionCtrl, wxGBPosition( 3, 1 ), wxGBSpan( 1, 1 ), wxALIGN_CENTER_VERTICAL|wxEXPAND, 5 );

	m_cbLocked = new wxCheckBox( m_PanelGeneral, wxID_ANY, _("Locked"), wxDefaultPosition, wxDefaultSize, 0 );
	gbSizer1->Add( m_cbLocked, wxGBPosition( 4, 0 ), wxGBSpan( 1, 3 ), wxALIGN_CENTER_VERTICAL, 5 );


	gbSizer1->AddGrowableCol( 1 );

	// Left column (~40% of the page width); the anchor box + a stretch spacer (added by the
	// derived dialog) take the rest, leaving a wide right margin.  A vertical spacer pushes the
	// first row down so "Position X" lines up with the top of the Anchor box on the right.
	wxBoxSizer* leftColSizer = new wxBoxSizer( wxVERTICAL );
	leftColSizer->AddSpacer( 12 );
	leftColSizer->Add( gbSizer1, 0, wxEXPAND, 0 );
	m_generalSizer->Add( leftColSizer, 4, wxALIGN_TOP|wxALL, 5 );

	m_PanelGeneral->SetSizer( m_generalSizer );
	m_PanelGeneral->Layout();
	m_generalSizer->Fit( m_PanelGeneral );
	m_Notebook->AddPage( m_PanelGeneral, _("General"), true );

	// "Image" page: the image editor plus the Width/Height size controls
	// (all added/positioned by the derived dialog).
	m_PanelImage = new wxPanel( m_Notebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	m_imageSizer = new wxBoxSizer( wxVERTICAL );

	// Width is intentionally placed above Height.  These controls are created here so the
	// derived dialog's UNIT_BINDERs can bind to them, but they are laid out by the derived ctor.
	m_WidthLabel = new wxStaticText( m_PanelImage, wxID_ANY, _("Width:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_WidthLabel->Wrap( -1 );
	m_ModWidth = new wxTextCtrl( m_PanelImage, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	m_WidthUnit = new wxStaticText( m_PanelImage, wxID_ANY, _("unit"), wxDefaultPosition, wxDefaultSize, 0 );
	m_WidthUnit->Wrap( -1 );

	m_HeightLabel = new wxStaticText( m_PanelImage, wxID_ANY, _("Height:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_HeightLabel->Wrap( -1 );
	m_ModHeight = new wxTextCtrl( m_PanelImage, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	m_HeightUnit = new wxStaticText( m_PanelImage, wxID_ANY, _("unit"), wxDefaultPosition, wxDefaultSize, 0 );
	m_HeightUnit->Wrap( -1 );

	m_PanelImage->SetSizer( m_imageSizer );
	m_PanelImage->Layout();
	m_imageSizer->Fit( m_PanelImage );
	m_Notebook->AddPage( m_PanelImage, _("Image"), false );

	bMainSizer->Add( m_Notebook, 1, wxEXPAND|wxALL, 8 );

	m_sdbSizerStdButtons = new wxStdDialogButtonSizer();
	m_sdbSizerStdButtonsOK = new wxButton( this, wxID_OK );
	m_sdbSizerStdButtons->AddButton( m_sdbSizerStdButtonsOK );
	m_sdbSizerStdButtonsCancel = new wxButton( this, wxID_CANCEL );
	m_sdbSizerStdButtons->AddButton( m_sdbSizerStdButtonsCancel );
	m_sdbSizerStdButtons->Realize();

	bMainSizer->Add( m_sdbSizerStdButtons, 0, wxEXPAND|wxBOTTOM|wxRIGHT|wxLEFT, 5 );


	this->SetSizer( bMainSizer );
	this->Layout();
	bMainSizer->Fit( this );

	this->Centre( wxBOTH );
}

DIALOG_REFERENCE_IMAGE_PROPERTIES_BASE::~DIALOG_REFERENCE_IMAGE_PROPERTIES_BASE()
{
}
