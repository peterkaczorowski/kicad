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

#include <mdw_sdk/mdw_subprocess.h>

#if defined( _WIN32 )

MDW_PROCESS_RESULT MDW_RunProcess( const std::vector<std::string>&, const std::string&,
                                   const std::string& )
{
    // TODO Windows CreateProcess implementation with pipe redirection.
    MDW_PROCESS_RESULT result;
    return result;
}

#else

#include <cstring>
#include <vector>

#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
void readReady( int aFd, std::string& aInto, bool& aOpen )
{
    char    buffer[4096];
    ssize_t count = ::read( aFd, buffer, sizeof( buffer ) );

    if( count > 0 )
        aInto.append( buffer, static_cast<std::size_t>( count ) );
    else
        aOpen = false;
}
}


MDW_PROCESS_RESULT MDW_RunProcess( const std::vector<std::string>& aArgv,
                                   const std::string& aWorkingDir, const std::string& aStdin )
{
    MDW_PROCESS_RESULT result;

    if( aArgv.empty() )
        return result;

    int inPipe[2];
    int outPipe[2];
    int errPipe[2];

    if( pipe( inPipe ) != 0 )
        return result;

    if( pipe( outPipe ) != 0 )
    {
        ::close( inPipe[0] );
        ::close( inPipe[1] );
        return result;
    }

    if( pipe( errPipe ) != 0 )
    {
        ::close( inPipe[0] );
        ::close( inPipe[1] );
        ::close( outPipe[0] );
        ::close( outPipe[1] );
        return result;
    }

    pid_t pid = fork();

    if( pid < 0 )
    {
        ::close( inPipe[0] );
        ::close( inPipe[1] );
        ::close( outPipe[0] );
        ::close( outPipe[1] );
        ::close( errPipe[0] );
        ::close( errPipe[1] );
        return result;
    }

    if( pid == 0 )
    {
        dup2( inPipe[0], STDIN_FILENO );
        dup2( outPipe[1], STDOUT_FILENO );
        dup2( errPipe[1], STDERR_FILENO );

        ::close( inPipe[0] );
        ::close( inPipe[1] );
        ::close( outPipe[0] );
        ::close( outPipe[1] );
        ::close( errPipe[0] );
        ::close( errPipe[1] );

        if( !aWorkingDir.empty() && chdir( aWorkingDir.c_str() ) != 0 )
            _exit( 127 );

        std::vector<char*> argv;

        for( const std::string& arg : aArgv )
            argv.push_back( const_cast<char*>( arg.c_str() ) );

        argv.push_back( nullptr );

        execvp( argv[0], argv.data() );
        _exit( 127 );
    }

    ::close( inPipe[0] );
    ::close( outPipe[1] );
    ::close( errPipe[1] );

    // Stdin for these backends is small (a commit message at most), so feed it
    // fully before draining stdout and stderr.
    if( !aStdin.empty() )
    {
        std::size_t written = 0;

        while( written < aStdin.size() )
        {
            ssize_t count = ::write( inPipe[1], aStdin.data() + written, aStdin.size() - written );

            if( count <= 0 )
                break;

            written += static_cast<std::size_t>( count );
        }
    }

    ::close( inPipe[1] );

    bool outOpen = true;
    bool errOpen = true;

    while( outOpen || errOpen )
    {
        struct pollfd fds[2];
        fds[0] = { outPipe[0], POLLIN, 0 };
        fds[1] = { errPipe[0], POLLIN, 0 };

        if( poll( fds, 2, -1 ) < 0 )
            break;

        if( outOpen && ( fds[0].revents & ( POLLIN | POLLHUP ) ) )
            readReady( outPipe[0], result.out, outOpen );

        if( errOpen && ( fds[1].revents & ( POLLIN | POLLHUP ) ) )
            readReady( errPipe[0], result.err, errOpen );
    }

    ::close( outPipe[0] );
    ::close( errPipe[0] );

    int status = 0;
    waitpid( pid, &status, 0 );

    result.started = true;
    result.exit_code = WIFEXITED( status ) ? WEXITSTATUS( status ) : -1;

    return result;
}

#endif
