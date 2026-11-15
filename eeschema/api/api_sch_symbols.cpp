/* Copyright The KiCad Developers, see AUTHORS.txt.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "api_sch_utils.h"
#include <api/api_utils.h>
#include <api/api_enums.h>
#include <sch_symbol.h>
#include <lib_symbol.h>
#include <sch_sheet.h>
#include <sch_sheet_path.h>
#include <sch_field.h>
#include <sch_pin.h>
#include <set>

using namespace kiapi::schematic::types;
using namespace kiapi::common;

namespace
{
int pinAngle( PIN_ORIENTATION value )
{
    switch( value )
    {
    case PIN_ORIENTATION::PIN_RIGHT: return 0;
    case PIN_ORIENTATION::PIN_UP: return 90;
    case PIN_ORIENTATION::PIN_LEFT: return 180;
    case PIN_ORIENTATION::PIN_DOWN: return 270;
    default: return 0;
    }
}
}

Symbol PackSchematicSymbol( const SCH_SYMBOL& symbol, const SCH_SHEET_PATH& path )
{
    Symbol out;
    out.mutable_id()->set_value( symbol.m_Uuid.AsStdString() );
    *out.mutable_lib_id() = LibIdToProto( symbol.GetLibId() );
    PackSchematicVector( *out.mutable_position(), symbol.GetPosition() );
    int orientation = symbol.GetOrientation();
    out.set_mirror_x( symbol.GetMirrorX() );
    out.set_mirror_y( symbol.GetMirrorY() );
    orientation &= ~( SYM_MIRROR_X | SYM_MIRROR_Y );
    out.set_rotation_degrees( orientation == SYM_ORIENT_90 ? 90 : orientation == SYM_ORIENT_180 ? 180
                                             : orientation == SYM_ORIENT_270 ? 270 : 0 );
    out.set_unit( symbol.GetUnitSelection( &path ) );
    out.set_body_style( symbol.GetBodyStyle() );
    out.set_reference( symbol.GetRef( &path, false ).ToStdString() );
    out.set_dnp( symbol.GetDNP( &path ) );
    out.set_in_bom( !symbol.GetExcludedFromBOM( &path ) );
    out.set_on_board( !symbol.GetExcludedFromBoard( &path ) );
    for( const auto& field : symbol.GetFields() )
    {
        auto* value = out.add_fields();
        value->set_name( field.GetCanonicalName().ToStdString() );
        PackSchematicText( *value->mutable_text(), field );
        if( field.GetId() == FIELD_T::REFERENCE ) value->mutable_text()->set_text( out.reference() );
        value->set_visible( field.IsVisible() );
    }
    for( const SCH_PIN* pin : symbol.GetPins( &path ) )
    {
        auto* value = out.add_pins();
        value->mutable_id()->set_value( pin->m_Uuid.AsStdString() );
        value->set_number( pin->GetNumber().ToStdString() );
        value->set_name( pin->GetName().ToStdString() );
        PackSchematicVector( *value->mutable_position(), pin->GetPosition() );
        value->set_electrical_type( ToProtoEnum<ELECTRICAL_PINTYPE, types::ElectricalPinType>( pin->GetType() ) );
        value->set_orientation_degrees( pinAngle( pin->PinDrawOrient( symbol.GetTransform() ) ) );
        value->set_alternate( pin->GetAlt().ToStdString() );
    }
    for( const auto& instance : symbol.GetInstances() )
    {
        auto* value = out.add_instances();
        for( const KIID& id : instance.m_Path ) value->mutable_sheet_path()->add_path()->set_value( id.AsStdString() );
        value->set_reference( instance.m_Reference.ToStdString() );
        value->set_unit( instance.m_Unit );
    }
    return out;
}

std::unique_ptr<SCH_SYMBOL> UnpackSchematicSymbol( const Symbol& input,
        const LIB_SYMBOL& definition, const SCH_SHEET_PATH& path,
        const SCH_SYMBOL* existing, std::string& error )
{
    VECTOR2I position;
    const int unit = input.unit() ? input.unit() : 1;
    const int body = input.body_style() ? input.body_style() : 1;
    if( !UnpackSchematicVector( input.position(), position ) || input.rotation_degrees() % 90
        || unit < 1 || unit > definition.GetUnitCount() || body < 1 || body > definition.GetBodyStyleCount() )
    { error = "Invalid symbol coordinates, quarter-turn rotation, unit or body style"; return nullptr; }
    const LIB_ID id = LibIdFromProto( input.lib_id() );
    std::unique_ptr<SCH_SYMBOL> symbol;
    if( existing )
    {
        symbol = std::make_unique<SCH_SYMBOL>( *existing );
        if( id != existing->GetLibId() )
        {
            symbol->SetLibId( id );
            symbol->SetLibSymbol( definition.Flatten().release() );
        }
        symbol->SetPosition( position );
    }
    else symbol = std::make_unique<SCH_SYMBOL>( definition, id, &path, unit, body, position );
    if( !input.id().value().empty() )
    {
        if( !KIID::SniffTest( wxString::FromUTF8( input.id().value() ) ) || KIID( input.id().value() ) == niluuid )
        { error = "Invalid symbol UUID"; return nullptr; }
        const_cast<KIID&>( symbol->m_Uuid ) = KIID( input.id().value() );
    }
    int angle = ( input.rotation_degrees() % 360 + 360 ) % 360;
    symbol->SetOrientation( ( angle == 90 ? SYM_ORIENT_90 : angle == 180 ? SYM_ORIENT_180
                               : angle == 270 ? SYM_ORIENT_270 : SYM_ORIENT_0 )
                           | ( input.mirror_x() ? SYM_MIRROR_X : 0 ) | ( input.mirror_y() ? SYM_MIRROR_Y : 0 ) );
    symbol->SetUnit( unit ); symbol->SetUnitSelection( &path, unit ); symbol->SetBodyStyle( body );
    if( input.has_dnp() ) symbol->SetDNP( input.dnp(), &path );
    if( input.has_in_bom() ) symbol->SetExcludedFromBOM( !input.in_bom(), &path );
    if( input.has_on_board() ) symbol->SetExcludedFromBoard( !input.on_board(), &path );
    std::set<std::string> names;
    for( const auto& field : input.fields() )
    {
        if( field.name().empty() || !names.insert( field.name() ).second )
        { error = "Symbol fields require unique nonempty canonical names"; return nullptr; }
        SCH_FIELD* target = nullptr;
        for( auto& candidate : symbol->GetFields() )
            if( candidate.GetCanonicalName() == wxString::FromUTF8( field.name() ) ) target = &candidate;
        if( !target ) target = symbol->AddField( SCH_FIELD( symbol.get(), FIELD_T::USER, wxString::FromUTF8( field.name() ) ) );
        if( !UnpackSchematicText( field.text(), *target, error ) ) return nullptr;
        target->SetVisible( field.visible() );
    }
    // Reference annotations are validated and assigned for every live shared-screen
    // instance by the handler before the symbol is attached or swapped.
    return symbol;
}
