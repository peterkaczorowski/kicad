/* Copyright The KiCad Developers, see AUTHORS.txt. SPDX-License-Identifier: GPL-3.0-or-later */
#include <api/api_handler_pcb.h>
#include <api/api_enums.h>
#include <api/api_utils.h>
#include <board.h>
#include <board_commit.h>
#include <connectivity/connectivity_data.h>
#include <connectivity/connectivity_items.h>
#include <footprint.h>
#include <footprint_library_adapter.h>
#include <netinfo.h>
#include <pad.h>
#include <pcb_draw_panel_gal.h>
#include <pcb_edit_frame.h>
#include <project.h>
#include <project_pcb.h>
#include <ratsnest/ratsnest_data.h>
#include <view/view.h>
#include <netlist_reader/pcb_netlist.h>
#include <netlist_reader/netlist_reader.h>
#include <netlist_reader/board_netlist_updater.h>
#include <richio.h>
#include <reporter.h>
#include <refdes_utils.h>
#include <kiway.h>
#include <kiface_ids.h>
#include <kiface_base.h>
#include <wx/filename.h>
#include <cmath>

using namespace kiapi;
using namespace kiapi::common;
using namespace kiapi::board::commands;

namespace
{
FootprintPlacementResponse packFootprint( const FOOTPRINT& footprint )
{
    FootprintPlacementResponse reply;
    google::protobuf::Any packed;
    footprint.Serialize( packed );
    packed.UnpackTo( reply.mutable_footprint() );
    for( const PAD* pad : footprint.Pads() )
    {
        auto* value = reply.add_pads();
        value->mutable_id()->set_value( pad->m_Uuid.AsStdString() );
        value->mutable_footprint_id()->set_value( footprint.m_Uuid.AsStdString() );
        value->set_reference( footprint.GetReference().ToStdString() );
        value->set_number( pad->GetNumber().ToStdString() );
        value->mutable_position()->set_x_nm( pad->GetPosition().x );
        value->mutable_position()->set_y_nm( pad->GetPosition().y );
        LSET copper = pad->GetLayerSet();
        copper &= LSET::AllCuMask();
        copper &= footprint.GetBoard()->GetEnabledLayers();
        for( PCB_LAYER_ID layer : copper.Seq() )
            value->add_copper_layers( ToProtoEnum<PCB_LAYER_ID, kiapi::board::types::BoardLayer>( layer ) );
        value->mutable_net()->set_name( pad->GetNetname().ToStdString() );
    }
    return reply;
}
}

void API_HANDLER_PCB::registerIntegrationHandlers()
{
    registerHandler<UpdatePcbFromSchematic, UpdatePcbFromSchematicResponse>( &API_HANDLER_PCB::handleUpdatePcbFromSchematic );
    registerHandler<PlaceFootprint, FootprintPlacementResponse>( &API_HANDLER_PCB::handlePlaceFootprint );
    registerHandler<GetFootprintPads, FootprintPlacementResponse>( &API_HANDLER_PCB::handleGetFootprintPads );
    registerHandler<GetRatsnest, RatsnestResponse>( &API_HANDLER_PCB::handleGetRatsnest );
}

HANDLER_RESULT<FootprintPlacementResponse> API_HANDLER_PCB::handleGetFootprintPads(
        const HANDLER_CONTEXT<GetFootprintPads>& context )
{
    const auto& request = context.Request;
    if( auto valid = validateDocument( request.document() ); !valid ) return tl::unexpected( valid.error() );
    if( request.document().project().path().empty() ) return documentError( "An explicit PCB project path is required" );
    if( auto busy = checkForBusy() ) return tl::unexpected( *busy );
    FOOTPRINT* found = nullptr;
    for( FOOTPRINT* footprint : frame()->GetBoard()->Footprints() )
    {
        const bool matches = request.has_footprint_id() ? footprint->m_Uuid.AsStdString() == request.footprint_id().value()
                           : !request.reference().empty() && footprint->GetReference().CmpNoCase( wxString::FromUTF8( request.reference() ) ) == 0;
        if( matches )
        {
            if( found ) return documentError( "Ambiguous footprint reference; select a footprint UUID" );
            found = footprint;
        }
    }
    if( !found ) return documentError( "Footprint does not exist on the selected board" );
    return packFootprint( *found );
}

HANDLER_RESULT<FootprintPlacementResponse> API_HANDLER_PCB::handlePlaceFootprint(
        const HANDLER_CONTEXT<PlaceFootprint>& context )
{
    const auto& request = context.Request;
    if( auto valid = validateDocument( request.header().document() ); !valid ) return tl::unexpected( valid.error() );
    if( request.header().document().project().path().empty() ) return documentError( "An explicit PCB project path is required" );
    if( auto busy = checkForWrite( context.ClientName ) ) return tl::unexpected( *busy );
    if( request.header().has_container() || request.header().field_mask().paths_size() )
        return documentError( "PlaceFootprint targets the board and does not accept item masks" );
    if( !std::isfinite( request.rotation_degrees() ) || request.position().x_nm() < -INT_MAX / 4 || request.position().x_nm() > INT_MAX / 4
        || request.position().y_nm() < -INT_MAX / 4 || request.position().y_nm() > INT_MAX / 4 ) return documentError( "Invalid footprint position or rotation" );
    const LIB_ID id = LibIdFromProto( request.lib_id() );
    if( !id.IsValid() || id.GetLibNickname().empty() || id.GetLibItemName().empty() )
        return documentError( "A complete library:footprint identifier is required" );
    BOARD* board = frame()->GetBoard();
    FOOTPRINT* previous = nullptr;
    if( request.has_replace_id() )
    {
        auto item = getItemById( KIID( request.replace_id().value() ) );
        previous = item ? dynamic_cast<FOOTPRINT*>( *item ) : nullptr;
        if( !previous ) return documentError( "Replacement footprint UUID does not exist" );
        if( previous->IsLocked() && !request.override_locks() ) return documentError( "Replacement footprint is locked" );
    }
    PCB_LAYER_ID side = request.side() == kiapi::board::types::BL_UNKNOWN
                       ? ( previous ? previous->GetLayer() : F_Cu )
                       : FromProtoEnum<PCB_LAYER_ID, kiapi::board::types::BoardLayer>( request.side() );
    if( side != F_Cu && side != B_Cu ) return documentError( "Footprint side must be F.Cu or B.Cu" );
    if( !board->GetEnabledLayers().test( side ) ) return documentError( "Requested footprint side is disabled" );
    const wxString reference = request.has_reference() ? wxString::FromUTF8( request.reference() )
                              : previous ? previous->GetReference() : wxString();
    if( request.has_reference() && ( reference.empty() || reference.Find( '\n' ) != wxNOT_FOUND || reference.Find( '\t' ) != wxNOT_FOUND ) )
        return documentError( "Invalid footprint reference" );
    for( FOOTPRINT* footprint : board->Footprints() )
        if( footprint != previous && !reference.empty() && footprint->GetReference().CmpNoCase( reference ) == 0 )
            return documentError( "Footprint reference is already in use" );
    std::unique_ptr<FOOTPRINT> footprint;
    try
    {
        auto* adapter = PROJECT_PCB::FootprintLibAdapter( &frame()->Prj() );
        if( request.has_replace_id() ) adapter->ReloadLibraryEntry( id.GetLibNickname() );
        auto status = adapter->LoadLibraryEntry( id.GetLibNickname() );
        if( !status || status->load_status != LOAD_STATUS::LOADED )
            return documentError( "Footprint library is unavailable" );
        footprint.reset( frame()->LoadFootprint( id ) );
    }
    catch( const IO_ERROR& error ) { return documentError( error.What().ToStdString() ); }
    if( !footprint ) return documentError( "Footprint does not exist in the selected library" );
    footprint->ClearFlags();
    footprint->SetFPID( id );
    footprint->SetParent( board );
    for( PAD* pad : footprint->Pads() ) pad->SetNetCode( 0 );
    std::map<wxString, NETINFO_ITEM*> padNets;
    if( previous )
        for( const PAD* oldPad : previous->Pads() )
        {
            auto [found, inserted] = padNets.emplace( oldPad->GetNumber(), oldPad->GetNet() );
            if( !inserted && found->second != oldPad->GetNet() )
                return documentError( "Replacement requires unambiguous nets for each pad number" );
            bool matching = false;
            for( const PAD* pad : footprint->Pads() ) if( pad->GetNumber() == oldPad->GetNumber() ) matching = true;
            if( oldPad->GetNetCode() > 0 && !matching ) return documentError( "Replacement would remove a connected pad" );
        }
    auto* commit = static_cast<BOARD_COMMIT*>( getCurrentCommit( context.ClientName ) );
    FOOTPRINT* placed = footprint.release();
    if( previous )
    {
        frame()->ExchangeFootprint( previous, placed, *commit, false, false, false, false, false, false, false, false );
        // GUI exchange matches by geometric similarity. Different library geometry
        // can leave an electrically identical numbered pad unmatched; IPC's
        // replacement contract preserves nets by logical pad number instead.
        for( PAD* pad : placed->Pads() )
        {
            auto found = padNets.find( pad->GetNumber() );
            pad->SetNet( found == padNets.end() ? board->FindNet( 0 ) : found->second );
        }
    }
    else
    {
        placed->ResetUuid();
        placed->RunOnChildren( []( BOARD_ITEM* item ) { item->ResetUuid(); }, RECURSE );
        commit->Add( placed );
    }
    if( request.has_position() ) placed->SetPosition( kiapi::common::UnpackVector2( request.position() ) );
    placed->SetLayerAndFlip( side );
    if( request.has_rotation_degrees() ) placed->SetOrientation( EDA_ANGLE( request.rotation_degrees(), DEGREES_T ) );
    if( !reference.empty() ) placed->SetReference( reference );
    if( request.has_value() ) placed->SetValue( wxString::FromUTF8( request.value() ) );
    board->GetConnectivity()->Update( placed );
    frame()->GetCanvas()->GetView()->Update( placed );
    auto reply = packFootprint( *placed );
    if( !m_activeClients.count( context.ClientName ) ) pushCurrentCommit( context.ClientName, "Place footprint from library via API" );
    frame()->Refresh();
    return reply;
}

HANDLER_RESULT<RatsnestResponse> API_HANDLER_PCB::handleGetRatsnest( const HANDLER_CONTEXT<GetRatsnest>& context )
{
    const auto& request = context.Request;
    if( auto valid = validateDocument( request.document() ); !valid ) return tl::unexpected( valid.error() );
    if( request.document().project().path().empty() ) return documentError( "An explicit PCB project path is required" );
    if( auto busy = checkForBusy() ) return tl::unexpected( *busy );
    BOARD* board = frame()->GetBoard();
    auto* connectivity = board->GetConnectivity().get();
    const PAD* pad = nullptr;
    if( request.has_pad_id() )
    {
        auto item = getItemById( KIID( request.pad_id().value() ) );
        pad = item ? dynamic_cast<PAD*>( *item ) : nullptr;
        if( !pad ) return documentError( "Ratsnest pad UUID does not exist" );
    }
    if( !request.net_name().empty() && !board->FindNet( wxString::FromUTF8( request.net_name() ) ) )
        return documentError( "Ratsnest net does not exist" );
    connectivity->RecalculateRatsnest();
    RatsnestResponse reply;
    const auto append = [&]( const CN_EDGE& edge, const wxString& name )
    {
        if( !request.net_name().empty() && name != wxString::FromUTF8( request.net_name() ) ) return;
        auto* value = reply.add_edges(); value->set_net_name( name.ToStdString() );
        value->mutable_start()->set_x_nm( edge.GetSourcePos().x ); value->mutable_start()->set_y_nm( edge.GetSourcePos().y );
        value->mutable_end()->set_x_nm( edge.GetTargetPos().x ); value->mutable_end()->set_y_nm( edge.GetTargetPos().y );
        if( auto* item = edge.GetSourceNode()->Parent() ) value->mutable_source_id()->set_value( item->m_Uuid.AsStdString() );
        if( auto* item = edge.GetTargetNode()->Parent() ) value->mutable_target_id()->set_value( item->m_Uuid.AsStdString() );
    };
    if( pad )
    {
        for( const auto& edge : connectivity->GetRatsnestForPad( pad ) ) append( edge, pad->GetNetname() );
    }
    else
        for( NETINFO_ITEM* net : board->GetNetInfo() )
            if( net->GetNetCode() > 0 )
                if( RN_NET* ratsnest = connectivity->GetRatsnestForNet( net->GetNetCode() ) )
                    for( const auto& edge : ratsnest->GetEdges() ) append( edge, net->GetNetname() );
    return reply;
}

namespace
{
class PCB_API_REPORTER : public REPORTER
{
public:
    explicit PCB_API_REPORTER( UpdatePcbFromSchematicResponse& response ) : m_response( response ) {}
    REPORTER& Report( const wxString& text, SEVERITY severity = RPT_SEVERITY_UNDEFINED ) override
    {
        REPORTER::Report( text, severity );
        auto* message = m_response.add_messages(); message->set_severity( severity );
        message->set_message( text.ToStdString() ); return *this;
    }
private:
    UpdatePcbFromSchematicResponse& m_response;
};

void configureUpdater( BOARD_NETLIST_UPDATER& updater, const PcbUpdateOptions& options )
{
    updater.SetLookupByTimestamp( !options.has_lookup_by_uuid() || options.lookup_by_uuid() );
    updater.SetReplaceFootprints( !options.has_replace_footprints() || options.replace_footprints() );
    updater.SetDeleteUnusedFootprints( options.delete_unused_footprints() );
    updater.SetUpdateFields( !options.has_update_fields() || options.update_fields() );
    updater.SetRemoveExtraFields( options.remove_extra_fields() );
    updater.SetOverrideLocks( options.override_locks() );
    updater.SetTransferGroups( options.transfer_groups() );
    updater.SetApplyDesignBlockLayouts( false ); // never opens placement/drag tools
}

void describeUpdate( NETLIST& netlist, BOARD& board, const PcbUpdateOptions& options,
                     UpdatePcbFromSchematicResponse& reply )
{
    std::set<std::string> nets;
    std::set<FOOTPRINT*> used;
    const bool byUuid = !options.has_lookup_by_uuid() || options.lookup_by_uuid();
    for( unsigned int i = 0; i < netlist.GetCount(); ++i )
    {
        COMPONENT* component = netlist.GetComponent( i );
        if( component->GetProperties().count( "exclude_from_board" ) ) continue;
        reply.set_component_count( reply.component_count() + 1 );
        for( unsigned int j = 0; j < component->GetNetCount(); ++j )
            if( !component->GetNet( j ).GetNetName().empty() ) nets.insert( component->GetNet( j ).GetNetName().ToStdString() );
        FOOTPRINT* found = nullptr;
        for( FOOTPRINT* footprint : board.Footprints() )
        {
            bool match = !byUuid && footprint->GetReference().CmpNoCase( component->GetReference() ) == 0;
            if( byUuid )
                for( const auto& id : component->GetKIIDs() )
                { KIID_PATH path = component->GetPath(); path.push_back( id ); if( path == footprint->GetPath() ) match = true; }
            if( match ) { found = footprint; used.insert( footprint ); break; }
        }
        FootprintChange change;
        change.set_reference( component->GetReference().ToStdString() );
        change.set_new_lib_id( component->GetFPID().Format().wx_str().ToStdString() );
        bool different = !found;
        if( found )
        {
            change.mutable_previous_id()->set_value( found->m_Uuid.AsStdString() );
            change.set_previous_lib_id( found->GetFPID().Format().wx_str().ToStdString() );
            different = found->GetReference() != component->GetReference() || found->GetValue() != component->GetValue()
                        || ( ( !options.has_replace_footprints() || options.replace_footprints() ) && found->GetFPID() != component->GetFPID() );
            for( const PAD* pad : found->Pads() )
            {
                const COMPONENT_NET& net = component->GetNet( pad->GetNumber() );
                const wxString wanted = net.IsValid() ? net.GetNetName() : wxString();
                if( pad->GetNetname() != wanted )
                {
                    auto* value = change.add_pad_nets(); value->set_number( pad->GetNumber().ToStdString() );
                    value->set_previous_net( pad->GetNetname().ToStdString() ); value->set_new_net( wanted.ToStdString() ); different = true;
                }
            }
        }
        else
            for( unsigned int j = 0; j < component->GetNetCount(); ++j )
            {
                const auto& net = component->GetNet( j );
                auto* value = change.add_pad_nets(); value->set_number( net.GetPinName().ToStdString() );
                value->set_new_net( net.GetNetName().ToStdString() );
            }
        if( different )
        { change.set_kind( found ? FCK_UPDATED : FCK_ADDED ); *reply.add_footprints() = change; }
    }
    if( options.delete_unused_footprints() )
        for( FOOTPRINT* footprint : board.Footprints() )
            if( !used.count( footprint ) && ( !footprint->IsLocked() || options.override_locks() ) )
            {
                auto* change = reply.add_footprints(); change->set_kind( FCK_REMOVED );
                change->set_reference( footprint->GetReference().ToStdString() );
                change->mutable_previous_id()->set_value( footprint->m_Uuid.AsStdString() );
                change->set_previous_lib_id( footprint->GetFPID().Format().wx_str().ToStdString() );
            }
    for( const auto& name : nets ) reply.add_net_names( name );
    reply.set_net_count( nets.size() );
}
}

HANDLER_RESULT<UpdatePcbFromSchematicResponse> API_HANDLER_PCB::handleUpdatePcbFromSchematic(
        const HANDLER_CONTEXT<UpdatePcbFromSchematic>& context )
{
    const auto& request = context.Request;
    if( auto valid = validateDocument( request.header().document() ); !valid ) return tl::unexpected( valid.error() );
    if( auto busy = checkForWrite( context.ClientName ) ) return tl::unexpected( *busy );
    if( request.header().document().project().path().empty() || request.header().has_container()
        || request.header().field_mask().paths_size() ) return documentError( "Update requires an explicit PCB project, without item container/mask" );
    UpdatePcbFromSchematicResponse reply;
    reply.set_dry_run( request.dry_run() );
    std::string payload;
    try
    {
        if( request.has_live_snapshot() )
        {
            const auto& source = request.live_snapshot();
            if( source.source() != kiapi::schematic::commands::PNS_LIVE || source.native_netlist().empty()
                || source.session_id().empty() || source.revision() == 0
                || source.source_document().type() != kiapi::common::types::DOCTYPE_SCHEMATIC
                || source.source_document().project().path().empty()
                || wxFileName::DirName( wxString::FromUTF8( source.source_document().project().path() ) )
                   != wxFileName::DirName( frame()->Prj().GetProjectDirectory() )
                || source.source_document().project().name() != frame()->Prj().GetProjectName().ToStdString() )
                return documentError( "A native live schematic snapshot from this explicit project is required" );
            *reply.mutable_source() = source; payload = source.native_netlist();
            reply.mutable_source()->clear_native_netlist();
        }
        else if( request.has_schematic_file() )
        {
            wxFileName path( wxString::FromUTF8( request.schematic_file() ) );
            if( !path.IsAbsolute() || !path.FileExists() || path.GetExt() != "kicad_sch" )
                return documentError( "Saved source must be an explicit existing absolute .kicad_sch file" );
            path.Normalize( wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE );
            const wxString directory = wxFileName::DirName( frame()->Prj().GetProjectDirectory() ).GetPathWithSep();
            if( !path.GetFullPath().StartsWith( directory ) ) return documentError( "Saved schematic must belong to the selected PCB project directory" );
            using ExportSaved = bool ( * )( const wxString&, PROJECT*, std::string&, std::string& );
            KIFACE* face = frame()->Kiway().KiFACE( KIWAY::FACE_SCH );
            auto exportSaved = face ? reinterpret_cast<ExportSaved>( face->IfaceOrAddress( KIFACE_EXPORT_PCB_NETLIST_SAVED ) ) : nullptr;
            std::string error;
            if( !exportSaved || !exportSaved( path.GetFullPath(), &frame()->Prj(), payload, error ) )
                return documentError( error.empty() ? "Native saved schematic export unavailable" : error );
            auto* source = reply.mutable_source(); source->set_source( kiapi::schematic::commands::PNS_SAVED );
            source->set_source_filename( path.GetFullPath().ToStdString() );
            source->mutable_source_document()->set_type( kiapi::common::types::DOCTYPE_SCHEMATIC );
            *source->mutable_source_document()->mutable_project() = request.header().document().project();
        }
        else return documentError( "Choose an explicit live snapshot or saved schematic file" );
        NETLIST netlist;
        auto* reader = new STRING_LINE_READER( payload, "IPC schematic snapshot" );
        KICAD_NETLIST_READER parser( reader, &netlist ); parser.LoadNetlist();
        std::set<wxString> references;
        auto* adapter = PROJECT_PCB::FootprintLibAdapter( &frame()->Prj() );
        for( unsigned int i = 0; i < netlist.GetCount(); ++i )
        {
            const COMPONENT* component = netlist.GetComponent( i );
            if( component->GetProperties().count( "exclude_from_board" ) ) continue;
            const wxString ref = component->GetReference().Upper();
            if( UTIL::GetRefDesNumber( ref ) < 0 || UTIL::GetRefDesPrefix( ref ).empty() || !references.insert( ref ).second )
                return documentError( "Schematic contains unannotated or duplicate component references" );
            const auto& id = component->GetFPID();
            if( !id.GetLibNickname().empty() ) adapter->LoadLibraryEntry( id.GetLibNickname() );
        }
        BOARD* board = frame()->GetBoard();
        // Native F8 modifies the board's variant catalogue outside BOARD_COMMIT.
        // Refuse rather than silently losing its atomic Undo/Drop guarantee.
        if( !netlist.GetVariantNames().empty() || !board->GetVariantNames().empty() )
            return documentError( "Atomic schematic update currently requires projects without assembly variants" );
        describeUpdate( netlist, *board, request.options(), reply );
        PCB_API_REPORTER reporter( reply );
        {
            BOARD_NETLIST_UPDATER preview( frame(), board );
            configureUpdater( preview, request.options() ); preview.SetReporter( &reporter ); preview.SetIsDryRun( true );
            const bool success = preview.UpdateNetlist( netlist );
            reply.set_error_count( preview.GetErrorCount() ); reply.set_warning_count( preview.GetWarningCount() );
            reply.set_can_apply( success && preview.GetErrorCount() == 0 );
        }
        if( request.dry_run() || !reply.can_apply() ) return reply;
        reply.clear_messages();
        auto* commit = static_cast<BOARD_COMMIT*>( getCurrentCommit( context.ClientName ) );
        // Native updater iterates a stable pre-update board. Apply its staged delta
        // afterwards, before replying, so subsequent IPC placement/routing sees it.
        deferCurrentCommit( context.ClientName, true );
        BOARD_NETLIST_UPDATER updater( frame(), board, commit );
        configureUpdater( updater, request.options() ); updater.SetReporter( &reporter );
        const bool success = updater.UpdateNetlist( netlist );
        for( auto* footprint : updater.GetAddedFootprints() )
            footprint->SetAttributes( footprint->GetAttributes() & ~FP_JUST_ADDED );
        deferCurrentCommit( context.ClientName, false );
        reply.set_error_count( updater.GetErrorCount() ); reply.set_warning_count( updater.GetWarningCount() );
        if( !success || updater.GetErrorCount() )
        {
            if( !m_activeClients.count( context.ClientName ) )
            { commit->Revert(); m_commits.erase( context.ClientName ); }
            return documentError( "PCB update failed; drop the API transaction before continuing" );
        }
        board->SynchronizeNetsAndNetClasses( true );
        // Footprints may have been staged before their new NETINFO entries.
        // Rebuild only after the entire native delta is attached, so queries
        // inside the caller's still-open commit see the final pad net codes.
        board->BuildConnectivity();
        board->GetConnectivity()->RefreshNetcodeMap( board );
        board->GetConnectivity()->RecalculateRatsnest();
        for( auto* footprint : updater.GetAddedFootprints() )
        {
            google::protobuf::Any packed; footprint->Serialize( packed );
            packed.UnpackTo( reply.add_added_footprints() );
        }
        if( !m_activeClients.count( context.ClientName ) ) pushCurrentCommit( context.ClientName, "Update PCB from schematic via API" );
        reply.set_applied( true ); frame()->Refresh();
        return reply;
    }
    catch( const IO_ERROR& exception )
    {
        if( m_commits.count( context.ClientName ) )
        {
            deferCurrentCommit( context.ClientName, false );
            if( !m_activeClients.count( context.ClientName ) )
            { m_commits.at( context.ClientName ).second->Revert(); m_commits.erase( context.ClientName ); }
        }
        return documentError( exception.What().ToStdString() );
    }
}
