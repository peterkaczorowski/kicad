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
 * @file mdw_subprocess.h
 * @brief A wx-free subprocess runner for SDK backends that drive a CLI.
 *
 * The SDK links kiapi and nng only, so a backend that shells out (the git
 * backend runs the git CLI) cannot use wxExecute. This helper runs a command
 * with an explicit argument vector, feeds it optional standard input, and
 * captures its standard output, standard error and exit code as std::string.
 */

#ifndef MDW_SUBPROCESS_H
#define MDW_SUBPROCESS_H

#include <string>
#include <vector>

struct MDW_PROCESS_RESULT
{
    bool        started = false;   ///< False if the process could not be spawned
    int         exit_code = -1;    ///< Process exit status when started
    std::string out;               ///< Captured standard output
    std::string err;               ///< Captured standard error
};

/**
 * Run a command to completion and capture its output. aArgv[0] is the program;
 * it is resolved against PATH. aWorkingDir, when non-empty, is the child's
 * working directory. aStdin, when non-empty, is written to the child's stdin.
 */
MDW_PROCESS_RESULT MDW_RunProcess( const std::vector<std::string>& aArgv,
                                   const std::string&              aWorkingDir = std::string(),
                                   const std::string&              aStdin = std::string() );

/**
 * Join an argument vector into a single Windows command line.
 *
 * CreateProcess takes one string and the child splits it again, so an argument
 * containing a space, a tab or a quote has to be quoted such that the child's
 * runtime recovers it exactly. Backslashes are only special immediately before a
 * quote, where each one must be doubled, which is the part implementations
 * usually get wrong. Project paths routinely contain spaces, so this is on the
 * normal path rather than an edge case.
 *
 * Declared on every platform, and built on every platform, so the quoting can be
 * tested where the tests run rather than only where it is used.
 */
std::string MDW_WindowsCommandLine( const std::vector<std::string>& aArgv );

#endif // MDW_SUBPROCESS_H
