# This program source code file is part of KiCad, a free EDA CAD application.
#
# Copyright (C) 2026 KiCad Developers, see AUTHORS.txt for contributors.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

# The mock Odoo server uses raw POSIX sockets, so the target is POSIX-only.
if( WIN32 )
    return()
endif()

find_package( wxWidgets 3.0.0 COMPONENTS base REQUIRED )

# Drives the real kicad-mdw-odoo backend against an in-process mock HTTP JSON-RPC
# server through the supervisor and exercises the protocol as a black box.
add_executable( qa_mdw_odoo
    test_module.cpp
    test_mdw_odoo.cpp
    test_odoo_error_mapper.cpp
    mock_odoo_server.cpp
    ${CMAKE_SOURCE_DIR}/plugins/mdw/odoo/odoo_error_mapper.cpp
    )

add_dependencies( qa_mdw_odoo kiapi kicad-mdw-odoo )

target_link_libraries( qa_mdw_odoo
    kicommon
    kiapi
    kinng
    ${NNG_LIBRARY}
    ${wxWidgets_LIBRARIES}
    ${CURL_LIBRARIES}
    Boost::headers
    Boost::unit_test_framework
    )

target_include_directories( qa_mdw_odoo PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/thirdparty/nlohmann_json
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/plugins/mdw/odoo
    )

target_compile_definitions( qa_mdw_odoo PRIVATE
    MDW_ODOO_EXECUTABLE="$<TARGET_FILE:kicad-mdw-odoo>"
    )

kicad_add_boost_test( qa_mdw_odoo qa_mdw_odoo )

setup_qa_env( qa_mdw_odoo )
