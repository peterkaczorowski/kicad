/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <qa_utils/wx_utils/unit_test_utils.h>
#include <io/cadstar/cadstar_archive_parser.h>
#include <ki_exception.h>

BOOST_AUTO_TEST_CASE( CadstarEmptyResolutionThrowsImportError )
{
    XNODE header( wxXML_ELEMENT_NODE, "HEADER" );
    header.AddChild( new XNODE( wxXML_ELEMENT_NODE, "RESOLUTION" ) );
    CADSTAR_ARCHIVE_PARSER::HEADER parsed;
    CADSTAR_ARCHIVE_PARSER::PARSER_CONTEXT context;
    BOOST_CHECK_THROW( parsed.Parse( &header, &context ), IO_ERROR );
}
