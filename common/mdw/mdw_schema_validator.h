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
 * @file mdw_schema_validator.h
 * @brief Validation of attribute values against a backend-declared schema.
 *
 * The schema-driven property editor renders one widget per attribute, but the
 * type and required-field checks are pure, so they live here where they can be
 * exercised without a display and reused by any consumer of the schema.
 */

#ifndef MDW_SCHEMA_VALIDATOR_H
#define MDW_SCHEMA_VALIDATOR_H

#include <kicommon.h>
#include "mdw_schema.h"

#include <vector>

#include <wx/string.h>

class KICOMMON_API MDW_SCHEMA_VALIDATOR
{
public:
    /// One validation failure: the offending attribute and a human message.
    struct ERROR
    {
        wxString attr_id;
        wxString message;
    };

    /**
     * Validate one value against one attribute definition. Returns true when the
     * value is acceptable; otherwise aError is set to a human-readable reason.
     */
    static bool ValidateValue( const MDW_ATTRIBUTE_SCHEMA& aAttr, const wxString& aValue,
                               wxString& aError );

    /**
     * Validate a full attribute set against a schema. Every required attribute
     * must be present and non-empty, and every present value must satisfy its
     * type. Returns the list of failures, empty when the set is valid.
     */
    static std::vector<ERROR> Validate( const MDW_SCHEMA& aSchema, const MDW_ATTRIBUTE_SET& aValues );
};

#endif // MDW_SCHEMA_VALIDATOR_H
