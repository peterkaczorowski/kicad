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

#include <mdw/mdw_session.h>


MDW_SESSION::MDW_SESSION( const wxString& aProjectPath, const MDW_PROJECT_BINDING& aBinding,
                          std::unique_ptr<MDW_BACKEND_SUPERVISOR> aSupervisor ) :
        m_projectPath( aProjectPath ),
        m_binding( aBinding ),
        m_supervisor( std::move( aSupervisor ) )
{
}


MDW_SESSION::~MDW_SESSION()
{
}


MDW_BACKEND* MDW_SESSION::Backend() const
{
    return m_supervisor ? m_supervisor->Backend() : nullptr;
}


bool MDW_SESSION::Online() const
{
    return m_supervisor && m_supervisor->Online();
}
