/* Copyright The KiCad Developers. SPDX-License-Identifier: GPL-3.0-or-later */
#include <api/api_sch_net_rename.h>
#include <api/api_sch_connectivity.h>
#include <schematic.h>
#include <connection_graph.h>
#include <sch_screen.h>
#include <sch_sheet.h>
#include <sch_sheet_pin.h>
#include <sch_symbol.h>
#include <sch_pin.h>
#include <sch_label.h>
#include <sch_validators.h>
#include <string_utils.h>
#include <map>
#include <set>

using namespace kiapi::schematic::types;
namespace
{
std::string key( const NetMember& member )
{
    std::string result;
    for( const auto& id : member.document().sheet_path().path() ) result += id.value() + "/";
    return result + member.id().value();
}
std::string key( const SCH_SHEET_PATH& path, const SCH_ITEM* item )
{
    std::string result;
    for( const auto& id : path.Path() ) result += id.AsStdString() + "/";
    return result + item->m_Uuid.AsStdString();
}
std::set<std::string> members( const SchematicNet& net )
{
    std::set<std::string> result;
    for( const auto& item : net.members() ) result.insert( key( item ) );
    return result;
}
// Destruction always restores literal text, including when graph generation
// throws. Explicit Restore also rebuilds the original graph before returning.
class PREVIEW_TEXT_GUARD
{
public:
    PREVIEW_TEXT_GUARD( SCHEMATIC& schematic, const std::vector<SCHEMATIC_NET_RENAME_EDIT>& edits ) :
        m_schematic( schematic ), m_edits( edits ) {}
    ~PREVIEW_TEXT_GUARD()
    {
        if( !m_restored )
            for( const auto& edit : m_edits ) edit.item->SetText( edit.before );
    }
    void Restore()
    {
        for( const auto& edit : m_edits ) edit.item->SetText( edit.before );
        m_schematic.ConnectionGraph()->Recalculate( m_schematic.Hierarchy(), true );
        // Restore per-instance dangling flags just as the normal snapshot path does.
        BuildSchematicConnectivity( m_schematic );
        m_restored = true;
    }
private:
    bool m_restored = false;
    SCHEMATIC& m_schematic;
    const std::vector<SCHEMATIC_NET_RENAME_EDIT>& m_edits;
};
}

SCHEMATIC_NET_RENAME_ANALYSIS PlanSchematicNetRename(
        SCHEMATIC& schematic, const ConnectivitySnapshot& snapshot,
        const std::string& netId, const std::string& newName )
{
    SCHEMATIC_NET_RENAME_ANALYSIS result;
    auto& plan = result.plan;
    *plan.mutable_document() = snapshot.document();
    plan.set_session_id( snapshot.session_id() );
    plan.set_revision( snapshot.revision() );
    plan.set_net_id( netId );
    plan.set_new_name( newName );
    plan.set_alias_policy( "all_identical_literal_labels_and_ports_in_entire_hierarchy" );
    std::set<std::string> blockers;
    auto block = [&]( const std::string& text )
    {
        if( blockers.insert( text ).second ) plan.add_blockers( text );
    };
    const SchematicNet* source = nullptr;
    for( const auto& net : snapshot.nets() ) if( net.id() == netId ) source = &net;
    if( !source ) { block( "Source net ID not found or stale" ); return result; }
    plan.set_old_name( source->name() );
    wxString newText = wxString::FromUTF8( newName );
    SCH_NETNAME_VALIDATOR validator( false );
    wxString invalid = validator.IsValid( newText );
    SCH_CONNECTION candidate( schematic.ConnectionGraph() );
    candidate.ConfigureFromLabel( EscapeString( newText, CTX_NETNAME ) );
    if( newText.empty() || !invalid.empty() || newText.Contains( '/' ) || newText.Contains( "${" )
        || candidate.IsBus() )
        block( "New name must be a valid literal scalar label name without paths or text variables" );
    if( source->is_bus() ) block( "Bus renaming is not supported" );
    for( const auto& net : snapshot.nets() )
    {
        for( const auto& id : net.bus_member_ids() )
            if( id == netId ) block( "Renaming bus members requires editing bus definitions and is not supported" );
        if( net.id() == netId ) continue;
        wxString name = UnescapeString( wxString::FromUTF8( net.name() ) );
        bool collision = name == newText || name.AfterLast( '/' ) == newText;
        for( const auto& alias : net.aliases() )
            collision |= UnescapeString( wxString::FromUTF8( alias ) ) == newText;
        for( const auto& member : net.members() ) collision |= member.text() == newName;
        if( collision ) *plan.add_collisions() = net;
    }
    if( plan.collisions_size() ) block( "Target name already exists in this hierarchy; merging is forbidden" );

    std::map<std::string, std::pair<SCH_ITEM*, SCH_SCREEN*>> items;
    for( const auto& path : schematic.Hierarchy() )
        for( SCH_ITEM* item : path.LastScreen()->Items() )
        {
            items[key( path, item )] = { item, path.LastScreen() };
            if( auto* sheet = dynamic_cast<SCH_SHEET*>( item ) )
                for( auto* pin : sheet->GetPins() ) items[key( path, pin )] = { pin, path.LastScreen() };
            if( auto* symbol = dynamic_cast<SCH_SYMBOL*>( item ) )
                for( auto* pin : symbol->GetPins( &path ) ) items[key( path, pin )] = { pin, path.LastScreen() };
        }
    std::set<SCH_LABEL_BASE*> edited;
    std::set<wxString> literalNames;
    for( const auto& member : source->members() )
    {
        auto found = items.find( key( member ) );
        if( found == items.end() ) { block( "A source member is missing from the live model" ); continue; }
        auto* item = found->second.first;
        if( auto* pin = dynamic_cast<SCH_PIN*>( item ) )
            if( pin->IsGlobalPower() || pin->IsLocalPower() )
                block( "Power-symbol naming sources cannot be renamed by this operation" );
        bool supported = item->Type() == SCH_LABEL_T || item->Type() == SCH_GLOBAL_LABEL_T
                         || item->Type() == SCH_HIER_LABEL_T || item->Type() == SCH_SHEET_PIN_T;
        if( !supported ) continue;
        auto* label = static_cast<SCH_LABEL_BASE*>( item );
        if( label->HasTextVars() || label->GetText().ToStdString() != member.text() )
            block( "Variable or expanded naming sources cannot be renamed" );
        literalNames.insert( label->GetText() );
        auto* change = plan.add_changes();
        *change->mutable_item() = member;
        change->set_before( label->GetText().ToStdString() );
        change->set_after( newName );
        if( edited.insert( label ).second )
            result.edits.push_back( {label, found->second.second, label->GetText(), newText} );
    }
    if( result.edits.empty() ) block( "Generated net names without editable labels or ports are not supported" );
    if( literalNames.size() > 1 ) block( "Different literal aliases are not supported; no partial alias rename is performed" );
    if( literalNames.size() == 1 && *literalNames.begin() == newText ) block( "Source already has the requested literal name" );
    if( plan.blockers_size() ) return result;

    const auto before = BuildSchematicConnectivity( schematic );
    const auto sourceMembers = members( *source );
    const SchematicNet* beforeSource = nullptr;
    for( const auto& net : before.nets() )
        if( net.name() == source->name() && members( net ) == sourceMembers ) beforeSource = &net;
    if( !beforeSource ) { block( "Source changed during preview" ); return result; }
    PREVIEW_TEXT_GUARD guard( schematic, result.edits );
    try
    {
        for( const auto& edit : result.edits ) edit.item->SetText( edit.after );
        schematic.ConnectionGraph()->Recalculate( schematic.Hierarchy(), true );
        const auto after = BuildSchematicConnectivity( schematic );
        const SchematicNet* afterSource = nullptr;
        std::map<std::string, const SchematicNet*> afterNets;
        for( const auto& net : after.nets() )
        {
            afterNets[net.id()] = &net;
            if( members( net ) == sourceMembers ) afterSource = &net;
        }
        if( !afterSource || before.nets_size() != after.nets_size() )
            block( "Preview would merge or split networks; rename refused" );
        else
            plan.set_resolved_name( afterSource->name() );
        for( const auto& net : before.nets() )
        {
            if( &net == beforeSource ) continue;
            auto found = afterNets.find( net.id() );
            if( found == afterNets.end() || net.SerializeAsString() != found->second->SerializeAsString() )
            {
                block( "Preview changes another net, a shared sheet instance, or a bus definition; rename refused" );
                break;
            }
        }
        guard.Restore();
    }
    catch( ... )
    {
        guard.Restore();
        throw;
    }
    plan.set_can_apply( plan.blockers_size() == 0 );
    return result;
}
