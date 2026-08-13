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

#include <widgets/mdw_property_editor.h>

#include <algorithm>

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>


MDW_PROPERTY_EDITOR::MDW_PROPERTY_EDITOR( wxWindow* aParent ) :
        wxScrolledWindow( aParent, wxID_ANY ),
        m_grid( nullptr ),
        m_readOnly( false )
{
    m_grid = new wxFlexGridSizer( 2, wxSize( 8, 6 ) );
    m_grid->AddGrowableCol( 1, 1 );

    wxBoxSizer* outer = new wxBoxSizer( wxVERTICAL );
    outer->Add( m_grid, 1, wxEXPAND | wxALL, 6 );

    SetSizer( outer );
    SetScrollRate( 0, 8 );
}


void MDW_PROPERTY_EDITOR::SetSchema( const MDW_SCHEMA& aSchema )
{
    m_schema = aSchema;
    rebuild();
}


void MDW_PROPERTY_EDITOR::rebuild()
{
    m_grid->Clear( true );
    m_fields.clear();

    std::vector<MDW_ATTRIBUTE_SCHEMA> ordered = m_schema.attributes;
    std::stable_sort( ordered.begin(), ordered.end(),
                      []( const MDW_ATTRIBUTE_SCHEMA& a, const MDW_ATTRIBUTE_SCHEMA& b )
                      { return a.display_order < b.display_order; } );

    for( const MDW_ATTRIBUTE_SCHEMA& attr : ordered )
    {
        wxString label = attr.display_name;

        if( attr.required )
            label += wxS( " *" );

        m_grid->Add( new wxStaticText( this, wxID_ANY, label ), 0, wxALIGN_CENTER_VERTICAL );

        FIELD field;
        field.schema = attr;

        switch( attr.type )
        {
        case MDW_ATTR_TYPE::BOOLEAN:
            field.control = new wxCheckBox( this, wxID_ANY, wxEmptyString );
            break;

        case MDW_ATTR_TYPE::ENUM:
        {
            wxChoice* choice = new wxChoice( this, wxID_ANY );

            for( const wxString& option : attr.enum_values )
                choice->Append( option );

            field.control = choice;
            break;
        }

        default:
            field.control = new wxTextCtrl( this, wxID_ANY );
            break;
        }

        field.control->Enable( attr.editable_in_kicad && !m_readOnly );
        m_grid->Add( field.control, 1, wxEXPAND );

        m_fields.push_back( field );
    }

    Layout();
    FitInside();
}


wxString MDW_PROPERTY_EDITOR::getControlValue( const FIELD& aField ) const
{
    if( auto* check = dynamic_cast<wxCheckBox*>( aField.control ) )
        return check->GetValue() ? wxS( "true" ) : wxS( "false" );

    if( auto* choice = dynamic_cast<wxChoice*>( aField.control ) )
        return choice->GetStringSelection();

    if( auto* text = dynamic_cast<wxTextCtrl*>( aField.control ) )
        return text->GetValue();

    return wxEmptyString;
}


void MDW_PROPERTY_EDITOR::setControlValue( const FIELD& aField, const wxString& aValue )
{
    if( auto* check = dynamic_cast<wxCheckBox*>( aField.control ) )
    {
        check->SetValue( aValue == wxS( "true" ) || aValue == wxS( "1" ) );
    }
    else if( auto* choice = dynamic_cast<wxChoice*>( aField.control ) )
    {
        if( !choice->SetStringSelection( aValue ) && choice->GetCount() > 0 )
            choice->SetSelection( 0 );
    }
    else if( auto* text = dynamic_cast<wxTextCtrl*>( aField.control ) )
    {
        text->SetValue( aValue );
    }
}


void MDW_PROPERTY_EDITOR::SetValues( const MDW_ATTRIBUTE_SET& aValues )
{
    for( const FIELD& field : m_fields )
        setControlValue( field, aValues.Get( field.schema.attr_id ) );
}


MDW_ATTRIBUTE_SET MDW_PROPERTY_EDITOR::GetValues() const
{
    MDW_ATTRIBUTE_SET values;

    for( const FIELD& field : m_fields )
        values.Set( field.schema.attr_id, getControlValue( field ) );

    return values;
}


std::vector<MDW_SCHEMA_VALIDATOR::ERROR> MDW_PROPERTY_EDITOR::Validate() const
{
    return MDW_SCHEMA_VALIDATOR::Validate( m_schema, GetValues() );
}


void MDW_PROPERTY_EDITOR::SetReadOnly( bool aReadOnly )
{
    m_readOnly = aReadOnly;

    for( const FIELD& field : m_fields )
        field.control->Enable( field.schema.editable_in_kicad && !m_readOnly );
}
