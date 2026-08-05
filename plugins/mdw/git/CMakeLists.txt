# This program source code file is part of KiCad, a free EDA CAD application.
#
# Copyright (C) 2026 KiCad Developers, see AUTHORS.txt for contributors.
#
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation, either version 3 of the License, or (at your
# option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

# A reference git-backed MDW backend built on the SDK. Like the SDK, it links
# kiapi and nng only, no wxWidgets, and drives the git CLI through MDW_RunProcess.

add_executable( kicad-mdw-git
    kicad_mdw_git.cpp
    git_operations.cpp
    )

set_target_properties( kicad-mdw-git PROPERTIES
    OUTPUT_NAME kicad-mdw-git
    )

target_link_libraries( kicad-mdw-git
    mdw_backend_sdk
    ${NNG_LIBRARY}
    )

target_include_directories( kicad-mdw-git PRIVATE
    ${NNG_INCLUDE_DIR}
    )
