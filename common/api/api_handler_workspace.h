/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef KICAD_API_HANDLER_WORKSPACE_H
#define KICAD_API_HANDLER_WORKSPACE_H

#include <string>
#include <vector>

#include <api/api_handler.h>
#include <api/workspace/workspace_commands.pb.h>

using namespace kiapi::workspace;

class MDW_WORKSPACE_SERVICE;


/**
 * The workspace.* IPC command surface. Every command dispatches through the
 * same MDW_WORKSPACE_SERVICE path the standard dialogs use, so capability
 * gating, tracker updates and event emission stay in one place.
 *
 * The API server marshals every request onto the main thread and each service
 * call blocks on a backend round-trip up to the operation timeout, so a slow or
 * wedged backend stalls the UI for the duration of the call. This matches the
 * synchronous contract of the rest of the API_HANDLER surface. Moving the
 * backend round-trip off the main thread and completing the reply
 * asynchronously is a framework-wide change tracked separately from this
 * namespace.
 */
class KICOMMON_API API_HANDLER_WORKSPACE : public API_HANDLER
{
public:
    API_HANDLER_WORKSPACE();

    ~API_HANDLER_WORKSPACE() override {}

    /// The registered command type names, for introspection and testing.
    std::vector<std::string> HandledTypes() const;

private:
    HANDLER_RESULT<GetWorkspaceInfoResponse> handleGetWorkspaceInfo(
            const HANDLER_CONTEXT<GetWorkspaceInfo>& aCtx );

    HANDLER_RESULT<BindWorkspaceResponse> handleBindWorkspace(
            const HANDLER_CONTEXT<BindWorkspace>& aCtx );

    HANDLER_RESULT<UnbindWorkspaceResponse> handleUnbindWorkspace(
            const HANDLER_CONTEXT<UnbindWorkspace>& aCtx );

    HANDLER_RESULT<ListWorkspaceItemsResponse> handleListWorkspaceItems(
            const HANDLER_CONTEXT<ListWorkspaceItems>& aCtx );

    HANDLER_RESULT<GetWorkspaceItemInfoResponse> handleGetWorkspaceItemInfo(
            const HANDLER_CONTEXT<GetWorkspaceItemInfo>& aCtx );

    HANDLER_RESULT<CheckoutItemResponse> handleCheckoutItem(
            const HANDLER_CONTEXT<CheckoutItem>& aCtx );

    HANDLER_RESULT<CheckinItemResponse> handleCheckinItem(
            const HANDLER_CONTEXT<CheckinItem>& aCtx );

    HANDLER_RESULT<CancelCheckoutResponse> handleCancelCheckout(
            const HANDLER_CONTEXT<CancelCheckout>& aCtx );

    HANDLER_RESULT<LockItemResponse> handleLockItem( const HANDLER_CONTEXT<LockItem>& aCtx );

    HANDLER_RESULT<LockItemResponse> handleUnlockItem( const HANDLER_CONTEXT<UnlockItem>& aCtx );

    HANDLER_RESULT<GetItemLockInfoResponse> handleGetItemLockInfo(
            const HANDLER_CONTEXT<GetItemLockInfo>& aCtx );

    HANDLER_RESULT<GetItemAttributesResponse> handleGetItemAttributes(
            const HANDLER_CONTEXT<GetItemAttributes>& aCtx );

    HANDLER_RESULT<SetItemAttributesResponse> handleSetItemAttributes(
            const HANDLER_CONTEXT<SetItemAttributes>& aCtx );

    HANDLER_RESULT<GetRevisionHistoryResponse> handleGetRevisionHistory(
            const HANDLER_CONTEXT<GetRevisionHistory>& aCtx );

    HANDLER_RESULT<DownloadRevisionResponse> handleDownloadRevision(
            const HANDLER_CONTEXT<DownloadRevision>& aCtx );

    HANDLER_RESULT<GetLifecycleCategoryResponse> handleGetLifecycleCategory(
            const HANDLER_CONTEXT<GetLifecycleCategory>& aCtx );

    HANDLER_RESULT<TransitionLifecycleResponse> handleTransitionLifecycle(
            const HANDLER_CONTEXT<TransitionLifecycle>& aCtx );

    HANDLER_RESULT<SubscribeWorkspaceEventsResponse> handleSubscribeWorkspaceEvents(
            const HANDLER_CONTEXT<SubscribeWorkspaceEvents>& aCtx );

    HANDLER_RESULT<RegisterContextActionResponse> handleRegisterContextAction(
            const HANDLER_CONTEXT<RegisterContextAction>& aCtx );

    HANDLER_RESULT<UnregisterContextActionResponse> handleUnregisterContextAction(
            const HANDLER_CONTEXT<UnregisterContextAction>& aCtx );

    /// The bound session's service, or nullptr with aError set to a not-bound
    /// or backend-lost code when there is no usable session.
    MDW_WORKSPACE_SERVICE* sessionService( WorkspaceError* aError );
};

#endif // KICAD_API_HANDLER_WORKSPACE_H
