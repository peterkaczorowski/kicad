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
