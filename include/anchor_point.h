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

#pragma once

#include <string>

/**
 * The nine points of an image bounding box that the stored position of a
 * reference image can be pinned to.  CENTER is the default (and matches the
 * historical KiCad behaviour, where the stored position is always the center).
 */
enum class ANCHOR_POINT : int
{
    TOP_LEFT = 0,
    TOP_CENTER,
    TOP_RIGHT,
    CENTER_LEFT,
    CENTER,
    CENTER_RIGHT,
    BOTTOM_LEFT,
    BOTTOM_CENTER,
    BOTTOM_RIGHT
};


/**
 * @return the S-expression token string for an anchor point.
 */
inline std::string getAnchorPointString( ANCHOR_POINT aAnchor )
{
    switch( aAnchor )
    {
    case ANCHOR_POINT::TOP_LEFT:      return "top_left";
    case ANCHOR_POINT::TOP_CENTER:    return "top_center";
    case ANCHOR_POINT::TOP_RIGHT:     return "top_right";
    case ANCHOR_POINT::CENTER_LEFT:   return "center_left";
    case ANCHOR_POINT::CENTER:        return "center";
    case ANCHOR_POINT::CENTER_RIGHT:  return "center_right";
    case ANCHOR_POINT::BOTTOM_LEFT:   return "bottom_left";
    case ANCHOR_POINT::BOTTOM_CENTER: return "bottom_center";
    case ANCHOR_POINT::BOTTOM_RIGHT:  return "bottom_right";
    default:                          return "center";
    }
}


/**
 * Map an anchor token string back to an ANCHOR_POINT (unknown -> CENTER).
 */
inline ANCHOR_POINT anchorPointFromString( const std::string& aStr )
{
    if( aStr == "top_left" )      return ANCHOR_POINT::TOP_LEFT;
    if( aStr == "top_center" )    return ANCHOR_POINT::TOP_CENTER;
    if( aStr == "top_right" )     return ANCHOR_POINT::TOP_RIGHT;
    if( aStr == "center_left" )   return ANCHOR_POINT::CENTER_LEFT;
    if( aStr == "center" )        return ANCHOR_POINT::CENTER;
    if( aStr == "center_right" )  return ANCHOR_POINT::CENTER_RIGHT;
    if( aStr == "bottom_left" )   return ANCHOR_POINT::BOTTOM_LEFT;
    if( aStr == "bottom_center" ) return ANCHOR_POINT::BOTTOM_CENTER;
    if( aStr == "bottom_right" )  return ANCHOR_POINT::BOTTOM_RIGHT;

    return ANCHOR_POINT::CENTER;
}
