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

/**
 * @file mdw_lock_tracker.h
 * @brief The last-known per-user lock state for managed items.
 *
 * The lock tracker is one of the two independent state axes; the other is the
 * lifecycle category. It is shared across every open frame so a lock event
 * propagates to all pills coherently. It never encodes lifecycle state.
 */

#ifndef MDW_LOCK_TRACKER_H
#define MDW_LOCK_TRACKER_H

#include <kicommon.h>
#include "mdw_lock.h"

#include <map>
#include <mutex>

#include <wx/string.h>

class KICOMMON_API MDW_LOCK_TRACKER
{
public:
    void Set( const wxString& aItemId, const MDW_LOCK_INFO& aInfo );

    MDW_LOCK_INFO Get( const wxString& aItemId ) const;

    bool IsLocked( const wxString& aItemId ) const;

    bool LockedByMe( const wxString& aItemId ) const;

    void Remove( const wxString& aItemId );

    void Clear();

private:
    mutable std::mutex               m_mutex;
    std::map<wxString, MDW_LOCK_INFO> m_locks;
};

#endif // MDW_LOCK_TRACKER_H
