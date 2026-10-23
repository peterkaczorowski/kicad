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

/*
 * kicad-mdw-git
 *
 * A minimal reference MDW backend that maps a git working tree onto the
 * workspace model. Folders are directories, items are tracked files, and the
 * revision history is the git log. It advertises CG_REVISIONS and nothing else,
 * so the SDK auto-answers WE_NOT_SUPPORTED for every group it does not serve.
 * Like the SDK, it links kiapi and nng only; no wxWidgets and no kicommon.
 */

#include "git_operations.h"

#include <mdw_sdk/mdw_backend_server.h>
#include <mdw_sdk/mdw_folder_tree.h>
#include <mdw_sdk/mdw_run_backend.h>

#include <api/mdw/backend_base.pb.h>
#include <api/mdw/backend_revisions.pb.h>
#include <api/workspace/workspace_types.pb.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mdw = kiapi::mdw;
namespace ws = kiapi::workspace;

namespace
{

const char FIELD_SEP = '\x1f';

/// Fields in the revision log format, hash through subject.
const std::size_t LOG_FIELDS = 5;


/// The value of a git config key, or aFallback when it is unset.
std::string configValue( const std::string& aKey, const std::string& aFallback )
{
    MDW_PROCESS_RESULT result = Git( { "config", aKey } );

    if( !result.started || result.exit_code != 0 )
        return aFallback;

    std::string value = MdwTrimmed( result.out );

    return value.empty() ? aFallback : value;
}


/// Build the item record for a tracked path, filling revision and mtime from
/// the last commit that touched it.
ws::ItemInfo itemInfo( const std::string& aPath )
{
    ws::ItemInfo item;
    item.set_item_id( aPath );
    item.set_display_name( MdwBaseName( aPath ) );
    item.set_folder_id( MdwParentDir( aPath ) );
    item.set_is_folder( false );
    item.set_category( ws::LC_EDITABLE );

    MDW_PROCESS_RESULT last = Git( { "log", "-1", "--format=%h%x1f%at", "--", aPath } );

    if( last.started && last.exit_code == 0 )
    {
        std::vector<std::string> fields = MdwSplit( MdwTrimmed( last.out ), FIELD_SEP );

        if( fields.size() >= 1 && !fields[0].empty() )
            item.set_revision_id( fields[0] );

        if( fields.size() >= 2 && !fields[1].empty() )
            item.set_modified_unix( std::strtoll( fields[1].c_str(), nullptr, 10 ) );
    }

    return item;
}


void registerHandlers( MDW_BACKEND_SERVER& aServer )
{
    aServer.RegisterHandler<mdw::AuthenticateRequest, mdw::AuthResult>(
            []( const mdw::AuthenticateRequest&, mdw::AuthResult& aReply ) -> ws::WorkspaceError
            {
                aReply.set_authenticated( true );
                aReply.set_user_id( configValue( "user.email", "local" ) );
                aReply.set_display_name( configValue( "user.name", "local" ) );
                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::LogoutRequest, mdw::LogoutReply>(
            []( const mdw::LogoutRequest&, mdw::LogoutReply& aReply ) -> ws::WorkspaceError
            {
                aReply.set_ok( true );
                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::IsAuthenticatedRequest, mdw::IsAuthenticatedReply>(
            []( const mdw::IsAuthenticatedRequest&,
                mdw::IsAuthenticatedReply& aReply ) -> ws::WorkspaceError
            {
                aReply.set_authenticated( true );
                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::GetRootFoldersRequest, mdw::GetRootFoldersReply>(
            []( const mdw::GetRootFoldersRequest&,
                mdw::GetRootFoldersReply& aReply ) -> ws::WorkspaceError
            {
                std::optional<std::vector<std::string>> files = GitLsFiles();

                if( !files )
                    return MdwFail( ws::WE_INTERNAL, "git failed" );

                ws::Folder* folder = aReply.add_folders();
                folder->set_folder_id( "" );
                folder->set_display_name( "Root" );

                for( const std::string& path : *files )
                {
                    if( path.find( '/' ) != std::string::npos )
                    {
                        folder->set_has_children( true );
                        break;
                    }
                }

                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::GetChildFoldersRequest, mdw::GetChildFoldersReply>(
            []( const mdw::GetChildFoldersRequest& aRequest,
                mdw::GetChildFoldersReply& aReply ) -> ws::WorkspaceError
            {
                std::optional<std::vector<std::string>> files = GitLsFiles();

                if( !files )
                    return MdwFail( ws::WE_INTERNAL, "git failed" );

                // git tracks files, not directories, so the folders are the
                // directories the tracked files sit in.
                std::set<std::string> folders;

                for( const std::string& path : *files )
                {
                    std::string dir = MdwParentDir( path );

                    if( !dir.empty() )
                        folders.insert( dir );
                }

                MdwAddChildFolders( folders, aRequest.folder_id(), aReply );

                return MdwOk();
            } );


    aServer.RegisterHandler<mdw::ListItemsRequest, mdw::ListItemsReply>(
            []( const mdw::ListItemsRequest& aRequest,
                mdw::ListItemsReply& aReply ) -> ws::WorkspaceError
            {
                std::optional<std::vector<std::string>> files = GitLsFiles();

                if( !files )
                    return MdwFail( ws::WE_INTERNAL, "git failed" );

                for( const std::string& path : *files )
                {
                    if( MdwParentDir( path ) == aRequest.folder_id() )
                        *aReply.add_items() = itemInfo( path );
                }

                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::GetItemInfoRequest, mdw::GetItemInfoReply>(
            []( const mdw::GetItemInfoRequest& aRequest,
                mdw::GetItemInfoReply& aReply ) -> ws::WorkspaceError
            {
                if( !IsSafePath( aRequest.item_id() ) )
                    return MdwFail( ws::WE_ITEM_NOT_FOUND, "invalid item path" );

                if( !GitTracked( aRequest.item_id() ) )
                    return MdwFail( ws::WE_ITEM_NOT_FOUND, "no such tracked file" );

                *aReply.mutable_item() = itemInfo( aRequest.item_id() );
                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::CheckoutRequest, mdw::CheckoutReply>(
            []( const mdw::CheckoutRequest& aRequest,
                mdw::CheckoutReply& aReply ) -> ws::WorkspaceError
            {
                if( !IsSafePath( aRequest.item_id() ) )
                    return MdwFail( ws::WE_ITEM_NOT_FOUND, "invalid item path" );

                if( !GitTracked( aRequest.item_id() ) )
                    return MdwFail( ws::WE_ITEM_NOT_FOUND, "no such tracked file" );

                *aReply.mutable_item() = itemInfo( aRequest.item_id() );

                if( !aRequest.local_path().empty() )
                    aReply.set_local_path( aRequest.local_path() );
                else
                    aReply.set_local_path( GitRepo() + "/" + aRequest.item_id() );

                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::CheckinRequest, mdw::CheckinReply>(
            []( const mdw::CheckinRequest& aRequest,
                mdw::CheckinReply& aReply ) -> ws::WorkspaceError
            {
                if( !IsSafePath( aRequest.item_id() ) )
                    return MdwFail( ws::WE_ITEM_NOT_FOUND, "invalid item path" );

                MDW_PROCESS_RESULT add = Git( { "add", "--", aRequest.item_id() } );

                if( !add.started || add.exit_code != 0 )
                    return MdwFail( ws::WE_INTERNAL, add.err );

                MDW_PROCESS_RESULT commit =
                        Git( { "commit", "-m", aRequest.comment(), "--", aRequest.item_id() } );

                if( !commit.started || commit.exit_code != 0 )
                {
                    std::string combined = commit.out + commit.err;

                    if( combined.find( "nothing to commit" ) != std::string::npos )
                        return MdwFail( ws::WE_NO_CHANGES, "nothing to commit" );

                    return MdwFail( ws::WE_INTERNAL, commit.err );
                }

                MDW_PROCESS_RESULT head = Git( { "rev-parse", "--short", "HEAD" } );

                ws::Revision* rev = aReply.mutable_revision();
                rev->set_revision_id( MdwTrimmed( head.out ) );
                rev->set_author( configValue( "user.email", "local" ) );
                rev->set_comment( aRequest.comment() );
                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::CancelCheckoutRequest, mdw::CancelCheckoutReply>(
            []( const mdw::CancelCheckoutRequest& aRequest,
                mdw::CancelCheckoutReply& ) -> ws::WorkspaceError
            {
                if( !IsSafePath( aRequest.item_id() ) )
                    return MdwFail( ws::WE_ITEM_NOT_FOUND, "invalid item path" );

                MDW_PROCESS_RESULT result = Git( { "checkout", "--", aRequest.item_id() } );

                if( !result.started || result.exit_code != 0 )
                    return MdwFail( ws::WE_INTERNAL, result.err );

                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::GetRevisionHistoryRequest, mdw::GetRevisionHistoryReply>(
            []( const mdw::GetRevisionHistoryRequest& aRequest,
                mdw::GetRevisionHistoryReply& aReply ) -> ws::WorkspaceError
            {
                if( !IsSafePath( aRequest.item_id() ) )
                    return MdwFail( ws::WE_ITEM_NOT_FOUND, "invalid item path" );

                MDW_PROCESS_RESULT log =
                        Git( { "-c", "core.quotePath=false", "log", "-z",
                               "--format=%H%x1f%P%x1f%an%x1f%at%x1f%s", "--", aRequest.item_id() } );

                if( !log.started || log.exit_code != 0 )
                    return MdwFail( ws::WE_ITEM_NOT_FOUND, log.err );

                for( const std::string& record : MdwSplit( log.out, '\0' ) )
                {
                    if( record.empty() )
                        continue;

                    std::vector<std::string> fields = MdwSplit( record, FIELD_SEP, LOG_FIELDS );

                    if( fields.size() < LOG_FIELDS )
                        continue;

                    ws::Revision* rev = aReply.add_revisions();
                    rev->set_revision_id( fields[0] );

                    for( const std::string& parent : MdwSplit( fields[1], ' ' ) )
                    {
                        if( !parent.empty() )
                            rev->add_parent_revision_ids( parent );
                    }

                    rev->set_author( fields[2] );
                    rev->set_timestamp_unix( std::strtoll( fields[3].c_str(), nullptr, 10 ) );
                    rev->set_comment( fields[4] );
                }

                return MdwOk();
            } );

    aServer.RegisterHandler<mdw::DownloadRevisionRequest, mdw::DownloadRevisionReply>(
            []( const mdw::DownloadRevisionRequest& aRequest,
                mdw::DownloadRevisionReply& ) -> ws::WorkspaceError
            {
                if( !IsSafeRef( aRequest.revision_id() ) )
                    return MdwFail( ws::WE_INVALID_PARAMETER, "invalid revision id" );

                if( !IsSafePath( aRequest.item_id() ) )
                    return MdwFail( ws::WE_ITEM_NOT_FOUND, "invalid item path" );

                MDW_PROCESS_RESULT show =
                        Git( { "show", aRequest.revision_id() + ":" + aRequest.item_id() } );

                if( !show.started || show.exit_code != 0 )
                    return MdwFail( ws::WE_REVISION_NOT_FOUND, show.err );

                std::ofstream out( aRequest.local_path(), std::ios::binary | std::ios::trunc );

                if( !out.is_open() )
                    return MdwFail( ws::WE_INTERNAL, "cannot open local path for write" );

                out.write( show.out.data(), static_cast<std::streamsize>( show.out.size() ) );
                out.flush();
                out.close();

                if( !out.good() )
                    return MdwFail( ws::WE_INTERNAL, "write failed" );

                return MdwOk();
            } );
}


void buildDescriptor( MDW_BACKEND_DESCRIPTOR& aDescriptor )
{
    aDescriptor.info.set_backend_id( "org.kicad.mdw.git" );
    aDescriptor.info.set_display_name( "Git Workspace" );
    aDescriptor.info.mutable_version()->set_major( 1 );
    aDescriptor.info.mutable_version()->set_minor( 0 );

    aDescriptor.capabilities.add_groups( ws::CG_REVISIONS );

    aDescriptor.schema.set_schema_version( "1" );
}

} // namespace


int main( int argc, char** argv )
{
    // Run git in the C locale so its diagnostic messages are stable and the
    // "nothing to commit" test in Checkin is not locale-dependent.
#if !defined( _WIN32 )
    setenv( "LC_ALL", "C", 1 );
    setenv( "LANG", "C", 1 );
#endif

    MDW_BACKEND_LAUNCH launch = MDW_ParseLaunch( argc, argv );

    MDW_BACKEND_DESCRIPTOR descriptor;
    buildDescriptor( descriptor );

    MDW_BACKEND_SERVER server;
    registerHandlers( server );

    return MDW_RunBackend( launch, descriptor, server, nullptr );
}
