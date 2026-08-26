/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * The save guard's warn decision is pure. The confirmation dialog itself is
 * shown on the main thread and verified live.
 */

#include <boost/test/unit_test.hpp>

#include <mdw/mdw_save_guard.h>


BOOST_AUTO_TEST_SUITE( MdwSaveGuard )


BOOST_AUTO_TEST_CASE( EditableDoesNotWarn )
{
    BOOST_CHECK( !MDW_SAVE_GUARD::NeedsWarning( MDW_LIFECYCLE_CATEGORY::EDITABLE ) );
}


BOOST_AUTO_TEST_CASE( WriteBlockingCategoriesWarn )
{
    BOOST_CHECK( MDW_SAVE_GUARD::NeedsWarning( MDW_LIFECYCLE_CATEGORY::PENDING_REVIEW ) );
    BOOST_CHECK( MDW_SAVE_GUARD::NeedsWarning( MDW_LIFECYCLE_CATEGORY::RESTRICTED ) );
    BOOST_CHECK( MDW_SAVE_GUARD::NeedsWarning( MDW_LIFECYCLE_CATEGORY::RELEASED ) );
}


BOOST_AUTO_TEST_SUITE_END()
