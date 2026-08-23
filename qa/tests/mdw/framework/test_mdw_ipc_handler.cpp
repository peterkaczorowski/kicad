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

/*
 * The workspace IPC handler wires the workspace.* command surface to the
 * shared MDW_WORKSPACE_SERVICE dispatch path. Actually invoking a command
 * needs a live Pgm() and a spawned backend, which this in-process module does
 * not stand up. What is verifiable here is the wiring itself: the handler
 * constructs, registers exactly the workspace command set, and rejects an
 * unrelated message as unhandled so the API server falls through to the next
 * handler.
 */

#include <boost/test/unit_test.hpp>

#include <set>
#include <string>
#include <vector>

#include <api/api_handler_workspace.h>

#include <google/protobuf/empty.pb.h>
#include <api/common/envelope.pb.h>


BOOST_AUTO_TEST_SUITE( MdwIpcHandler )


BOOST_AUTO_TEST_CASE( RegistersEveryWorkspaceCommand )
{
    API_HANDLER_WORKSPACE handler;

    std::vector<std::string>   handled = handler.HandledTypes();
    std::set<std::string>      registered( handled.begin(), handled.end() );

    const std::vector<std::string> expected = {
        kiapi::workspace::GetWorkspaceInfo().GetTypeName(),
        kiapi::workspace::BindWorkspace().GetTypeName(),
        kiapi::workspace::UnbindWorkspace().GetTypeName(),
        kiapi::workspace::ListWorkspaceItems().GetTypeName(),
        kiapi::workspace::GetWorkspaceItemInfo().GetTypeName(),
        kiapi::workspace::CheckoutItem().GetTypeName(),
        kiapi::workspace::CheckinItem().GetTypeName(),
        kiapi::workspace::CancelCheckout().GetTypeName(),
        kiapi::workspace::LockItem().GetTypeName(),
        kiapi::workspace::UnlockItem().GetTypeName(),
        kiapi::workspace::GetItemLockInfo().GetTypeName(),
        kiapi::workspace::GetItemAttributes().GetTypeName(),
        kiapi::workspace::SetItemAttributes().GetTypeName(),
        kiapi::workspace::GetRevisionHistory().GetTypeName(),
        kiapi::workspace::DownloadRevision().GetTypeName(),
        kiapi::workspace::GetLifecycleCategory().GetTypeName(),
        kiapi::workspace::TransitionLifecycle().GetTypeName()
    };

    BOOST_CHECK_EQUAL( registered.size(), expected.size() );

    for( const std::string& type : expected )
        BOOST_CHECK_MESSAGE( registered.count( type ) == 1, "missing handler for " + type );
}


BOOST_AUTO_TEST_CASE( UnrelatedMessageIsUnhandled )
{
    API_HANDLER_WORKSPACE handler;

    kiapi::common::ApiRequest request;
    request.mutable_message()->PackFrom( google::protobuf::Empty() );

    API_RESULT result = handler.Handle( request );

    BOOST_REQUIRE( !result.has_value() );
    BOOST_CHECK_EQUAL( static_cast<int>( result.error().status() ),
                       static_cast<int>( kiapi::common::ApiStatusCode::AS_UNHANDLED ) );
}


BOOST_AUTO_TEST_SUITE_END()
