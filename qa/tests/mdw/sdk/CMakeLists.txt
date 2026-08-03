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

add_executable( qa_mdw_sdk
    test_module.cpp
    test_mdw_sdk_dispatch.cpp
    test_mdw_subprocess.cpp
    )

target_link_libraries( qa_mdw_sdk
    mdw_backend_sdk
    kinng
    kiapi
    ${NNG_LIBRARY}
    ${wxWidgets_LIBRARIES}
    Boost::headers
    Boost::unit_test_framework
    )

target_include_directories( qa_mdw_sdk PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    )

kicad_add_boost_test( qa_mdw_sdk qa_mdw_sdk )

setup_qa_env( qa_mdw_sdk )
