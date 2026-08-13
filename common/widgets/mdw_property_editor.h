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
 * @file mdw_property_editor.h
 * @brief The one schema-driven property editor shared across the workspace UI.
 *
 * The widget consumes an MDW_SCHEMA and emits one typed control per attribute,
 * so adding an attribute never requires an editor change. The browser property
 * panel, the checkin dialog and the search filter row all use this widget, and
 * none of them contains any backend-specific UI.
 */

#ifndef MDW_PROPERTY_EDITOR_H
#define MDW_PROPERTY_EDITOR_H

#include <kicommon.h>
#include <mdw/mdw_schema.h>
#include <mdw/mdw_schema_validator.h>

#include <vector>

#include <wx/scrolwin.h>

class wxFlexGridSizer;

class KICOMMON_API MDW_PROPERTY_EDITOR : public wxScrolledWindow
{
public:
    MDW_PROPERTY_EDITOR( wxWindow* aParent );

    /// Rebuild the control grid from a schema, discarding any prior controls.
    void SetSchema( const MDW_SCHEMA& aSchema );

    void SetValues( const MDW_ATTRIBUTE_SET& aValues );

    MDW_ATTRIBUTE_SET GetValues() const;

    /// Validate the current values against the schema.
    std::vector<MDW_SCHEMA_VALIDATOR::ERROR> Validate() const;

    /// Disable every control, for a read-only or write-blocked item.
    void SetReadOnly( bool aReadOnly );

private:
    struct FIELD
    {
        MDW_ATTRIBUTE_SCHEMA schema;
        wxWindow*            control = nullptr;
    };

    void rebuild();

    wxString getControlValue( const FIELD& aField ) const;

    void setControlValue( const FIELD& aField, const wxString& aValue );

    MDW_SCHEMA         m_schema;
    std::vector<FIELD> m_fields;
    wxFlexGridSizer*   m_grid;
    bool               m_readOnly;
};

#endif // MDW_PROPERTY_EDITOR_H
