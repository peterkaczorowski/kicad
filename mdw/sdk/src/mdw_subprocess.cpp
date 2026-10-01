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


std::string MDW_WindowsCommandLine( const std::vector<std::string>& aArgv )
{
    std::string command;

    for( const std::string& arg : aArgv )
    {
        if( !command.empty() )
            command += ' ';

        if( !arg.empty() && arg.find_first_of( " \t\n\v\"" ) == std::string::npos )
        {
            command += arg;
            continue;
        }

        command += '"';

        for( std::size_t i = 0; ; ++i )
        {
            std::size_t backslashes = 0;

            while( i < arg.size() && arg[i] == '\\' )
            {
                ++i;
                ++backslashes;
            }

            if( i == arg.size() )
            {
                // Trailing backslashes would otherwise escape the closing quote.
                command.append( backslashes * 2, '\\' );
                break;
            }

            if( arg[i] == '"' )
                command.append( backslashes * 2 + 1, '\\' );
            else
                command.append( backslashes, '\\' );

            command += arg[i];
        }

        command += '"';
    }

    return command;
}


#if defined( _WIN32 )

#include <thread>

#include <windows.h>

namespace
{
/// Read a pipe to EOF. Run on its own thread so a child filling one pipe while
/// the parent reads the other cannot deadlock.
void drainPipe( HANDLE aPipe, std::string& aInto )
{
    char  buffer[4096];
    DWORD count = 0;

    while( ReadFile( aPipe, buffer, sizeof( buffer ), &count, nullptr ) && count > 0 )
        aInto.append( buffer, count );
}


struct HANDLE_PAIR
{
    HANDLE read = nullptr;
    HANDLE write = nullptr;

    ~HANDLE_PAIR()
    {
        if( read )
            CloseHandle( read );

        if( write )
            CloseHandle( write );
    }

    /// Create the pipe with both ends inheritable, then clear inheritance on the
    /// end the parent keeps. A child holding a copy of the parent's end would
    /// keep the pipe open and the drain would never see EOF.
    bool Open( bool aParentKeepsRead )
    {
        SECURITY_ATTRIBUTES attrs{};
        attrs.nLength = sizeof( attrs );
        attrs.bInheritHandle = TRUE;

        if( !CreatePipe( &read, &write, &attrs, 0 ) )
        {
            read = nullptr;
            write = nullptr;
            return false;
        }

        return SetHandleInformation( aParentKeepsRead ? read : write, HANDLE_FLAG_INHERIT, 0 );
    }
};
}


MDW_PROCESS_RESULT MDW_RunProcess( const std::vector<std::string>& aArgv,
                                   const std::string& aWorkingDir, const std::string& aStdin )
{
    MDW_PROCESS_RESULT result;

    if( aArgv.empty() )
        return result;

    HANDLE_PAIR in;
    HANDLE_PAIR out;
    HANDLE_PAIR err;

    if( !in.Open( false ) || !out.Open( true ) || !err.Open( true ) )
        return result;

    STARTUPINFOA startup{};
    startup.cb = sizeof( startup );
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = in.read;
    startup.hStdOutput = out.write;
    startup.hStdError = err.write;

    PROCESS_INFORMATION process{};

    // CreateProcess writes to the command line buffer, so it cannot be const.
    std::string commandLine = MDW_WindowsCommandLine( aArgv );
    std::vector<char> mutableCommandLine( commandLine.begin(), commandLine.end() );
    mutableCommandLine.push_back( '\0' );

    // A null application name is what makes the program name resolve against
    // PATH, matching execvp on the POSIX side.
    BOOL launched = CreateProcessA( nullptr, mutableCommandLine.data(), nullptr, nullptr, TRUE, 0,
                                    nullptr, aWorkingDir.empty() ? nullptr : aWorkingDir.c_str(),
                                    &startup, &process );

    if( !launched )
        return result;

    // The child owns its ends now. Closing the parent's copies is what lets the
    // reads below terminate once the child exits.
    CloseHandle( in.read );
    in.read = nullptr;
    CloseHandle( out.write );
    out.write = nullptr;
    CloseHandle( err.write );
    err.write = nullptr;

    std::thread outReader( [&]() { drainPipe( out.read, result.out ); } );
    std::thread errReader( [&]() { drainPipe( err.read, result.err ); } );

    if( !aStdin.empty() )
    {
        std::size_t written = 0;

        while( written < aStdin.size() )
        {
            DWORD count = 0;

            if( !WriteFile( in.write, aStdin.data() + written,
                            static_cast<DWORD>( aStdin.size() - written ), &count, nullptr )
                || count == 0 )
            {
                break;
            }

            written += count;
        }
    }

    CloseHandle( in.write );
    in.write = nullptr;

    outReader.join();
    errReader.join();

    WaitForSingleObject( process.hProcess, INFINITE );

    DWORD exitCode = 0;
    result.started = true;
    result.exit_code = GetExitCodeProcess( process.hProcess, &exitCode ) ? static_cast<int>( exitCode )
                                                                        : -1;

    CloseHandle( process.hThread );
    CloseHandle( process.hProcess );

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
