/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2026 KiCad Developers, see AUTHORS.txt for contributors.
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
 * @file git_operations.h
 * @brief Thin wrappers that drive the git CLI through MDW_RunProcess.
 *
 * The git backend links kiapi and nng only, so it shells out to git rather than
 * using a git library. These helpers resolve the working tree, prefix the
 * common "git -C <repo>" arguments, and shape the raw output into the small
 * pieces the handlers consume.
 */

#ifndef GIT_OPERATIONS_H
#define GIT_OPERATIONS_H

#include <mdw_sdk/mdw_strings.h>
#include <mdw_sdk/mdw_subprocess.h>

#include <optional>
#include <string>
#include <vector>

/// The git working tree, from KICAD_MDW_GIT_REPO or the current directory.
std::string GitRepo();

/// Run git in the repo, prefixing {"git", "-C", repo} onto aArgs.
MDW_PROCESS_RESULT Git( const std::vector<std::string>& aArgs );

/// Every tracked file in the working tree, one path each. Empty when the repo
/// has no tracked files, and nullopt when git itself failed, so a caller can
/// tell the two apart.
std::optional<std::vector<std::string>> GitLsFiles();

/// True when git tracks aPath in the working tree.
bool GitTracked( const std::string& aPath );

/// True when aRef is safe to pass to git as a revision with no "--" guard.
/// Rejects empty, a leading '-', and any character outside [0-9A-Za-z._/-].
bool IsSafeRef( const std::string& aRef );

/// True when aPath is safe to pass to git as a repo-relative path. Rejects
/// empty, a leading '-', an absolute path, and any ".." path component.
bool IsSafePath( const std::string& aPath );

#endif // GIT_OPERATIONS_H
