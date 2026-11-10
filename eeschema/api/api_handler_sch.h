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

#ifndef KICAD_API_HANDLER_SCH_H
#define KICAD_API_HANDLER_SCH_H

#include <api/api_handler_editor.h>
#include <api/common/commands/editor_commands.pb.h>
#include <api/schematic/schematic_commands.pb.h>
#include <kiid.h>
#include <sch_sheet_path.h>

using namespace kiapi;
using namespace kiapi::common;

class SCH_EDIT_FRAME;
class SCH_ITEM;


class API_HANDLER_SCH : public API_HANDLER_EDITOR
{
public:
    API_HANDLER_SCH( SCH_EDIT_FRAME* aFrame );

protected:
    std::unique_ptr<COMMIT> createCommit() override;

    kiapi::common::types::DocumentType thisDocumentType() const override
    {
        return kiapi::common::types::DOCTYPE_SCHEMATIC;
    }

    bool validateDocumentInternal( const DocumentSpecifier& aDocument ) const override;

    HANDLER_RESULT<std::unique_ptr<EDA_ITEM>> createItemForType( KICAD_T aType,
                                                                 EDA_ITEM* aContainer );

    HANDLER_RESULT<types::ItemRequestStatus> handleCreateUpdateItemsInternal( bool aCreate,
            const std::string& aClientName,
            const types::ItemHeader &aHeader,
            const google::protobuf::RepeatedPtrField<google::protobuf::Any>& aItems,
            std::function<void(commands::ItemStatus, google::protobuf::Any)> aItemHandler )
            override;

    void deleteItemsInternal( std::map<KIID, ItemDeletionStatus>& aItemsToDelete,
                              const std::string& aClientName ) override;

    std::optional<EDA_ITEM*> getItemFromDocument( const DocumentSpecifier& aDocument,
                                                  const KIID& aId ) override;

private:
    HANDLER_RESULT<kiapi::schematic::types::ConnectivitySnapshot> connectivitySnapshot(
            const DocumentSpecifier& aDocument );
    void registerConnectivityHandlers();
    void registerNetRenameHandlers();
    std::map<std::string, kiapi::schematic::commands::NetRenamePlan> m_netRenamePlans;
    HANDLER_RESULT<kiapi::schematic::commands::PlanNetRenameResponse> handlePlanNetRename(
            const HANDLER_CONTEXT<kiapi::schematic::commands::PlanNetRename>& aCtx );
    HANDLER_RESULT<kiapi::schematic::commands::RenameNetResponse> handleRenameNet(
            const HANDLER_CONTEXT<kiapi::schematic::commands::RenameNet>& aCtx );
    std::string m_connectivitySession = KIID().AsStdString();
    std::string m_connectivityContent;
    uint64_t m_connectivityRevision = 0;
    HANDLER_RESULT<kiapi::schematic::commands::NetQueryResponse> handleGetSchematicNetlist(
            const HANDLER_CONTEXT<kiapi::schematic::commands::GetSchematicNetlist>& aCtx );
    HANDLER_RESULT<kiapi::schematic::commands::NetQueryResponse> handleGetPinNet(
            const HANDLER_CONTEXT<kiapi::schematic::commands::GetPinNet>& aCtx );
    HANDLER_RESULT<kiapi::schematic::commands::NetQueryResponse> handleGetNetMembers(
            const HANDLER_CONTEXT<kiapi::schematic::commands::GetNetMembers>& aCtx );
    HANDLER_RESULT<kiapi::schematic::commands::NetQueryResponse> handleGetItemNet(
            const HANDLER_CONTEXT<kiapi::schematic::commands::GetItemNet>& aCtx );
    HANDLER_RESULT<kiapi::schematic::commands::GetSymbolConnectionsResponse> handleGetSymbolConnections(
            const HANDLER_CONTEXT<kiapi::schematic::commands::GetSymbolConnections>& aCtx );
    HANDLER_RESULT<kiapi::schematic::commands::HighlightNetResponse> handleHighlightNet(
            const HANDLER_CONTEXT<kiapi::schematic::commands::HighlightNet>& aCtx );
    HANDLER_RESULT<kiapi::schematic::commands::GetConnectivityIssuesResponse> handleGetConnectivityIssues(
            const HANDLER_CONTEXT<kiapi::schematic::commands::GetConnectivityIssues>& aCtx );

    std::optional<SCH_SHEET_PATH> resolveDocument( const DocumentSpecifier& aDocument ) const;
    DocumentSpecifier describeDocument( const SCH_SHEET_PATH& aPath ) const;
    HANDLER_RESULT<kiapi::schematic::types::Sheet> createSheet(
            const types::ItemHeader& aHeader, const kiapi::schematic::types::Sheet& aSheet,
            const std::string& aClientName, std::optional<bool> aLinkExisting );
    HANDLER_RESULT<kiapi::schematic::commands::CreateSheetResponse> handleCreateSheet(
            const HANDLER_CONTEXT<kiapi::schematic::commands::CreateSheet>& aCtx );

    HANDLER_RESULT<commands::GetItemsResponse> handleGetItems(
            const HANDLER_CONTEXT<commands::GetItems>& aCtx );
    HANDLER_RESULT<commands::GetItemsResponse> handleGetItemsById(
            const HANDLER_CONTEXT<commands::GetItemsById>& aCtx );

    HANDLER_RESULT<commands::GetOpenDocumentsResponse> handleGetOpenDocuments(
            const HANDLER_CONTEXT<commands::GetOpenDocuments>& aCtx );
};


#endif //KICAD_API_HANDLER_SCH_H
