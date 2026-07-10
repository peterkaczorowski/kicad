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

/**
 * @file mdw_schema.h
 * @brief Backend-declared attribute schema and attribute values.
 *
 * The framework treats the schema as opaque. The schema-driven property editor
 * walks the attribute list and emits one typed widget per definition, so adding
 * an attribute needs no editor change.
 */

#ifndef MDW_SCHEMA_H
#define MDW_SCHEMA_H

#include "mdw_types.h"

#include <wx/string.h>
#include <vector>

/**
 * One attribute definition.
 */
struct MDW_ATTRIBUTE_SCHEMA
{
    wxString              attr_id;
    wxString              display_name;
    wxString              description;
    MDW_ATTR_TYPE         type = MDW_ATTR_TYPE::STRING;
    bool                  required = false;
    bool                  editable_in_kicad = true;
    std::vector<wxString> enum_values;             ///< MDW_ATTR_TYPE::ENUM only
    wxString              default_value;
    int                   display_order = 0;
};


/**
 * The backend-declared item attribute schema. Vendor vocabularies for optional
 * capabilities live on their own sub-interface schema accessors.
 */
struct MDW_SCHEMA
{
    wxString                          schema_version;
    std::vector<MDW_ATTRIBUTE_SCHEMA> attributes;

    const MDW_ATTRIBUTE_SCHEMA* Find( const wxString& aAttrId ) const
    {
        for( const MDW_ATTRIBUTE_SCHEMA& attr : attributes )
        {
            if( attr.attr_id == aAttrId )
                return &attr;
        }

        return nullptr;
    }
};


/**
 * One attribute value. The value is text, typed by the schema on render.
 */
struct MDW_ATTRIBUTE_VALUE
{
    wxString attr_id;
    wxString value;
};


/**
 * A set of attribute values attached to an item.
 */
struct MDW_ATTRIBUTE_SET
{
    std::vector<MDW_ATTRIBUTE_VALUE> values;

    wxString Get( const wxString& aAttrId ) const
    {
        for( const MDW_ATTRIBUTE_VALUE& v : values )
        {
            if( v.attr_id == aAttrId )
                return v.value;
        }

        return wxEmptyString;
    }

    void Set( const wxString& aAttrId, const wxString& aValue )
    {
        for( MDW_ATTRIBUTE_VALUE& v : values )
        {
            if( v.attr_id == aAttrId )
            {
                v.value = aValue;
                return;
            }
        }

        values.push_back( { aAttrId, aValue } );
    }
};


#endif // MDW_SCHEMA_H
