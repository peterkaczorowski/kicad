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

#include <algorithm>
#include <cctype>
#include "git_operations.h"

#include <cstdlib>


std::string GitRepo()
{
    const char* repo = std::getenv( "KICAD_MDW_GIT_REPO" );

    if( repo && repo[0] != '\0' )
        return std::string( repo );

    return std::string( "." );
}


MDW_PROCESS_RESULT Git( const std::vector<std::string>& aArgs )
{
    std::vector<std::string> argv = { "git", "-C", GitRepo() };
    argv.insert( argv.end(), aArgs.begin(), aArgs.end() );

    return MDW_RunProcess( argv );
}


std::vector<std::string> GitLsFiles( const std::string& aPrefix, bool* aOk )
{
    std::vector<std::string> args = { "-c", "core.quotePath=false", "ls-files", "-z" };

    if( !aPrefix.empty() )
    {
        args.push_back( "--" );
        args.push_back( aPrefix );
    }

    MDW_PROCESS_RESULT result = Git( args );
    bool               succeeded = result.started && result.exit_code == 0;

    if( aOk )
        *aOk = succeeded;

    if( !succeeded )
        return {};

    return Split( result.out, '\0' );
}


bool GitTracked( const std::string& aPath )
{
    if( aPath.empty() )
        return false;

    MDW_PROCESS_RESULT result = Git( { "ls-files", "--error-unmatch", "--", aPath } );

    return result.started && result.exit_code == 0;
}


bool IsSafeRef( const std::string& aRef )
{
    // A leading dash would be read as an option rather than a ref.
    if( aRef.empty() || aRef[0] == '-' )
        return false;

    return std::all_of( aRef.begin(), aRef.end(),
                        []( char c )
                        {
                            return std::isalnum( static_cast<unsigned char>( c ) ) || c == '.'
                                   || c == '_' || c == '/' || c == '-';
                        } );
}


bool IsSafePath( const std::string& aPath )
{
    if( aPath.empty() || aPath[0] == '-' || aPath[0] == '/' )
        return false;

    for( const std::string& component : Split( aPath, '/' ) )
    {
        if( component == ".." )
            return false;
    }

    return true;
}


std::string Trimmed( const std::string& aText )
{
    std::size_t begin = 0;
    std::size_t end = aText.size();

    while( begin < end && ( aText[begin] == ' ' || aText[begin] == '\t' || aText[begin] == '\n'
                            || aText[begin] == '\r' ) )
    {
        ++begin;
    }

    while( end > begin && ( aText[end - 1] == ' ' || aText[end - 1] == '\t' || aText[end - 1] == '\n'
                            || aText[end - 1] == '\r' ) )
    {
        --end;
    }

    return aText.substr( begin, end - begin );
}


std::vector<std::string> Split( const std::string& aText, char aDelim )
{
    std::vector<std::string> parts;
    std::string              token;

    for( char c : aText )
    {
        if( c == aDelim )
        {
            parts.push_back( token );
            token.clear();
        }
        else
        {
            token.push_back( c );
        }
    }

    if( !token.empty() )
        parts.push_back( token );

    return parts;
}
