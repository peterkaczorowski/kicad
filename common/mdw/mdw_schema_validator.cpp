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

#include <mdw/mdw_schema_validator.h>

#include <wx/datetime.h>
#include <wx/translation.h>


static bool isEnumMember( const MDW_ATTRIBUTE_SCHEMA& aAttr, const wxString& aValue )
{
    for( const wxString& option : aAttr.enum_values )
    {
        if( option == aValue )
            return true;
    }

    return false;
}


bool MDW_SCHEMA_VALIDATOR::ValidateValue( const MDW_ATTRIBUTE_SCHEMA& aAttr, const wxString& aValue,
                                          wxString& aError )
{
    if( aValue.IsEmpty() )
    {
        if( aAttr.required )
        {
            aError = wxString::Format( _( "%s is required." ), aAttr.display_name );
            return false;
        }

        return true;
    }

    switch( aAttr.type )
    {
    case MDW_ATTR_TYPE::INTEGER:
    {
        long value = 0;

        if( !aValue.ToLong( &value ) )
        {
            aError = wxString::Format( _( "%s must be a whole number." ), aAttr.display_name );
            return false;
        }

        break;
    }

    case MDW_ATTR_TYPE::FLOAT:
    {
        double value = 0.0;

        if( !aValue.ToDouble( &value ) )
        {
            aError = wxString::Format( _( "%s must be a number." ), aAttr.display_name );
            return false;
        }

        break;
    }

    case MDW_ATTR_TYPE::BOOLEAN:
    {
        if( aValue != wxS( "0" ) && aValue != wxS( "1" ) && aValue.CmpNoCase( wxS( "true" ) ) != 0
            && aValue.CmpNoCase( wxS( "false" ) ) != 0 )
        {
            aError = wxString::Format( _( "%s must be true or false." ), aAttr.display_name );
            return false;
        }

        break;
    }

    case MDW_ATTR_TYPE::ENUM:
    {
        if( !isEnumMember( aAttr, aValue ) )
        {
            aError = wxString::Format( _( "%s is not one of the allowed values." ),
                                       aAttr.display_name );
            return false;
        }

        break;
    }

    case MDW_ATTR_TYPE::DATE:
    case MDW_ATTR_TYPE::DATETIME:
    {
        wxDateTime      parsed;
        wxString::const_iterator end;
        const wxString  format = aAttr.type == MDW_ATTR_TYPE::DATE ? wxS( "%Y-%m-%d" )
                                                                   : wxS( "%Y-%m-%dT%H:%M:%S" );

        if( !parsed.ParseFormat( aValue, format, &end ) )
        {
            aError = wxString::Format( _( "%s is not a valid date." ), aAttr.display_name );
            return false;
        }

        break;
    }

    case MDW_ATTR_TYPE::STRING:
    case MDW_ATTR_TYPE::USER_REFERENCE:
        break;
    }

    return true;
}


std::vector<MDW_SCHEMA_VALIDATOR::ERROR> MDW_SCHEMA_VALIDATOR::Validate(
        const MDW_SCHEMA& aSchema, const MDW_ATTRIBUTE_SET& aValues )
{
    std::vector<ERROR> errors;

    for( const MDW_ATTRIBUTE_SCHEMA& attr : aSchema.attributes )
    {
        wxString message;

        if( !ValidateValue( attr, aValues.Get( attr.attr_id ), message ) )
            errors.push_back( { attr.attr_id, message } );
    }

    return errors;
}
