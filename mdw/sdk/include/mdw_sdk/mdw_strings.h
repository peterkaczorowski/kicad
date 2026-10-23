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
 * @file mdw_strings.h
 * @brief Trimming and splitting for backends that parse CLI or file output.
 *
 * The SDK links kiapi and nng only, so a backend cannot reach for wxString or
 * the kicommon string helpers. Every reference backend needed the same two
 * operations, so they live here rather than being rewritten in each of them.
 * Header-only, so the SDK stays a two-file dependency for a third party.
 */

#ifndef MDW_STRINGS_H
#define MDW_STRINGS_H

#include <cstddef>
#include <string>
#include <vector>

/// Trim leading and trailing spaces, tabs, carriage returns and newlines.
inline std::string MdwTrimmed( const std::string& aText )
{
    static const char* const WHITESPACE = " \t\r\n";

    std::string::size_type begin = aText.find_first_not_of( WHITESPACE );

    if( begin == std::string::npos )
        return std::string();

    return aText.substr( begin, aText.find_last_not_of( WHITESPACE ) - begin + 1 );
}


/**
 * Split aText into fields on aDelim.
 *
 * With aMaxFields zero every delimiter splits and a trailing empty field is
 * dropped, which is what a NUL-terminated list of records from a CLI wants.
 * With aMaxFields set the split stops there and the unsplit remainder becomes
 * the last field, so a record whose final field is legitimately empty keeps its
 * field count. Fewer separators than requested yields fewer fields.
 */
inline std::vector<std::string> MdwSplit( const std::string& aText, char aDelim,
                                          std::size_t aMaxFields = 0 )
{
    std::vector<std::string> fields;
    std::string::size_type   start = 0;

    while( aMaxFields == 0 || fields.size() + 1 < aMaxFields )
    {
        std::string::size_type pos = aText.find( aDelim, start );

        if( pos == std::string::npos )
            break;

        fields.push_back( aText.substr( start, pos - start ) );
        start = pos + 1;
    }

    std::string tail = aText.substr( start );

    if( aMaxFields != 0 || !tail.empty() )
        fields.push_back( tail );

    return fields;
}

#endif // MDW_STRINGS_H
