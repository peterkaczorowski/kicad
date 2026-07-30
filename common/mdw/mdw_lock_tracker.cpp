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

#include <mdw/mdw_lock_tracker.h>


void MDW_LOCK_TRACKER::Set( const wxString& aItemId, const MDW_LOCK_INFO& aInfo )
{
    std::lock_guard<std::mutex> lock( m_mutex );

    if( aInfo.is_locked )
        m_locks[aItemId] = aInfo;
    else
        m_locks.erase( aItemId );
}


MDW_LOCK_INFO MDW_LOCK_TRACKER::Get( const wxString& aItemId ) const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    auto                        it = m_locks.find( aItemId );

    return it == m_locks.end() ? MDW_LOCK_INFO{} : it->second;
}


bool MDW_LOCK_TRACKER::IsLocked( const wxString& aItemId ) const
{
    std::lock_guard<std::mutex> lock( m_mutex );

    return m_locks.find( aItemId ) != m_locks.end();
}


bool MDW_LOCK_TRACKER::LockedByMe( const wxString& aItemId ) const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    auto                        it = m_locks.find( aItemId );

    return it != m_locks.end() && it->second.locked_by_me;
}


void MDW_LOCK_TRACKER::Remove( const wxString& aItemId )
{
    std::lock_guard<std::mutex> lock( m_mutex );
    m_locks.erase( aItemId );
}


void MDW_LOCK_TRACKER::Clear()
{
    std::lock_guard<std::mutex> lock( m_mutex );
    m_locks.clear();
}
