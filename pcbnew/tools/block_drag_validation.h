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
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef BLOCK_DRAG_VALIDATION_H
#define BLOCK_DRAG_VALIDATION_H

#include <utility>
#include <vector>

class BOARD;
class BOARD_ITEM;
class ZONE;

enum class BLOCK_DRAG_VALIDATION_RESULT
{
    OK,

    /// A carried zone's outline became self-intersecting.
    INVALID_ZONE,

    /// The board outline was closed before the drag and the moved Edge.Cuts broke it.
    INVALID_OUTLINE
};

/**
 * Checks a Block Drag's result at release and says whether it may be kept.
 *
 * Frame free by design so the QA harness can drive it: it reads only the board
 * model. Capture the baseline before the first mutation, then validate at release.
 * Pre-existing damage never rejects a gesture: a zone that was already
 * self-intersecting and an outline that was already broken are recorded at capture
 * and only newly introduced damage fails validation.
 */
class BLOCK_DRAG_VALIDATION
{
public:
    BLOCK_DRAG_VALIDATION( BOARD& aBoard );

    /// Note whether the zones coming along, and the board outline, were valid to start with.
    void CaptureBaseline( const std::vector<BOARD_ITEM*>& aCarriedItems );

    BLOCK_DRAG_VALIDATION_RESULT Validate() const;

private:
    bool outlineIsValid() const;

    BOARD*                              m_board;
    std::vector<std::pair<ZONE*, bool>> m_zones; // zone and whether it started out valid
    bool                                m_outlineAffected;
    bool                                m_outlineWasValid;
};

#endif // BLOCK_DRAG_VALIDATION_H
