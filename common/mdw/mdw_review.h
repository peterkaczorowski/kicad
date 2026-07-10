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
 * @file mdw_review.h
 * @brief Design review value structs for the Managed Design Workspace.
 *
 * The framework reads review identity, its reviewers, and the revision under
 * review; it does not drive the review workflow. state is a backend display
 * string the UI renders verbatim.
 */

#ifndef MDW_REVIEW_H
#define MDW_REVIEW_H

#include "mdw_types.h"
#include "mdw_schema.h"

#include <wx/string.h>
#include <vector>

/**
 * A design review as reported by the backend.
 */
struct MDW_REVIEW
{
    wxString              review_id;
    wxString              title;
    wxString              state;
    wxString              author;
    std::vector<wxString> reviewer_ids;
    wxString              revision_id;
};


/**
 * A single resolved review.
 */
struct MDW_REVIEW_RESULT : public MDW_RESULT
{
    MDW_REVIEW review;
};


/**
 * The vendor attribute vocabulary for reviews.
 */
struct MDW_REVIEW_SCHEMA_RESULT : public MDW_RESULT
{
    MDW_SCHEMA schema;
};


#endif // MDW_REVIEW_H
