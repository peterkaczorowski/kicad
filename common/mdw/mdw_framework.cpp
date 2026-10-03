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
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <mdw/mdw_framework.h>


MDW_FRAMEWORK::MDW_FRAMEWORK()
{
    m_registry.Discover();
}


MDW_FRAMEWORK::~MDW_FRAMEWORK()
{
    Shutdown();
}


MDW_SESSION* MDW_FRAMEWORK::OpenSession( const wxString& aProjectPath,
                                        const MDW_PROJECT_BINDING& aBinding,
                                        const wxString& aKicadVersion, int aTimeoutMs )
{
    if( !aBinding.IsBound() )
        return nullptr;

    const MDW_BACKEND_MANIFEST* manifest = m_registry.Find( aBinding.backend_id );

    if( !manifest )
        return nullptr;

    auto supervisor = std::make_unique<MDW_BACKEND_SUPERVISOR>( *manifest );

    if( !supervisor->Launch( aKicadVersion, aTimeoutMs ) )
        return nullptr;

    auto session = std::make_unique<MDW_SESSION>( aProjectPath, aBinding, std::move( supervisor ) );
    MDW_SESSION* raw = session.get();

    // Authenticate before the session is published, so nothing can borrow it and
    // issue operations while it is still anonymous. A failure is not fatal: the
    // session stays open and unauthenticated so the UI can report it and offer
    // Configure, rather than the project silently appearing unbound.
    session->Authenticate( m_credentialStore, aTimeoutMs );

    std::unique_ptr<MDW_SESSION> previous;

    {
        std::lock_guard<std::mutex> lock( m_mutex );
        auto                        it = m_sessions.find( aProjectPath );

        if( it != m_sessions.end() )
            previous = std::move( it->second );

        m_sessions[aProjectPath] = std::move( session );
    }

    // A replaced session (and its backend teardown, which can take seconds) is
    // destroyed here, outside the lock, so it does not block other lookups.
    previous.reset();

    return raw;
}


MDW_SESSION* MDW_FRAMEWORK::Session( const wxString& aProjectPath ) const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    auto                        it = m_sessions.find( aProjectPath );

    return it == m_sessions.end() ? nullptr : it->second.get();
}


void MDW_FRAMEWORK::CloseSession( const wxString& aProjectPath )
{
    std::unique_ptr<MDW_SESSION> session;

    {
        std::lock_guard<std::mutex> lock( m_mutex );
        auto                        it = m_sessions.find( aProjectPath );

        if( it != m_sessions.end() )
        {
            session = std::move( it->second );
            m_sessions.erase( it );
        }
    }

    // The session (and its supervisor) is destroyed here, outside the lock, so a
    // slow backend shutdown does not block other session lookups.
}


void MDW_FRAMEWORK::Shutdown()
{
    std::map<wxString, std::unique_ptr<MDW_SESSION>> sessions;

    {
        std::lock_guard<std::mutex> lock( m_mutex );
        sessions.swap( m_sessions );
    }

    sessions.clear();
}
