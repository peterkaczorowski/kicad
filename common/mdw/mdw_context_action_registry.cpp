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

#include <mdw/mdw_context_action_registry.h>

#include <kiid.h>


void MDW_CONTEXT_ACTION_REGISTRY::SetConsentHandler( CONSENT_FN aHandler )
{
    std::lock_guard<std::mutex> lock( m_mutex );
    m_consent = std::move( aHandler );
}


bool MDW_CONTEXT_ACTION_REGISTRY::consents( const wxString& aClientIdentity )
{
    CONSENT_FN handler;

    {
        std::lock_guard<std::mutex> lock( m_mutex );
        auto                        cached = m_decisions.find( aClientIdentity );

        if( cached != m_decisions.end() )
            return cached->second;

        handler = m_consent;
    }

    // Prompt with the lock released. The handler runs on the dispatch thread and
    // may open a modal dialog whose nested event loop dispatches another IPC
    // request back into the registry; holding a non-recursive mutex across that
    // would self-deadlock.
    bool granted = handler && handler( aClientIdentity );

    std::lock_guard<std::mutex> lock( m_mutex );

    // A racing prompt for the same identity may have already recorded a decision;
    // keep the first one so both callers agree.
    return m_decisions.try_emplace( aClientIdentity, granted ).first->second;
}


bool MDW_CONTEXT_ACTION_REGISTRY::Register( MDW_CONTEXT_ACTION& aSpec )
{
    if( !consents( aSpec.client_identity ) )
        return false;

    std::lock_guard<std::mutex> lock( m_mutex );

    aSpec.action_id = KIID().AsString();
    m_actions[aSpec.action_id] = aSpec;

    return true;
}


bool MDW_CONTEXT_ACTION_REGISTRY::Unregister( const wxString& aActionId,
                                              const wxString& aClientIdentity )
{
    std::lock_guard<std::mutex> lock( m_mutex );

    auto it = m_actions.find( aActionId );

    if( it == m_actions.end() || it->second.client_identity != aClientIdentity )
        return false;

    m_actions.erase( it );

    return true;
}


std::vector<MDW_CONTEXT_ACTION> MDW_CONTEXT_ACTION_REGISTRY::Actions() const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    std::vector<MDW_CONTEXT_ACTION> out;

    for( const auto& [id, action] : m_actions )
        out.push_back( action );

    return out;
}
