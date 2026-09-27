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

#include <mdw_sdk/mdw_folder_tree.h>

namespace ws = kiapi::workspace;


std::string MdwParentDir( const std::string& aPath )
{
    std::size_t slash = aPath.find_last_of( '/' );

    return slash == std::string::npos ? std::string() : aPath.substr( 0, slash );
}


std::string MdwBaseName( const std::string& aPath )
{
    std::size_t slash = aPath.find_last_of( '/' );

    return slash == std::string::npos ? aPath : aPath.substr( slash + 1 );
}


void MdwAddChildFolders( const std::set<std::string>& aFolderPaths,
                         const std::string&           aParentFolderId,
                         kiapi::mdw::GetChildFoldersReply& aReply )
{
    const std::string prefix =
            aParentFolderId.empty() ? std::string() : aParentFolderId + "/";

    std::set<std::string> emitted;

    for( const std::string& path : aFolderPaths )
    {
        if( path.compare( 0, prefix.size(), prefix ) != 0 )
            continue;

        std::string rest = path.substr( prefix.size() );

        if( rest.empty() )
            continue;

        std::size_t slash = rest.find( '/' );
        std::string child = prefix + ( slash == std::string::npos ? rest : rest.substr( 0, slash ) );

        if( child == aParentFolderId || !emitted.insert( child ).second )
            continue;

        // The set is ordered, so any descendant of child sorts immediately after
        // it under the "child/" prefix. That answers has_children in one lookup
        // rather than by rescanning every path per candidate.
        const std::string deeper = child + "/";
        auto              below = aFolderPaths.lower_bound( deeper );

        ws::Folder* folder = aReply.add_folders();
        folder->set_folder_id( child );
        folder->set_display_name( MdwBaseName( child ) );
        folder->set_parent_folder_id( aParentFolderId );
        folder->set_has_children( below != aFolderPaths.end()
                                  && below->compare( 0, deeper.size(), deeper ) == 0 );
    }
}
