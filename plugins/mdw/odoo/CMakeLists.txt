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

# A reference Odoo-backed MDW backend built on the SDK. Like the SDK it links
# kiapi and nng only, plus curl for the JSON-RPC transport and nlohmann_json for
# the message bodies. No wxWidgets, no kicommon.

add_executable( kicad-mdw-odoo
    kicad_mdw_odoo.cpp
    odoo_rpc_client.cpp
    odoo_error_mapper.cpp
    )

set_target_properties( kicad-mdw-odoo PROPERTIES
    OUTPUT_NAME kicad-mdw-odoo
    )

target_link_libraries( kicad-mdw-odoo
    mdw_backend_sdk
    ${NNG_LIBRARY}
    ${CURL_LIBRARIES}
    )

target_include_directories( kicad-mdw-odoo PRIVATE
    ${NNG_INCLUDE_DIR}
    ${CURL_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/thirdparty/nlohmann_json
    )
