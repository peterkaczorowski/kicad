/* Copyright The KiCad Developers, see AUTHORS.txt.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "api_sch_utils.h"
#include <api/api_utils.h>
#include <api/api_enums.h>
#include <lib_symbol.h>
#include <sch_field.h>
#include <sch_shape.h>
#include <sch_text.h>
#include <sch_textbox.h>
#include <magic_enum.hpp>
#include <cmath>
#include <set>

using namespace kiapi::schematic::types;
using namespace kiapi::common;

namespace
{
int angle( PIN_ORIENTATION value )
{
    switch( value )
    {
    case PIN_ORIENTATION::PIN_RIGHT: return 0;
    case PIN_ORIENTATION::PIN_UP: return 90;
    case PIN_ORIENTATION::PIN_LEFT: return 180;
    case PIN_ORIENTATION::PIN_DOWN: return 270;
    default: return -1;
    }
}
PIN_ORIENTATION orientation( int value )
{
    value = ( value % 360 + 360 ) % 360;
    return value == 90 ? PIN_ORIENTATION::PIN_UP : value == 180 ? PIN_ORIENTATION::PIN_LEFT
                        : value == 270 ? PIN_ORIENTATION::PIN_DOWN : PIN_ORIENTATION::PIN_RIGHT;
}
bool dimension( int64_t value, int& out, bool positive = false )
{
    if( value < ( positive ? 1 : 0 ) || value > int64_t( INT_MAX ) / 4 * 100 ) return false;
    out = std::llround( value / 100.0 ); return !positive || out > 0;
}
}

std::optional<LibrarySymbolDefinition> PackLibrarySymbol( const LIB_SYMBOL& original, std::string& error )
{
    auto flattened = original.Flatten();
    const LIB_SYMBOL& symbol = *flattened;
    LibrarySymbolDefinition out;
    *out.mutable_lib_id() = LibIdToProto( original.GetLibId() );
    out.set_unit_count( symbol.GetUnitCount() ); out.set_body_style_count( symbol.GetBodyStyleCount() );
    out.set_units_locked( symbol.UnitsLocked() ); out.set_demorgan_body_styles( symbol.HasDeMorganBodyStyles() );
    out.set_global_power( symbol.IsGlobalPower() ); out.set_local_power( symbol.IsLocalPower() );
    out.set_keywords( symbol.GetKeyWords().ToStdString() );
    out.set_exclude_from_bom( symbol.GetExcludedFromBOM() ); out.set_exclude_from_board( symbol.GetExcludedFromBoard() );
    out.set_exclude_from_sim( symbol.GetExcludedFromSim() ); out.set_exclude_from_position_files( symbol.GetExcludedFromPosFiles() );
    out.set_pin_names_hidden( !symbol.GetShowPinNames() ); out.set_pin_numbers_hidden( !symbol.GetShowPinNumbers() );
    out.set_pin_name_offset_nm( int64_t( symbol.GetPinNameOffset() ) * 100 );
    out.set_duplicate_pin_numbers_are_jumpers( symbol.GetDuplicatePinNumbersAreJumpers() );
    for( const auto& [unit, name] : symbol.GetUnitDisplayNames() ) (*out.mutable_unit_names())[unit] = name.ToStdString();
    for( const auto& group : symbol.JumperPinGroups() )
    { auto* value = out.add_jumper_groups(); for( const auto& n : group ) value->add_numbers( n.ToStdString() ); }
    for( const auto& filter : symbol.GetFPFilters() ) out.add_footprint_filters( filter.ToStdString() );
    for( const auto& name : symbol.GetBodyStyleNames() ) out.add_body_style_names( name.ToStdString() );
    std::vector<SCH_FIELD*> fields; symbol.GetFields( fields );
    for( const auto* field : fields )
    {
        auto* value = out.add_fields(); value->set_name( field->GetCanonicalName().ToStdString() );
        PackSchematicText( *value->mutable_text(), *field ); value->set_visible( field->IsVisible() );
    }
    for( const SCH_PIN* pin : symbol.GetGraphicalPins() )
    {
        auto* value = out.add_pins(); value->set_number( pin->GetNumber().ToStdString() ); value->set_name( pin->GetName().ToStdString() );
        PackSchematicVector( *value->mutable_position(), pin->GetPosition() );
        value->set_electrical_type( ToProtoEnum<ELECTRICAL_PINTYPE, types::ElectricalPinType>( pin->GetType() ) );
        value->set_orientation_degrees( angle( pin->GetOrientation() ) );
        value->set_length_nm( int64_t( pin->GetLength() ) * 100 );
        value->set_unit( pin->GetUnit() ); value->set_body_style( pin->GetBodyStyle() );
        value->set_shape( int( pin->GetShape() ) ); value->set_visible( pin->IsVisible() );
        value->set_name_size_nm( int64_t( pin->GetNameTextSize() ) * 100 ); value->set_number_size_nm( int64_t( pin->GetNumberTextSize() ) * 100 );
        for( const auto& [name, alt] : pin->GetAlternates() )
        {
            auto* alternate = value->add_alternates(); alternate->set_name( name.ToStdString() );
            alternate->set_shape( int( alt.m_Shape ) );
            alternate->set_electrical_type( ToProtoEnum<ELECTRICAL_PINTYPE, types::ElectricalPinType>( alt.m_Type ) );
        }
    }
    for( const SCH_ITEM& item : symbol.GetDrawItems() )
    {
        if( item.Type() == SCH_PIN_T || item.Type() == SCH_FIELD_T ) continue;
        auto* graphic = out.add_graphics(); graphic->set_unit( item.GetUnit() ); graphic->set_body_style( item.GetBodyStyle() );
        if( const auto* shape = dynamic_cast<const SCH_SHAPE*>( &item ) )
        { graphic->set_fill_mode( int( shape->GetFillMode() ) ); PackColor( *graphic->mutable_fill_color(), shape->GetFillColor() ); }
        graphic->set_exclude_from_sim( item.GetExcludedFromSim() );
        if( item.Type() == SCH_TEXTBOX_T )
        {
            const auto& box = static_cast<const SCH_TEXTBOX&>( item );
            auto* value = graphic->mutable_text_box();
            PackSchematicVector( *value->mutable_top_left(), box.GetStart() );
            PackSchematicVector( *value->mutable_bottom_right(), box.GetEnd() );
            types::Text text; PackSchematicText( text, box ); *value->mutable_attributes() = text.attributes(); value->set_text( text.text() );
            value->set_border_enabled( box.GetStroke().GetWidth() >= 0 );
            value->mutable_margin_left()->set_value_nm( int64_t( box.GetMarginLeft() ) * 100 );
            value->mutable_margin_top()->set_value_nm( int64_t( box.GetMarginTop() ) * 100 );
            value->mutable_margin_right()->set_value_nm( int64_t( box.GetMarginRight() ) * 100 );
            value->mutable_margin_bottom()->set_value_nm( int64_t( box.GetMarginBottom() ) * 100 );
            box.EDA_SHAPE::Serialize( *graphic->mutable_box_outline() );
            if( !ConvertSchematicUnits( *graphic->mutable_box_outline(), true, error ) ) return std::nullopt;
        }
        else if( item.Type() == SCH_SHAPE_T )
        {
            static_cast<const SCH_SHAPE&>( item ).EDA_SHAPE::Serialize( *graphic->mutable_shape() );
            if( !ConvertSchematicUnits( *graphic->mutable_shape(), true, error ) ) return std::nullopt;
        }
        else if( item.Type() == SCH_TEXT_T ) PackSchematicText( *graphic->mutable_text(), static_cast<const SCH_TEXT&>( item ) );
        else
        { error = "Symbol contains an unsupported library graphic; refusing an incomplete definition"; return std::nullopt; }
    }
    return out;
}

std::unique_ptr<LIB_SYMBOL> UnpackLibrarySymbol( const LibrarySymbolDefinition& input, std::string& error )
{
    auto fail = [&]( const char* message ) -> std::unique_ptr<LIB_SYMBOL> { error = message; return nullptr; };
    LIB_ID id = LibIdFromProto( input.lib_id() );
    if( id.GetLibNickname().empty() || id.GetLibItemName().empty()
        || LIB_ID::FindIllegalLibraryNameChar( id.GetLibNickname() ) != 0
        || LIB_ID::HasIllegalChars( id.GetLibItemName() ) >= 0
        || input.unit_count() < 1 || input.unit_count() > 256
        || input.body_style_count() < 1 || input.body_style_count() > 256 || ( input.global_power() && input.local_power() ) )
        return fail( "Invalid library identifier, unit/body counts or power classification" );
    if( ( input.demorgan_body_styles() && input.body_style_count() != 2 )
        || ( input.body_style_names_size() && input.body_style_names_size() != input.body_style_count() ) )
        return fail( "Body style names/count or De Morgan classification are inconsistent" );
    auto symbol = std::make_unique<LIB_SYMBOL>( id.GetLibItemName() ); symbol->SetLibId( id );
    symbol->SetUnitCount( input.unit_count(), false ); symbol->SetBodyStyleCount( input.body_style_count(), false, false );
    symbol->LockUnits( input.units_locked() ); symbol->SetHasDeMorganBodyStyles( input.demorgan_body_styles() );
    symbol->SetKeyWords( wxString::FromUTF8( input.keywords() ) );
    if( input.global_power() ) symbol->SetGlobalPower(); else if( input.local_power() ) symbol->SetLocalPower();
    symbol->SetExcludedFromBOM( input.exclude_from_bom() ); symbol->SetExcludedFromBoard( input.exclude_from_board() );
    symbol->SetExcludedFromSim( input.exclude_from_sim() ); symbol->SetExcludedFromPosFiles( input.exclude_from_position_files() );
    symbol->SetShowPinNames( !input.pin_names_hidden() ); symbol->SetShowPinNumbers( !input.pin_numbers_hidden() );
    int offset;
    if( !dimension( input.pin_name_offset_nm(), offset ) ) return fail( "Invalid pin name offset" );
    symbol->SetPinNameOffset( offset ); symbol->SetDuplicatePinNumbersAreJumpers( input.duplicate_pin_numbers_are_jumpers() );
    wxArrayString filters; for( const auto& filter : input.footprint_filters() ) filters.Add( wxString::FromUTF8( filter ) ); symbol->SetFPFilters( filters );
    std::vector<wxString> styles;
    for( const auto& style : input.body_style_names() ) styles.push_back( wxString::FromUTF8( style ) );
    if( styles.empty() && !input.demorgan_body_styles() && input.body_style_count() > 1 )
        for( int n = 1; n <= input.body_style_count(); ++n ) styles.push_back( wxString::Format( "Style %d", n ) );
    symbol->SetBodyStyleNames( styles );
    for( const auto& [unit, name] : input.unit_names() )
    { if( unit < 1 || unit > input.unit_count() ) return fail( "Invalid named unit" ); symbol->GetUnitDisplayNames()[unit] = wxString::FromUTF8( name ); }
    for( const auto& group : input.jumper_groups() )
    { std::set<wxString> values; for( const auto& n : group.numbers() ) values.insert( wxString::FromUTF8( n ) ); symbol->JumperPinGroups().push_back( values ); }
    std::set<std::string> names;
    std::vector<SCH_FIELD*> defaultFields; symbol->GetFields( defaultFields );
    std::vector<SCH_FIELD> fields; for( const auto* field : defaultFields ) fields.push_back( *field );
    for( const auto& field : input.fields() )
    {
        if( field.name().empty() || !names.insert( field.name() ).second ) return fail( "Duplicate or empty symbol field name" );
        SCH_FIELD* target = nullptr;
        for( auto& native : fields ) if( native.GetCanonicalName() == wxString::FromUTF8( field.name() ) ) target = &native;
        if( !target ) { fields.emplace_back( symbol.get(), FIELD_T::USER, wxString::FromUTF8( field.name() ) ); target = &fields.back(); }
        if( !UnpackSchematicText( field.text(), *target, error ) ) return nullptr;
        target->SetVisible( field.visible() );
    }
    symbol->SetFields( fields );
    auto validUnit = [&]( int unit, int body ) { return unit >= 0 && unit <= input.unit_count() && body >= 0 && body <= input.body_style_count(); };
    std::vector<const LibraryPin*> pins;
    for( const auto& pin : input.pins() )
    {
        VECTOR2I position; int length, nameSize, numberSize;
        if( pin.number().empty() || !validUnit( pin.unit(), pin.body_style() ) || pin.orientation_degrees() % 90
            || !UnpackSchematicVector( pin.position(), position ) || !dimension( pin.length_nm(), length )
            || !dimension( pin.name_size_nm(), nameSize ) || !dimension( pin.number_size_nm(), numberSize )
            || pin.electrical_type() < 1 || pin.electrical_type() > 12 || pin.shape() >= GRAPHIC_PINSHAPES_TOTAL )
            return fail( "Invalid library pin: number, unit/body, position, dimensions, orientation, type or shape" );
        for( const auto* previous : pins )
            if( previous->number() == pin.number()
                && ( previous->unit() == 0 || pin.unit() == 0 || previous->unit() == pin.unit() )
                && ( previous->body_style() == 0 || pin.body_style() == 0 || previous->body_style() == pin.body_style() ) )
                return fail( "Ambiguous duplicate pin number in the same unit/body style" );
        pins.push_back( &pin );
        auto native = std::make_unique<SCH_PIN>( symbol.get() );
        native->SetNumber( wxString::FromUTF8( pin.number() ) ); native->SetName( wxString::FromUTF8( pin.name() ) );
        native->SetPosition( position ); native->SetOrientation( orientation( pin.orientation_degrees() ) );
        native->SetType( FromProtoEnum<ELECTRICAL_PINTYPE>( pin.electrical_type() ) );
        native->SetShape( static_cast<GRAPHIC_PINSHAPE>( pin.shape() ) ); native->SetVisible( pin.visible() );
        native->SetLength( length ); native->SetNameTextSize( nameSize ); native->SetNumberTextSize( numberSize );
        native->SetUnit( pin.unit() ); native->SetBodyStyle( pin.body_style() );
        for( const auto& alt : pin.alternates() )
        {
            wxString name = wxString::FromUTF8( alt.name() );
            if( name.empty() || native->GetAlternates().count( name ) || alt.electrical_type() < 1
                || alt.electrical_type() > 12 || alt.shape() >= GRAPHIC_PINSHAPES_TOTAL ) return fail( "Invalid alternate pin definition" );
            native->GetAlternates()[name] = { name, static_cast<GRAPHIC_PINSHAPE>( alt.shape() ), FromProtoEnum<ELECTRICAL_PINTYPE>( alt.electrical_type() ) };
        }
        symbol->AddDrawItem( native.release() );
    }
    for( const auto& graphic : input.graphics() )
    {
        if( !validUnit( graphic.unit(), graphic.body_style() ) ) return fail( "Invalid graphic unit or body style" );
        std::unique_ptr<SCH_ITEM> item;
        if( graphic.fill_mode() > 7 ) return fail( "Invalid native schematic fill mode" );
        if( graphic.has_text_box() )
        {
            const auto& value = graphic.text_box();
            auto box = std::make_unique<SCH_TEXTBOX>();
            types::Text text; *text.mutable_attributes() = value.attributes(); text.set_text( value.text() );
            *text.mutable_position() = value.top_left();
            if( !UnpackSchematicText( text, *box, error ) ) return nullptr;
            types::GraphicShape outline = graphic.box_outline();
            if( !graphic.has_box_outline() )
            { *outline.mutable_rectangle()->mutable_top_left() = value.top_left(); *outline.mutable_rectangle()->mutable_bottom_right() = value.bottom_right(); }
            if( !ConvertSchematicUnits( outline, false, error ) || !box->EDA_SHAPE::Deserialize( outline ) ) return fail( "Invalid library text box outline" );
            int left, top, right, bottom;
            if( !dimension( value.margin_left().value_nm(), left ) || !dimension( value.margin_top().value_nm(), top )
                || !dimension( value.margin_right().value_nm(), right ) || !dimension( value.margin_bottom().value_nm(), bottom ) ) return fail( "Invalid text box margins" );
            box->SetMarginLeft( left ); box->SetMarginTop( top ); box->SetMarginRight( right ); box->SetMarginBottom( bottom );
            if( !value.border_enabled() ) { auto stroke = box->GetStroke(); stroke.SetWidth( -1 ); box->SetStroke( stroke ); }
            item = std::move( box );
        }
        else if( graphic.has_shape() )
        {
            auto shape = std::make_unique<SCH_SHAPE>(); types::GraphicShape value = graphic.shape();
            if( !ConvertSchematicUnits( value, false, error ) || !shape->EDA_SHAPE::Deserialize( value ) ) return fail( "Invalid library shape" );
            item = std::move( shape );
        }
        else if( graphic.has_text() )
        {
            auto text = std::make_unique<SCH_TEXT>();
            if( !UnpackSchematicText( graphic.text(), *text, error ) ) return nullptr;
            item = std::move( text );
        }
        else return fail( "Unsupported or empty library graphic" );
        if( auto* shape = dynamic_cast<SCH_SHAPE*>( item.get() ) )
        { if( graphic.fill_mode() ) shape->SetFillMode( static_cast<FILL_T>( graphic.fill_mode() ) );
          if( graphic.has_fill_color() ) shape->SetFillColor( UnpackColor( graphic.fill_color() ) ); }
        item->SetExcludedFromSim( graphic.exclude_from_sim() );
        item->SetUnit( graphic.unit() ); item->SetBodyStyle( graphic.body_style() ); item->SetParent( symbol.get() );
        symbol->AddDrawItem( item.release() );
    }
    return symbol;
}
