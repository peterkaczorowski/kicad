/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
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

/*
 * The git backend end-to-end test. It stands up a real git working tree in a
 * temp directory, points the reference kicad-mdw-git backend at it through the
 * supervisor, and drives the protocol as a black box: the handshake advertises
 * REVISIONS, the tracked file lists and resolves, its history is non-empty, and
 * an unadvertised lock request is answered honestly with NOT_SUPPORTED.
 */

#include <boost/test/unit_test.hpp>

#include <mdw/mdw_backend_supervisor.h>
#include <mdw/mdw_backend.h>

#include <api/mdw/backend_base.pb.h>
#include <api/mdw/backend_revisions.pb.h>
#include <api/mdw/backend_locking.pb.h>
#include <api/workspace/workspace_types.pb.h>

#include <wx/utils.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace
{
namespace fs = std::filesystem;


/// Run a git command in aRepo, aborting the test if it fails.
void runGit( const std::string& aRepo, const std::string& aArgs )
{
    std::string command = "git -C \"" + aRepo + "\" " + aArgs;
    int         code = std::system( command.c_str() );

    BOOST_REQUIRE_EQUAL( code, 0 );
}


/// Create a temp git repo with one committed file and return its path.
fs::path makeRepo()
{
    fs::path dir = fs::temp_directory_path()
                   / ( "mdw_git_test_" + std::to_string( ::getpid() ) + "_"
                       + std::to_string( std::rand() ) );
    fs::create_directories( dir );

    std::ofstream board( dir / "board.kicad_pcb", std::ios::binary | std::ios::trunc );
    board << "(kicad_pcb (version 20240101))\n";
    board.close();

    const std::string repo = dir.string();
    runGit( repo, "init -q" );
    runGit( repo, "config user.email test@kicad.org" );
    runGit( repo, "config user.name \"MDW Test\"" );
    runGit( repo, "add board.kicad_pcb" );
    runGit( repo, "commit -q -m initial" );

    return dir;
}
}


BOOST_AUTO_TEST_SUITE( MdwGit )


BOOST_AUTO_TEST_CASE( GitBackendEndToEnd )
{
    fs::path repo = makeRepo();

    wxSetEnv( wxS( "KICAD_MDW_GIT_REPO" ), wxString::FromUTF8( repo.string() ) );

    MDW_BACKEND_MANIFEST manifest;
    manifest.backend_id = wxS( "org.kicad.mdw.git" );
    manifest.executable_path = wxString::FromUTF8( MDW_GIT_EXECUTABLE );
    manifest.valid = true;

    MDW_BACKEND_SUPERVISOR supervisor( manifest );
    BOOST_REQUIRE( supervisor.Launch( wxS( "10.0" ), 8000 ) );

    MDW_BACKEND* backend = supervisor.Backend();
    BOOST_REQUIRE( backend != nullptr );
    BOOST_CHECK( backend->Handshaked() );
    BOOST_CHECK( backend->Has( MDW_CAPABILITY_GROUP::REVISIONS ) );

    kiapi::mdw::ListItemsRequest listRequest;
    listRequest.set_folder_id( "" );
    kiapi::mdw::ListItemsReply listReply;
    BOOST_CHECK( backend->CallSync( listRequest, listReply, 5000 ) == MDW_ERROR_CODE::SUCCESS );

    bool found = false;

    for( const kiapi::workspace::ItemInfo& item : listReply.items() )
    {
        if( item.item_id() == "board.kicad_pcb" )
            found = true;
    }

    BOOST_CHECK( found );

    kiapi::mdw::GetItemInfoRequest infoRequest;
    infoRequest.set_item_id( "board.kicad_pcb" );
    kiapi::mdw::GetItemInfoReply infoReply;
    BOOST_CHECK( backend->CallSync( infoRequest, infoReply, 5000 ) == MDW_ERROR_CODE::SUCCESS );

    kiapi::mdw::GetRevisionHistoryRequest historyRequest;
    historyRequest.set_item_id( "board.kicad_pcb" );
    kiapi::mdw::GetRevisionHistoryReply historyReply;
    BOOST_CHECK( backend->CallSync( historyRequest, historyReply, 5000 )
                 == MDW_ERROR_CODE::SUCCESS );
    BOOST_CHECK( historyReply.revisions_size() >= 1 );

    // Locking is not advertised, so the SDK must answer NOT_SUPPORTED.
    kiapi::mdw::LockRequest lockRequest;
    lockRequest.set_item_id( "board.kicad_pcb" );
    kiapi::mdw::LockReply lockReply;
    BOOST_CHECK( backend->CallSync( lockRequest, lockReply, 5000 )
                 == MDW_ERROR_CODE::NOT_SUPPORTED );

    supervisor.Shutdown();
    BOOST_CHECK( !supervisor.Online() );

    std::error_code ec;
    fs::remove_all( repo, ec );
}


BOOST_AUTO_TEST_CASE( SecurityAndParsingRobustness )
{
    fs::path repo = makeRepo();
    const std::string repoStr = repo.string();

    // A file whose name contains a space, plus an empty-message commit, exercise
    // the NUL-delimited listing and the empty-trailing-field history parse.
    std::ofstream spaced( repo / "my board.kicad_sch", std::ios::binary | std::ios::trunc );
    spaced << "(kicad_sch)\n";
    spaced.close();
    runGit( repoStr, "add \"my board.kicad_sch\"" );
    runGit( repoStr, "commit -q -m added-spaced" );

    std::ofstream append( repo / "board.kicad_pcb", std::ios::binary | std::ios::app );
    append << "; edit\n";
    append.close();
    runGit( repoStr, "commit -q --allow-empty-message -m \"\" board.kicad_pcb" );

    wxSetEnv( wxS( "KICAD_MDW_GIT_REPO" ), wxString::FromUTF8( repoStr ) );

    MDW_BACKEND_MANIFEST manifest;
    manifest.backend_id = wxS( "org.kicad.mdw.git" );
    manifest.executable_path = wxString::FromUTF8( MDW_GIT_EXECUTABLE );
    manifest.valid = true;

    MDW_BACKEND_SUPERVISOR supervisor( manifest );
    BOOST_REQUIRE( supervisor.Launch( wxS( "10.0" ), 8000 ) );

    MDW_BACKEND* backend = supervisor.Backend();
    BOOST_REQUIRE( backend != nullptr );

    // The spaced filename round-trips through the NUL-delimited listing.
    kiapi::mdw::ListItemsRequest listRequest;
    listRequest.set_folder_id( "" );
    kiapi::mdw::ListItemsReply listReply;
    BOOST_CHECK( backend->CallSync( listRequest, listReply, 5000 ) == MDW_ERROR_CODE::SUCCESS );

    bool foundSpaced = false;

    for( const kiapi::workspace::ItemInfo& item : listReply.items() )
    {
        if( item.item_id() == "my board.kicad_sch" )
            foundSpaced = true;
    }

    BOOST_CHECK( foundSpaced );

    // The empty-message commit is still present in the history.
    kiapi::mdw::GetRevisionHistoryRequest historyRequest;
    historyRequest.set_item_id( "board.kicad_pcb" );
    kiapi::mdw::GetRevisionHistoryReply historyReply;
    BOOST_CHECK( backend->CallSync( historyRequest, historyReply, 5000 )
                 == MDW_ERROR_CODE::SUCCESS );
    BOOST_CHECK( historyReply.revisions_size() >= 2 );

    // An option-injecting revision id must be rejected, not written as a file.
    kiapi::mdw::DownloadRevisionRequest evilRequest;
    evilRequest.set_item_id( "board.kicad_pcb" );
    evilRequest.set_revision_id( "--output=/tmp/mdw-conformance-evil" );
    evilRequest.set_local_path( ( repo / "out" ).string() );
    kiapi::mdw::DownloadRevisionReply evilReply;
    BOOST_CHECK( backend->CallSync( evilRequest, evilReply, 5000 ) != MDW_ERROR_CODE::SUCCESS );
    BOOST_CHECK( !fs::exists( "/tmp/mdw-conformance-evil" ) );

    // A path-traversal item id must be rejected.
    kiapi::mdw::GetItemInfoRequest traversal;
    traversal.set_item_id( "../../../etc/passwd" );
    kiapi::mdw::GetItemInfoReply traversalReply;
    BOOST_CHECK( backend->CallSync( traversal, traversalReply, 5000 ) != MDW_ERROR_CODE::SUCCESS );

    supervisor.Shutdown();

    std::error_code ec;
    fs::remove_all( repo, ec );
}


BOOST_AUTO_TEST_SUITE_END()
