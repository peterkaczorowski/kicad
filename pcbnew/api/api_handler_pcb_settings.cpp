/* Copyright The KiCad Developers, see AUTHORS.txt. SPDX-License-Identifier: GPL-3.0-or-later */
#include <api/api_handler_pcb.h>
#include <api/api_pcb_settings_utils.h>
#include <board.h>
#include <board_design_settings.h>
#include <pcb_edit_frame.h>
#include <pcb_draw_panel_gal.h>
#include <pcb_painter.h>
#include <project.h>
#include <kiway.h>
#include <project/project_file.h>
#include <project/net_settings.h>
#include <settings/json_settings_internals.h>
#include <tool/tool_manager.h>
#include <tools/drc_tool.h>
#include <tools/pcb_selection_tool.h>
#include <pcb_marker.h>
#include <drc/drc_engine.h>
#include <drc/drc_rule_parser.h>
#include <drc/drc_rule.h>
#include <drc/drc_rule_condition.h>
#include <reporter.h>
#include <string_utils.h>
#include <view/view.h>
#include <google/protobuf/util/json_util.h>
#include <wx/ffile.h>
#include <wx/log.h>
#include <stdexcept>
#include <cmath>
#include <set>

using namespace kiapi::common;
using namespace kiapi::board::commands;
namespace
{
using JSON = nlohmann::json;
std::string readFile( const wxString& path, bool optional = false )
{
    if( optional && !wxFileExists( path ) ) return {};
    wxFFile input( path, "rb" );
    wxString text;
    if( !input.IsOpened() || !input.ReadAll( &text, wxConvUTF8 ) )
        throw std::runtime_error( "Cannot read settings file: " + path.ToStdString() );
    return text.ToStdString( wxConvUTF8 );
}
JSON fromProto( const google::protobuf::Message& message )
{
    std::string text;
    google::protobuf::util::JsonPrintOptions options;
    options.preserve_proto_field_names = true;
    options.always_print_fields_with_no_presence = true;
    auto status = google::protobuf::util::MessageToJsonString( message, &text, options );
    if( !status.ok() ) throw std::invalid_argument( status.ToString() );
    return JSON::parse( text );
}
void toProto( const JSON& json, google::protobuf::Message& message )
{
    auto status = google::protobuf::util::JsonStringToMessage( json.dump(), &message );
    if( !status.ok() ) throw std::invalid_argument( status.ToString() );
}
std::string rules( PCB_EDIT_FRAME* frame )
{
    const auto& file = frame->Prj().GetProjectFile();
    if( file.PendingBoardRules() ) return *file.PendingBoardRules();
    if( file.Internals()->m_loadedBoardRules ) return *file.Internals()->m_loadedBoardRules;
    return readFile( frame->GetDesignRulesPath(), true );
}
JSON snapshot( PCB_EDIT_FRAME* frame )
{
    return { { "net_settings", frame->Prj().GetProjectFile().NetSettings()->GetCurrentParams() },
             { "board_design_settings", frame->GetBoard()->GetDesignSettings().GetCurrentParams() },
             { "custom_rules", rules( frame ) } };
}
void loadParams( JSON_SETTINGS& settings, const JSON& json )
{
    static_cast<JSON&>( *settings.Internals() ) = json;
    settings.Load();
}
void validateRules( BOARD* board, const std::string& text )
{
    std::function<bool( wxString* )> resolver = [&]( wxString* token ) -> bool
    {
        return !IsComponentClassSelector( *token ) && board->ResolveTextVar( token, 0 );
    };
    wxString expanded = board->ConvertCrossReferencesToKIIDs( wxString::FromUTF8( text ) );
    expanded = ExpandTextVars( expanded, &resolver );
    std::vector<std::shared_ptr<DRC_RULE>> parsed;
    DRC_RULES_PARSER parser( expanded, "IPC project design rules" );
    parser.Parse( parsed, nullptr );
    REPORTER errors;
    for( const auto& rule : parsed )
        if( rule->m_Condition && !rule->m_Condition->GetExpression().empty() )
            rule->m_Condition->Compile( &errors );
    if( errors.HasMessageOfSeverity( RPT_SEVERITY_ERROR ) )
        throw std::invalid_argument( "Custom-rule condition could not be compiled" );
}

std::set<std::string> implicitClasses( const JSON& net )
{
    std::set<std::string> defined, referenced;
    for( const auto& nc : net.at( "classes" ) )
        defined.insert( nc.at( "name" ).get<std::string>() );
    for( const auto& pattern : net.at( "netclass_patterns" ) )
        referenced.insert( pattern.at( "netclass" ).get<std::string>() );
    for( const auto& assignment : net.at( "netclass_assignments" ).items() )
        for( const auto& name : assignment.value() )
            referenced.insert( name.get<std::string>() );
    for( const auto& name : defined ) referenced.erase( name );
    return referenced;
}

JSON validationNet( const JSON& candidate, const JSON& baseline )
{
    JSON result = candidate;
    if( !result.contains( "classes" ) || !result.at( "classes" ).is_array() ) return result;
    // Native schematic label assignments can legitimately refer to implicit classes. Preserve
    // those already present; newly introduced dangling references still refuse. These placeholders
    // exist only for reference validation and are never loaded into the live explicit catalogue.
    for( const auto& name : implicitClasses( baseline ) )
    {
        bool defined = false;
        for( const auto& nc : result.at( "classes" ) )
            if( nc.is_object() && nc.value( "name", std::string() ) == name ) defined = true;
        if( !defined ) result["classes"].push_back( { { "name", name } } );
    }
    return result;
}
// Validate legacy native readers in isolation, including clamping/ignored values, before changing
// any live pointer, dirty flag, marker or undo record. GUI and API use the same settings classes.
void validateCandidate( const JSON& candidate, const JSON& current )
{
    if( candidate.at( "net_settings" ).value( "meta", JSON::object() )
        != current.at( "net_settings" ).value( "meta", JSON::object() ) )
        throw std::invalid_argument( "The net-settings schema metadata is read-only" );
    for( const auto& field : current.at( "board_design_settings" ).items() )
        if( !candidate.at( "board_design_settings" ).contains( field.key() ) )
            throw std::invalid_argument( "Incomplete board settings; read first or use a leaf field mask" );
    for( const auto& section : { "rules", "rule_severities" } )
        for( const auto& field : current.at( "board_design_settings" ).at( section ).items() )
            if( !candidate.at( "board_design_settings" ).at( section ).contains( field.key() ) )
                throw std::invalid_argument( "Incomplete board constraint/severity map; use a leaf field mask" );
    PCB_PROJECT_SETTINGS::Validate( validationNet( candidate.at( "net_settings" ), current.at( "net_settings" ) ), candidate.at( "board_design_settings" ),
                                    current.at( "board_design_settings" ) );
    NET_SETTINGS net( nullptr, "net_settings" );
    BOARD_DESIGN_SETTINGS board( nullptr, "board.design_settings" );
    loadParams( net, candidate.at( "net_settings" ) );
    loadParams( board, candidate.at( "board_design_settings" ) );
    // Native distances round to 1nm. Detect rejected/clamped values, allowing only rounding.
    const auto normalized = board.GetCurrentParams();
    for( const auto& rule : candidate.at( "board_design_settings" ).at( "rules" ).items() )
    {
        const auto& result = normalized.at( "rules" ).at( rule.key() );
        if( rule.value().is_number() && std::abs( result.get<double>() - rule.value().get<double>() ) > 0.000001 )
            throw std::invalid_argument( "Board constraint is outside the native range: " + rule.key() );
    }
}
bool modified( PCB_EDIT_FRAME* frame )
{
    auto& file = frame->Prj().GetProjectFile();
    return file.NetSettings()->HasUnsavedChanges()
           || frame->GetBoard()->GetDesignSettings().HasUnsavedChanges() || file.PendingBoardRules().has_value();
}
void apply( PCB_EDIT_FRAME* frame, const JSON& candidate )
{
    auto& file = frame->Prj().GetProjectFile();
    const JSON before = snapshot( frame );
    const auto pending = file.PendingBoardRules();
    auto update = [&]( const JSON& value )
    {
        loadParams( *file.NetSettings(), value.at( "net_settings" ) );
        file.NetSettings()->ClearAllCaches();
        loadParams( frame->GetBoard()->GetDesignSettings(), value.at( "board_design_settings" ) );
        auto* render = static_cast<KIGFX::PCB_RENDER_SETTINGS*>(
                frame->GetCanvas()->GetView()->GetPainter()->GetSettings() );
        auto& colors = render->GetNetColorMap();
        colors.clear();
        for( const auto& [name, color] : file.NetSettings()->GetNetColorAssignments() )
            if( color != KIGFX::COLOR4D::UNSPECIFIED )
                if( auto* net = frame->GetBoard()->GetNetInfo().GetNetItem( name ) )
                    colors[net->GetNetCode()] = color;
        frame->GetBoard()->SynchronizeNetsAndNetClasses( true );
        frame->GetBoard()->GetDesignSettings().m_DRCEngine->InitEngine( frame->GetDesignRulesPath() );
    };
    try
    {
        file.SetPendingBoardRules( candidate.at( "custom_rules" ).get<std::string>() );
        update( candidate );
        // An unchanged rules file is not an outstanding write.
        if( candidate.at( "custom_rules" ) == readFile( frame->GetDesignRulesPath(), true ) )
            file.SetPendingBoardRules( std::nullopt );
    }
    catch( ... )
    {
        file.SetPendingBoardRules( before.at( "custom_rules" ).get<std::string>() );
        update( before );
        file.SetPendingBoardRules( pending );
        throw;
    }
}
}

void API_HANDLER_PCB::registerProjectSettingsHandlers()
{
    registerHandler<GetProjectSettings, ProjectSettingsResponse>( &API_HANDLER_PCB::handleGetProjectSettings );
    registerHandler<SetProjectSettings, ProjectSettingsResponse>( &API_HANDLER_PCB::handleSetProjectSettings );
    registerHandler<ReloadProjectSettings, ProjectSettingsResponse>( &API_HANDLER_PCB::handleReloadProjectSettings );
    registerHandler<GetNetsByNetClass, NetsByNetClassResponse>( &API_HANDLER_PCB::handleGetNetsByNetClass );
}

HANDLER_RESULT<bool> API_HANDLER_PCB::validateProjectSettingsContext( const DocumentSpecifier& document )
{
    if( auto valid = validateDocumentCommand( document ); !valid ) return valid;
    if( document.project().path().empty() || document.project().name().empty()
        || frame()->Prj().IsNullProject() )
        return documentError( "An explicit non-null PCB project is required" );
    if( frame()->GetToolManager()->GetTool<DRC_TOOL>()->IsDRCRunning() )
        return documentError( "DRC is running", ApiStatusCode::AS_BUSY );
    return true;
}

ProjectSettingsResponse API_HANDLER_PCB::projectSettingsResponse()
{
    const auto current = snapshot( frame() );
    const auto serialized = frame()->Prj().GetProjectFullName().ToStdString() + '\n' + current.dump();
    if( serialized != m_projectSettingsSnapshot )
    {
        m_projectSettingsSnapshot = serialized;
        m_projectSettingsRevision = KIID().AsStdString();
    }
    ProjectSettingsResponse response;
    toProto( current, *response.mutable_settings() );
    response.set_revision( m_projectSettingsRevision );
    response.set_modified( modified( frame() ) );
    return response;
}

HANDLER_RESULT<ProjectSettingsResponse> API_HANDLER_PCB::handleGetProjectSettings(
        const HANDLER_CONTEXT<GetProjectSettings>& context )
{
    if( auto valid = validateProjectSettingsContext( context.Request.document() ); !valid ) return tl::unexpected( valid.error() );
    wxLogNull noDialogs;
    try { return projectSettingsResponse(); }
    catch( const std::exception& error ) { return documentError( error.what() ); }
}

void API_HANDLER_PCB::refreshProjectSettings()
{
    auto* board = frame()->GetBoard();
    frame()->GetToolManager()->GetTool<DRC_TOOL>()->DestroyDRCDialog();
    for( auto* marker : board->Markers() )
        if( marker->GetSeverity() != RPT_SEVERITY_EXCLUSION )
        {
            frame()->GetToolManager()->GetTool<PCB_SELECTION_TOOL>()->RemoveItemFromSel( marker, true );
            frame()->GetCanvas()->GetView()->Remove( marker );
        }
    board->DeleteMARKERs( true, false ); // Keep explicit exclusions, invalidate stale DRC results.
    board->InitializeClearanceCache();
    frame()->Kiway().CommonSettingsChanged( TEXTVARS_CHANGED );
    frame()->Prj().IncrementTextVarsTicker();
    frame()->Prj().IncrementNetclassesTicker();
    // Unlike LoadProjectSettings(), this must not restore window/display/local-file state.
    frame()->GetCanvas()->GetView()->UpdateAllLayersColor();
    frame()->GetCanvas()->GetView()->UpdateAllItems( KIGFX::ALL );
    static_cast<EDA_DRAW_FRAME*>( frame() )->ReCreateAuxiliaryToolbar();
    frame()->UpdateUserInterface();
    frame()->GetToolManager()->ResetTools( TOOL_BASE::MODEL_RELOAD );
    TOOL_EVENT changed( TC_COMMAND, TA_MODEL_CHANGE, AS_ACTIVE );
    changed.SetHasPosition( false );
    frame()->GetToolManager()->ProcessEvent( changed );
    frame()->GetCanvas()->Refresh();
}

HANDLER_RESULT<ProjectSettingsResponse> API_HANDLER_PCB::handleSetProjectSettings(
        const HANDLER_CONTEXT<SetProjectSettings>& context )
{
    const auto& request = context.Request;
    if( auto valid = validateProjectSettingsContext( request.document() ); !valid ) return tl::unexpected( valid.error() );
    if( frame()->Prj().IsReadOnly() ) return documentError( "Project is read-only" );
    wxLogNull noDialogs;
    try
    {
        const auto previous = projectSettingsResponse();
        if( request.expected_revision().empty() || request.expected_revision() != previous.revision() )
            return documentError( "Stale project settings; read them again before editing" );
        const auto current = snapshot( frame() );
        const auto candidate = PCB_PROJECT_SETTINGS::Patch( current, fromProto( request.settings() ),
                { request.field_mask().paths().begin(), request.field_mask().paths().end() } );
        if( current == candidate ) return previous;
        validateCandidate( candidate, current );
        validateRules( frame()->GetBoard(), candidate.at( "custom_rules" ).get<std::string>() );
        apply( frame(), candidate );
        frame()->OnModify();
        refreshProjectSettings();
        return projectSettingsResponse();
    }
    catch( const IO_ERROR& error ) { return documentError( error.What().ToStdString() ); }
    catch( const std::exception& error ) { return documentError( error.what() ); }
}

HANDLER_RESULT<ProjectSettingsResponse> API_HANDLER_PCB::handleReloadProjectSettings(
        const HANDLER_CONTEXT<ReloadProjectSettings>& context )
{
    const auto& request = context.Request;
    if( auto valid = validateProjectSettingsContext( request.document() ); !valid ) return tl::unexpected( valid.error() );
    wxLogNull noDialogs;
    try
    {
        if( modified( frame() ) && !request.discard_unsaved() )
            return documentError( "Project settings have unsaved changes; set discard_unsaved=true to discard them" );
        const auto path = frame()->Prj().GetProjectFullName();
        const auto disk = JSON::parse( readFile( path ), nullptr, true, true );
        if( !disk.is_object() || !disk.contains( "net_settings" ) || !disk.contains( "board" )
            || !disk.at( "board" ).contains( "design_settings" ) )
            return documentError( "Project file is missing native net/board settings" );
        PROJECT_FILE detached( path );
        detached.SetReadOnly( true ); // Native migration must never write during a reload.
        BOARD_DESIGN_SETTINGS design( &detached, "board.design_settings" );
        if( !detached.LoadFromFile() ) return documentError( "Could not load project settings" );
        const JSON candidate = { { "net_settings", detached.NetSettings()->GetCurrentParams() },
                { "board_design_settings", design.GetCurrentParams() },
                { "custom_rules", readFile( frame()->GetDesignRulesPath(), true ) } };
        // Legacy loaders may silently ignore malformed keys or clamp values. Validate the raw
        // file as well, so a broken disk edit never turns into a partially successful reload.
        JSON rawBoard = candidate.at( "board_design_settings" );
        for( const auto& entry : disk.at( "board" ).at( "design_settings" ).items() )
        {
            if( !rawBoard.contains( entry.key() ) )
                return documentError( "Unknown external board design setting: " + entry.key() );
            // Other native sections may omit defaults or undergo migrations. Keep their native
            // normalized representation; strictly check the requested rule/size editing surface.
            if( entry.key() == "rules" || entry.key() == "rule_severities" || entry.key() == "track_widths"
                || entry.key() == "via_dimensions" || entry.key() == "diff_pair_dimensions" )
                rawBoard[entry.key()] = entry.value();
        }
        // The native reader accepts this KiCad 8 spelling (modern spelling wins).
        // Validate its value before normalizing it, rather than rejecting a valid older project
        // or silently accepting a malformed legacy severity ignored by the reader.
        auto& severities = rawBoard["rule_severities"];
        if( severities.is_object() && severities.contains( "hole_near_hole" ) )
        {
            const auto legacy = severities.at( "hole_near_hole" );
            if( !legacy.is_string() || ( legacy != "error" && legacy != "warning" && legacy != "ignore" ) )
                return documentError( "Invalid legacy hole_near_hole severity" );
            if( !severities.contains( "hole_to_hole" ) ) severities["hole_to_hole"] = legacy;
            severities.erase( "hole_near_hole" );
        }
        PCB_PROJECT_SETTINGS::Validate( validationNet( disk.at( "net_settings" ), candidate.at( "net_settings" ) ), rawBoard,
                                        candidate.at( "board_design_settings" ) );
        for( const auto& rule : disk.at( "board" ).at( "design_settings" ).at( "rules" ).items() )
            if( rule.value().is_number()
                && std::abs( candidate.at( "board_design_settings" ).at( "rules" ).at( rule.key() ).get<double>()
                             - rule.value().get<double>() ) > 0.000001 )
                return documentError( "External board constraint is outside the native range: " + rule.key() );
        validateCandidate( candidate, candidate );
        validateRules( frame()->GetBoard(), candidate.at( "custom_rules" ).get<std::string>() );
        apply( frame(), candidate );
        auto& file = frame()->Prj().GetProjectFile();
        file.AdoptFileScope( "net_settings", disk.at( "net_settings" ) );
        file.AdoptFileScope( "board.design_settings", disk.at( "board" ).at( "design_settings" ) );
        file.NetSettings()->MarkSaved();
        frame()->GetBoard()->GetDesignSettings().MarkSaved();
        file.Internals()->m_savedBoardDesignParams =
                frame()->GetBoard()->GetDesignSettings().GetCurrentParams().dump();
        refreshProjectSettings(); // Preserve board geometry, Undo stack and dirty state.
        return projectSettingsResponse();
    }
    catch( const IO_ERROR& error ) { return documentError( error.What().ToStdString() ); }
    catch( const std::exception& error ) { return documentError( error.what() ); }
}

HANDLER_RESULT<NetsByNetClassResponse> API_HANDLER_PCB::handleGetNetsByNetClass(
        const HANDLER_CONTEXT<GetNetsByNetClass>& context )
{
    const auto& request = context.Request;
    if( auto valid = validateProjectSettingsContext( request.document() ); !valid ) return tl::unexpected( valid.error() );
    const auto name = wxString::FromUTF8( request.netclass() );
    const auto& settings = frame()->Prj().GetProjectFile().NetSettings();
    if( name.empty() || ( !settings->HasNetclass( name )
                         && !implicitClasses( settings->GetCurrentParams() ).count( context.Request.netclass() ) ) )
        return documentError( "Unknown netclass" );
    NetsByNetClassResponse response;
    for( auto* net : frame()->GetBoard()->GetNetInfo() )
        if( net->GetNetCode() > 0 && net->GetNetClass()->ContainsNetclassWithName( name ) )
        {
            auto* value = response.add_nets();
            value->set_name( net->GetNetname().ToStdString() );
            value->mutable_code()->set_value( net->GetNetCode() );
        }
    return response;
}
