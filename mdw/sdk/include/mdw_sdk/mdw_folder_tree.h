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

/**
 * @file mdw_folder_tree.h
 * @brief Deriving a folder tree from flat slash-delimited paths.
 *
 * Backends rarely store a folder hierarchy; they store paths. Both reference
 * backends had to turn a flat list into one level of the tree, so the rule lives
 * here rather than in each of them.
 */

#ifndef MDW_FOLDER_TREE_H
#define MDW_FOLDER_TREE_H

#include <api/mdw/backend_base.pb.h>

#include <set>
#include <string>
#include <vector>

/// The directory part of a path, or empty when it has no separator.
std::string MdwParentDir( const std::string& aPath );

/// The last segment of a path.
std::string MdwBaseName( const std::string& aPath );

/**
 * Append the distinct immediate child folders of aParentFolderId to aReply.
 *
 * aFolderPaths are folder paths, not file paths; a backend holding files should
 * map them through MdwParentDir first. has_children is set on a child that has
 * any descendant in the same list, so a caller gets an expandable node only where
 * expanding it would show something.
 */
void MdwAddChildFolders( const std::set<std::string>& aFolderPaths,
                         const std::string&           aParentFolderId,
                         kiapi::mdw::GetChildFoldersReply& aReply );

#endif // MDW_FOLDER_TREE_H
