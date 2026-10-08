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

find_package( wxWidgets 3.0.0 COMPONENTS base REQUIRED )

# In-process framework tests: enum-numbering agreement with the protobuf enums,
# the restart backoff policy, the backend proxy over a loopback transport, the
# manifest registry, and a live spawn/handshake against the mock backend.
add_executable( qa_mdw_framework
    test_module.cpp
    test_mdw_types.cpp
    test_mdw_restart_policy.cpp
    test_mdw_backend_proxy.cpp
    test_mdw_supervisor.cpp
    test_mdw_runtime.cpp
    test_mdw_schema_validator.cpp
    test_mdw_event_bridge.cpp
    test_mdw_event_ingress.cpp
    test_mdw_ipc_handler.cpp
    test_mdw_context_actions.cpp
    test_mdw_status_pill.cpp
    test_mdw_save_guard.cpp
    test_mdw_notification_bridge.cpp
    )

add_dependencies( qa_mdw_framework kiapi kicad-mdw-mock )

target_link_libraries( qa_mdw_framework
    kicommon
    kiapi
    kinng
    ${NNG_LIBRARY}
    ${wxWidgets_LIBRARIES}
    Boost::headers
    Boost::unit_test_framework
    )

target_include_directories( qa_mdw_framework PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}
    )

target_compile_definitions( qa_mdw_framework PRIVATE
    MDW_MOCK_EXECUTABLE="$<TARGET_FILE:kicad-mdw-mock>"
    )

kicad_add_boost_test( qa_mdw_framework qa_mdw_framework )

setup_qa_env( qa_mdw_framework )
