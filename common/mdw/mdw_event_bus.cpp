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

#include <mdw/mdw_event_bus.h>

#include <vector>


MDW_EVENT_BUS::FINGERPRINT MDW_EVENT_BUS::fingerprint( const MDW_EVENT& aEvent )
{
    // The workspace id is part of the key because item ids are only unique within
    // a workspace, so an echo must not match a same-named item elsewhere.
    return { static_cast<int>( aEvent.type ), aEvent.workspace_id, aEvent.item_id };
}


std::uint64_t MDW_EVENT_BUS::Subscribe( HANDLER aHandler )
{
    std::lock_guard<std::mutex> lock( m_mutex );
    std::uint64_t               token = ++m_nextToken;
    m_handlers[token] = std::move( aHandler );

    return token;
}


void MDW_EVENT_BUS::Unsubscribe( std::uint64_t aToken )
{
    std::lock_guard<std::mutex> lock( m_mutex );
    m_handlers.erase( aToken );
}


void MDW_EVENT_BUS::dispatch( const MDW_EVENT& aEvent )
{
    std::vector<HANDLER> handlers;

    {
        std::lock_guard<std::mutex> lock( m_mutex );

        for( const auto& [token, handler] : m_handlers )
            handlers.push_back( handler );
    }

    // Called outside the lock so a handler may subscribe, unsubscribe, or emit.
    for( const HANDLER& handler : handlers )
        handler( aEvent );
}


void MDW_EVENT_BUS::PublishLocal( const MDW_EVENT& aEvent, bool aExpectEcho )
{
    if( aExpectEcho )
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_pendingEchoes.insert( fingerprint( aEvent ) );
    }

    dispatch( aEvent );
}


void MDW_EVENT_BUS::PublishFromBackend( const MDW_EVENT& aEvent )
{
    {
        std::lock_guard<std::mutex> lock( m_mutex );

        // Record the sequence first so a later replay of this seq is recognized
        // as already-seen even when the event is consumed here as an echo.
        if( aEvent.seq != 0 )
        {
            std::uint64_t& last = m_lastSeq[aEvent.workspace_id];

            if( aEvent.seq <= last )
                return;

            last = aEvent.seq;
        }

        auto echo = m_pendingEchoes.find( fingerprint( aEvent ) );

        if( echo != m_pendingEchoes.end() )
        {
            // This is the backend echoing an operation we already emitted locally.
            m_pendingEchoes.erase( echo );
            return;
        }
    }

    dispatch( aEvent );
}
