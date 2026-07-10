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
 * @file mdw_permissions.h
 * @brief Effective per-item permissions for the Managed Design Workspace.
 *
 * The backend resolves policy and role into a flat set of booleans for the
 * current user against one item. The UI greys out actions the user cannot take
 * rather than discovering the denial on dispatch.
 */

#ifndef MDW_PERMISSIONS_H
#define MDW_PERMISSIONS_H

#include "mdw_types.h"

/**
 * The actions the current user may take on an item.
 */
struct MDW_PERMISSIONS
{
    bool can_checkout = false;
    bool can_checkin = false;
    bool can_lock = false;
    bool can_transition = false;
    bool can_edit_attributes = false;
    bool can_manage = false;
};


/**
 * The resolved permissions for an item.
 */
struct MDW_PERMISSIONS_RESULT : public MDW_RESULT
{
    MDW_PERMISSIONS permissions;
};


#endif // MDW_PERMISSIONS_H
