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
 * Both reference backends derive their folder tree from flat paths through this
 * helper. has_children in particular went unset in one of them for as long as the
 * two had separate copies, which is what these cover.
 */

#include <boost/test/unit_test.hpp>

#include <mdw_sdk/mdw_folder_tree.h>

namespace ws = kiapi::workspace;


namespace
{
std::vector<std::string> childIds( const kiapi::mdw::GetChildFoldersReply& aReply )
{
    std::vector<std::string> ids;

    for( const ws::Folder& folder : aReply.folders() )
        ids.push_back( folder.folder_id() );

    return ids;
}
}


BOOST_AUTO_TEST_SUITE( MdwFolderTree )


BOOST_AUTO_TEST_CASE( EmitsOneLevelOnly )
{
    const std::set<std::string> folders = { "hw", "hw/board", "hw/board/rev2", "hw/sch", "docs" };

    kiapi::mdw::GetChildFoldersReply root;
    MdwAddChildFolders( folders, "", root );
    BOOST_CHECK( childIds( root ) == std::vector<std::string>( { "docs", "hw" } ) );

    kiapi::mdw::GetChildFoldersReply under;
    MdwAddChildFolders( folders, "hw", under );
    BOOST_CHECK( childIds( under ) == std::vector<std::string>( { "hw/board", "hw/sch" } ) );
}


BOOST_AUTO_TEST_CASE( SetsHasChildrenOnlyWhereExpandable )
{
    const std::set<std::string> folders = { "hw", "hw/board", "hw/board/rev2", "hw/sch" };

    kiapi::mdw::GetChildFoldersReply reply;
    MdwAddChildFolders( folders, "hw", reply );

    BOOST_REQUIRE_EQUAL( reply.folders_size(), 2 );
    BOOST_CHECK_EQUAL( reply.folders( 0 ).folder_id(), "hw/board" );
    BOOST_CHECK( reply.folders( 0 ).has_children() );
    BOOST_CHECK_EQUAL( reply.folders( 1 ).folder_id(), "hw/sch" );
    BOOST_CHECK( !reply.folders( 1 ).has_children() );
}


BOOST_AUTO_TEST_CASE( DoesNotEmitTheParentOrSiblings )
{
    // "hwx" shares a textual prefix with "hw" but is not below it.
    const std::set<std::string> folders = { "hw", "hw/board", "hwx", "hwx/board" };

    kiapi::mdw::GetChildFoldersReply reply;
    MdwAddChildFolders( folders, "hw", reply );

    BOOST_CHECK( childIds( reply ) == std::vector<std::string>( { "hw/board" } ) );
}


BOOST_AUTO_TEST_CASE( NamesAndParentsAreSet )
{
    const std::set<std::string> folders = { "hw/board" };

    kiapi::mdw::GetChildFoldersReply reply;
    MdwAddChildFolders( folders, "hw", reply );

    BOOST_REQUIRE_EQUAL( reply.folders_size(), 1 );
    BOOST_CHECK_EQUAL( reply.folders( 0 ).display_name(), "board" );
    BOOST_CHECK_EQUAL( reply.folders( 0 ).parent_folder_id(), "hw" );
}


BOOST_AUTO_TEST_SUITE_END()
