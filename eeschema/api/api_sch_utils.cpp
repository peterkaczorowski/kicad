/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2024 Jon Evans <jon@craftyjon.com>
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sch_pin.h>
#include <lib_symbol.h>
#include <sch_symbol.h>
#include <sch_bitmap.h>
#include <sch_bus_entry.h>
#include <sch_field.h>
#include <sch_group.h>
#include <sch_junction.h>
#include <sch_label.h>
#include <sch_line.h>
#include <sch_no_connect.h>
#include <sch_shape.h>
#include <sch_sheet.h>
#include <sch_sheet_pin.h>
#include <sch_table.h>
#include <sch_tablecell.h>
#include <sch_text.h>
#include <sch_textbox.h>

#include "api_sch_utils.h"
#include <cmath>
#include <limits>
#include <set>

namespace
{
void packSchematicPosition( kiapi::common::types::Vector2* aOutput, const VECTOR2I& aPosition )
{
    // Eeschema uses 100 nm per internal unit; PCB coordinates already use nm.
    aOutput->set_x_nm( int64_t( aPosition.x ) * 100 );
    aOutput->set_y_nm( int64_t( aPosition.y ) * 100 );
}
}


kiapi::schematic::types::SheetPin PackSchematicSheetPin( const SCH_SHEET_PIN& aPin )
{
    kiapi::schematic::types::SheetPin result;
    result.mutable_id()->set_value( aPin.m_Uuid.AsStdString() );
    result.set_name( aPin.GetText().ToStdString() );
    packSchematicPosition( result.mutable_position(), aPin.GetPosition() );
    result.set_side( static_cast<kiapi::schematic::types::SheetPinSide>( int( aPin.GetSide() ) + 1 ) );
    result.set_shape( static_cast<kiapi::schematic::types::SheetPinShape>(
            int( aPin.GetLabelShape() ) + 1 ) );

    if( aPin.GetParent() )
        result.mutable_sheet_id()->set_value( aPin.GetParent()->m_Uuid.AsStdString() );

    return result;
}


kiapi::schematic::types::Sheet PackSchematicSheet( const SCH_SHEET& aSheet )
{
    kiapi::schematic::types::Sheet result;
    result.mutable_id()->set_value( aSheet.m_Uuid.AsStdString() );
    result.set_name( aSheet.GetName().ToStdString() );
    result.set_filename( aSheet.GetFileName().ToStdString() );
    packSchematicPosition( result.mutable_position(), aSheet.GetPosition() );
    packSchematicPosition( result.mutable_size(), aSheet.GetSize() );

    for( const SCH_SHEET_PIN* pin : aSheet.GetPins() )
        *result.add_pins() = PackSchematicSheetPin( *pin );

    return result;
}


std::unique_ptr<SCH_SHEET> UnpackSchematicSheet( const kiapi::schematic::types::Sheet& aSheet,
                                              EDA_ITEM* aParent, std::string& aError )
{
    auto vector = []( const kiapi::common::types::Vector2& value, VECTOR2I& out )
    {
        constexpr int64_t limit = int64_t( std::numeric_limits<int>::max() ) / 4 * 100;
        if( value.x_nm() < -limit || value.x_nm() > limit
            || value.y_nm() < -limit || value.y_nm() > limit )
            return false;

        out = VECTOR2I( std::llround( value.x_nm() / 100.0 ),
                        std::llround( value.y_nm() / 100.0 ) );
        return true;
    };
    auto validId = []( const std::string& id )
    {
        return id.empty() || ( KIID::SniffTest( wxString::FromUTF8( id ) )
                              && KIID( id ) != niluuid );
    };
    VECTOR2I position, size;
    if( aSheet.name().empty() || aSheet.filename().empty()
        || !validId( aSheet.id().value() )
        || !vector( aSheet.position(), position ) || !vector( aSheet.size(), size )
        || size.x <= 0 || size.y <= 0 )
    {
        aError = "A sheet requires a name, filename, valid UUID and positive bounded size";
        return nullptr;
    }

    auto result = std::make_unique<SCH_SHEET>( aParent, position, size );
    if( !aSheet.id().value().empty() )
        const_cast<KIID&>( result->m_Uuid ) = KIID( aSheet.id().value() );
    result->SetName( wxString::FromUTF8( aSheet.name() ) );
    result->SetFileName( wxString::FromUTF8( aSheet.filename() ) );

    std::set<std::string> names, ids{ result->m_Uuid.AsStdString() };
    for( const auto& input : aSheet.pins() )
    {
        VECTOR2I point;
        if( input.name().empty() || !names.insert( input.name() ).second
            || !validId( input.id().value() )
            || ( !input.id().value().empty() && !ids.insert( KIID( input.id().value() ).AsStdString() ).second )
            || ( !input.sheet_id().value().empty()
                 && input.sheet_id().value() != result->m_Uuid.AsStdString() )
            || !vector( input.position(), point )
            || input.side() < kiapi::schematic::types::SPS_LEFT
            || input.side() > kiapi::schematic::types::SPS_BOTTOM
            || input.shape() < kiapi::schematic::types::SPH_INPUT
            || input.shape() > kiapi::schematic::types::SPH_PASSIVE )
        {
            aError = "Invalid or duplicate sheet pin (name, UUID, parent, position, side or shape)";
            return nullptr;
        }
        const VECTOR2I offset = point - position;
        bool onEdge = false;
        switch( input.side() )
        {
        case kiapi::schematic::types::SPS_LEFT:   onEdge = offset.x == 0; break;
        case kiapi::schematic::types::SPS_RIGHT:  onEdge = offset.x == size.x; break;
        case kiapi::schematic::types::SPS_TOP:    onEdge = offset.y == 0; break;
        case kiapi::schematic::types::SPS_BOTTOM: onEdge = offset.y == size.y; break;
        default: break;
        }
        if( !onEdge || offset.x < 0 || offset.x > size.x || offset.y < 0 || offset.y > size.y )
        {
            aError = "Sheet pin position must lie on its declared sheet edge";
            return nullptr;
        }
        auto pin = std::make_unique<SCH_SHEET_PIN>( result.get(), point,
                                                  wxString::FromUTF8( input.name() ) );
        if( !input.id().value().empty() )
            const_cast<KIID&>( pin->m_Uuid ) = KIID( input.id().value() );
        pin->SetSide( static_cast<SHEET_SIDE>( input.side() - 1 ) );
        pin->SetLabelShape( static_cast<LABEL_SHAPE>( input.shape() - 1 ) );
        result->AddPin( pin.release() );
    }
    result->AutoplaceFields( nullptr, AUTOPLACE_AUTO );
    return result;
}


std::unique_ptr<EDA_ITEM> CreateItemForType( KICAD_T aType, EDA_ITEM* aContainer )
{
    SCH_ITEM* parentSchItem = dynamic_cast<SCH_ITEM*>( aContainer );

    switch( aType )
    {
    case SCH_JUNCTION_T:        return std::make_unique<SCH_JUNCTION>();
    case SCH_NO_CONNECT_T:      return std::make_unique<SCH_NO_CONNECT>();
    case SCH_BUS_WIRE_ENTRY_T:  return std::make_unique<SCH_BUS_WIRE_ENTRY>();
    case SCH_BUS_BUS_ENTRY_T:   return std::make_unique<SCH_BUS_BUS_ENTRY>();
    case SCH_LINE_T:            return std::make_unique<SCH_LINE>();
    case SCH_SHAPE_T:           return std::make_unique<SCH_SHAPE>();
    case SCH_BITMAP_T:          return std::make_unique<SCH_BITMAP>();
    case SCH_TEXTBOX_T:         return std::make_unique<SCH_TEXTBOX>();
    case SCH_TEXT_T:            return std::make_unique<SCH_TEXT>();
    case SCH_TABLE_T:           return std::make_unique<SCH_TABLE>();
    case SCH_TABLECELL_T:       return std::make_unique<SCH_TABLECELL>();
    case SCH_LABEL_T:           return std::make_unique<SCH_LABEL>();
    case SCH_GLOBAL_LABEL_T:    return std::make_unique<SCH_GLOBALLABEL>();
    case SCH_HIER_LABEL_T:      return std::make_unique<SCH_HIERLABEL>();
    case SCH_DIRECTIVE_LABEL_T: return std::make_unique<SCH_DIRECTIVE_LABEL>();
    case SCH_FIELD_T:           return std::make_unique<SCH_FIELD>( parentSchItem );
    case SCH_GROUP_T:           return std::make_unique<SCH_GROUP>();
    case SCH_SYMBOL_T:          return std::make_unique<SCH_SYMBOL>();
    case LIB_SYMBOL_T:          return std::make_unique<LIB_SYMBOL>( wxEmptyString );
    case SCH_SHEET_T:           return std::make_unique<SCH_SHEET>();

    case SCH_SHEET_PIN_T:
        if( aContainer && aContainer->Type() == SCH_SHEET_T )
            return std::make_unique<SCH_SHEET_PIN>( static_cast<SCH_SHEET*>( aContainer ) );

        return nullptr;

    case SCH_PIN_T:
        if( aContainer && aContainer->Type() == LIB_SYMBOL_T )
            return std::make_unique<SCH_PIN>( static_cast<LIB_SYMBOL*>( aContainer ) );

        return nullptr;

    default:
        return nullptr;
    }
}
