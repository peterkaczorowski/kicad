/* Copyright The KiCad Developers. SPDX-License-Identifier: GPL-3.0-or-later */
#include <api/api_sch_connectivity.h>
#include <connection_graph.h>
#include <schematic.h>
#include <sch_symbol.h>
#include <sch_pin.h>
#include <sch_sheet.h>
#include <sch_screen.h>
#include <sch_sheet_pin.h>
#include <sch_line.h>
#include <sch_label.h>
#include <project.h>
#include <map>
#include <set>

using namespace kiapi::schematic::types;

namespace
{
std::string itemKey( const SCH_SHEET_PATH& path, SCH_ITEM* item )
{
    return path.Path().AsString().ToStdString() + item->m_Uuid.AsStdString();
}

void packDocument( kiapi::common::types::DocumentSpecifier& doc, const SCH_SHEET_PATH& path,
                   SCHEMATIC& schematic )
{
    doc.set_type( kiapi::common::types::DOCTYPE_SCHEMATIC );
    for( const KIID& id : path.Path() )
        doc.mutable_sheet_path()->add_path()->set_value( id.AsStdString() );
    doc.mutable_sheet_path()->set_path_human_readable( path.PathHumanReadable().ToStdString() );
    doc.mutable_project()->set_name( schematic.Project().GetProjectName().ToStdString() );
    doc.mutable_project()->set_path( schematic.Project().GetProjectPath().ToStdString() );
}

NetMember packMember( SCH_ITEM* item, const SCH_SHEET_PATH& path, SCHEMATIC& schematic,
                     bool explicitNC )
{
    NetMember out;
    out.mutable_id()->set_value( item->m_Uuid.AsStdString() );
    packDocument( *out.mutable_document(), path, schematic );
    out.mutable_position()->set_x_nm( int64_t( item->GetPosition().x ) * 100 );
    out.mutable_position()->set_y_nm( int64_t( item->GetPosition().y ) * 100 );
    out.set_dangling( item->IsDangling() );
    out.set_no_connect( explicitNC );
    switch( item->Type() )
    {
    case SCH_PIN_T:
    {
        auto* pin = static_cast<SCH_PIN*>( item );
        auto* symbol = dynamic_cast<SCH_SYMBOL*>( pin->GetParentSymbol() );
        out.set_kind( "pin" );
        out.set_pin_number( pin->GetShownNumber().ToStdString() );
        out.set_pin_name( pin->GetShownName().ToStdString() );
        out.set_electrical_type( pin->GetCanonicalElectricalTypeName().ToStdString() );
        out.set_no_connect( explicitNC || pin->GetType() == ELECTRICAL_PINTYPE::PT_NC
                            || pin->GetType() == ELECTRICAL_PINTYPE::PT_NIC );
        if( symbol )
        {
            out.mutable_symbol_id()->set_value( symbol->m_Uuid.AsStdString() );
            out.set_reference( symbol->GetRef( &path ).ToStdString() );
            out.set_unit( symbol->GetUnitSelection( &path ) );
            out.set_value( symbol->GetValue( true, &path, false ).ToStdString() );
        }
        break;
    }
    case SCH_LINE_T:
    {
        auto* wire = static_cast<SCH_LINE*>( item );
        out.set_kind( wire->IsBus() ? "bus" : "wire" );
        out.mutable_end_position()->set_x_nm( int64_t( wire->GetEndPoint().x ) * 100 );
        out.mutable_end_position()->set_y_nm( int64_t( wire->GetEndPoint().y ) * 100 );
        break;
    }
    case SCH_LABEL_T: out.set_kind( "label" ); break;
    case SCH_GLOBAL_LABEL_T: out.set_kind( "global_label" ); break;
    case SCH_HIER_LABEL_T: out.set_kind( "hierarchical_label" ); break;
    case SCH_SHEET_PIN_T: out.set_kind( "sheet_pin" ); break;
    case SCH_NO_CONNECT_T: out.set_kind( "no_connect" ); break;
    case SCH_JUNCTION_T: out.set_kind( "junction" ); break;
    default: out.set_kind( "connection" ); break;
    }
    if( auto* label = dynamic_cast<SCH_LABEL_BASE*>( item ) )
        out.set_text( label->GetShownText( &path, false ).ToStdString() );
    return out;
}
}

ConnectivitySnapshot BuildSchematicConnectivity( SCHEMATIC& schematic )
{
    std::map<std::string, SchematicNet> nets;
    std::map<std::string, std::map<std::string, NetMember>> members;
    std::map<std::string, std::set<std::string>> aliases;
    std::set<std::string> visited;
    std::map<std::string, std::set<std::string>> memberNets;
    auto ensureNet = [&]( const std::string& key, const wxString& name, bool bus ) -> SchematicNet&
    {
        auto& net = nets[key];
        net.set_id( key );
        net.set_name( name.ToStdString() );
        net.set_is_bus( bus );
        return net;
    };

    // Preserve native name+code separation even for ambiguous/empty names.
    // Do not include transient net codes in ordinary snapshot identity.
    std::map<std::string, int> nameCounts;
    std::map<std::pair<std::string, int>, std::string> nativeKeys;
    for( const auto& [key, graphs] : schematic.ConnectionGraph()->GetNetMap() )
        ++nameCounts[key.Name.ToStdString()];
    for( const auto& [key, graphs] : schematic.ConnectionGraph()->GetNetMap() )
    {
        std::string name = key.Name.ToStdString();
        std::string netKey = "N:" + name;
        if( nameCounts[name] > 1 )
        {
            std::set<std::string> identities;
            for( auto* graph : graphs )
                for( SCH_ITEM* item : graph->GetItems() ) identities.insert( itemKey( graph->GetSheet(), item ) );
            netKey += "|" + ( identities.empty() ? std::to_string( key.Netcode ) : *identities.begin() );
        }
        nativeKeys[{name, key.Netcode}] = netKey;
    }

    // Use exactly the native netlist exporter's grouping. Sheet instances are
    // part of member identity: UUID alone is insufficient with shared screens.
    for( const auto& [key, subgraphs] : schematic.ConnectionGraph()->GetNetMap() )
    {
        std::string netKey = nativeKeys.at( {key.Name.ToStdString(), key.Netcode} );
        ensureNet( netKey, key.Name, false );
        for( auto* graph : subgraphs )
        {
            const auto& path = graph->GetSheet();
            bool nc = graph->GetNoConnect() && graph->GetNoConnect()->Type() == SCH_NO_CONNECT_T;
            for( SCH_ITEM* item : graph->GetItems() )
            {
                auto* connection = item->Connection( &path );
                if( connection && connection->IsBus() )
                    continue;
                auto priority = CONNECTION_SUBGRAPH::GetDriverPriority( item );
                if( priority >= CONNECTION_SUBGRAPH::PRIORITY::SHEET_PIN )
                    aliases[netKey].insert( graph->GetNameForDriver( item ).ToStdString() );
                members[netKey][itemKey( path, item )] = packMember( item, path, schematic, nc && item->Type() == SCH_PIN_T
                        && graph->GetNoConnect()->GetPosition() == item->GetPosition() );
                visited.insert( itemKey( path, item ) );
                memberNets[itemKey( path, item )].insert( netKey );
            }
        }
    }

    for( const auto& path : schematic.Hierarchy() )
    {
        // Dangling flags are per live instance; restore the active one below.
        path.LastScreen()->TestDanglingEnds( &path );
        std::vector<SCH_ITEM*> items;
        for( SCH_ITEM* item : path.LastScreen()->Items() )
        {
            if( auto* symbol = dynamic_cast<SCH_SYMBOL*>( item ) )
            {
                for( auto* pin : symbol->GetPins( &path ) ) items.push_back( pin );
            }
            else if( auto* sheet = dynamic_cast<SCH_SHEET*>( item ) )
            {
                for( auto* pin : sheet->GetPins() ) items.push_back( pin );
            }
            else if( item->IsConnectable() )
                items.push_back( item );
        }
        for( SCH_ITEM* item : items )
        {
            auto* conn = item->Connection( &path );
            if( conn && conn->IsBus() )
            {
                std::string busKey = "B:" + conn->Name().ToStdString();
                auto& bus = ensureNet( busKey, conn->Name(), true );
                std::set<std::string> leafKeys;
                for( const auto& leaf : conn->AllMembers() )
                {
                    if( !leaf->IsNet() ) continue;
                    std::string leafKey = "N:" + leaf->Name().ToStdString();
                    auto native = nativeKeys.find( { leaf->Name().ToStdString(), leaf->NetCode() } );
                    if( native != nativeKeys.end() ) leafKey = native->second;
                    ensureNet( leafKey, leaf->Name(), false );
                    leafKeys.insert( leafKey );
                }
                // All copies of this resolved bus have the same membership.
                bus.clear_bus_member_ids();
                for( const auto& key : leafKeys ) bus.add_bus_member_ids( key );
                members[busKey][itemKey( path, item )] = packMember( item, path, schematic, false );
                if( auto* label = dynamic_cast<SCH_LABEL_BASE*>( item ) )
                    aliases[busKey].insert( label->GetShownText( &path, false ).ToStdString() );
            }
            else if( !visited.count( itemKey( path, item ) ) )
            {
                // Include isolated pins and NC symbols even when no native net
                // was generated. These are essential to the connection table.
                std::string key = conn && conn->IsNet() ? "N:" + conn->Name().ToStdString()
                                                       : "U:" + itemKey( path, item );
                ensureNet( key, conn && conn->IsNet() ? conn->Name() : wxString(), false );
                members[key][itemKey( path, item )] = packMember( item, path, schematic, false );
            }
            else
            {
                // Refresh dangling state in the correct instance context.
                for( const auto& key : memberNets[itemKey( path, item )] )
                    members[key][itemKey( path, item )].set_dangling( item->IsDangling() );
            }
        }
    }
    if( schematic.CurrentSheet().LastScreen() )
        schematic.CurrentSheet().LastScreen()->TestDanglingEnds( &schematic.CurrentSheet() );

    ConnectivitySnapshot result;
    for( auto& [key, net] : nets )
    {
        for( const auto& alias : aliases[key] ) net.add_aliases( alias );
        for( const auto& [id, member] : members[key] ) *net.add_members() = member;
        *result.add_nets() = net;
    }
    return result;
}

void StampSchematicConnectivity( ConnectivitySnapshot& snapshot, const std::string& session,
                                uint64_t revision )
{
    snapshot.set_session_id( session );
    snapshot.set_revision( revision );
    std::map<std::string, std::string> ids;
    for( int i = 0; i < snapshot.nets_size(); ++i )
        ids[snapshot.nets( i ).id()] = session + "/" + std::to_string( revision ) + "/" + std::to_string( i );
    for( auto& net : *snapshot.mutable_nets() )
    {
        net.set_id( ids.at( net.id() ) );
        for( auto& memberId : *net.mutable_bus_member_ids() ) memberId = ids.at( memberId );
    }
}
