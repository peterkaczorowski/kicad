/* Copyright The KiCad Developers, see AUTHORS.txt. SPDX-License-Identifier: GPL-3.0-or-later */
#include <api/api_handler_sch.h>
#include <sch_edit_frame.h>
#include <sch_screen.h>
#include <schematic.h>
#include <netlist_exporters/netlist_exporter_kicad.h>
#include <richio.h>

using namespace kiapi::schematic::commands;

void API_HANDLER_SCH::registerPcbExportHandler()
{
    registerHandler<ExportSchematicForPcb, PcbNetlistSnapshot>( &API_HANDLER_SCH::handleExportSchematicForPcb );
}

HANDLER_RESULT<PcbNetlistSnapshot> API_HANDLER_SCH::handleExportSchematicForPcb(
        const HANDLER_CONTEXT<ExportSchematicForPcb>& context )
{
    const auto& document = context.Request.document();
    if( auto valid = validateDocument( document ); !valid ) return tl::unexpected( valid.error() );
    if( auto busy = checkForBusy() ) return tl::unexpected( *busy );
    if( HasPendingCommit() ) return documentError( "Finish schematic commits before exporting a PCB snapshot" );
    if( document.project().path().empty() || !document.has_sheet_path() )
        return documentError( "Export requires an explicit project and sheet UUID path" );
    auto graph = connectivitySnapshot( document );
    if( !graph ) return tl::unexpected( graph.error() );
    auto* frame = static_cast<SCH_EDIT_FRAME*>( m_frame );
    try
    {
        NETLIST_EXPORTER_KICAD exporter( &frame->Schematic() );
        STRING_FORMATTER formatter;
        exporter.Format( &formatter, GNL_ALL | GNL_OPT_KICAD );
        PcbNetlistSnapshot reply;
        *reply.mutable_source_document() = document;
        reply.set_source( PNS_LIVE ); reply.set_native_netlist( formatter.GetString() );
        reply.set_source_filename( frame->Schematic().RootScreen()->GetFileName().ToStdString() );
        reply.set_session_id( graph->session_id() ); reply.set_revision( graph->revision() );
        reply.set_source_modified( documentModified() );
        return reply;
    }
    catch( const IO_ERROR& error ) { return documentError( error.What().ToStdString() ); }
}
