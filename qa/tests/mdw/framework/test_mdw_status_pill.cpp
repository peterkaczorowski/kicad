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
 * The status pill label mapping is pure, so it is exercised here without a
 * display. The widget itself is drawn on the main thread and verified live.
 */

#include <boost/test/unit_test.hpp>

#include <widgets/mdw_status_pill.h>
#include <mdw/mdw_state_mapper.h>


BOOST_AUTO_TEST_SUITE( MdwStatusPill )


BOOST_AUTO_TEST_CASE( UnboundLabelIsEmpty )
{
    MDW_STATUS_PILL::STATE state;
    state.bound = false;

    BOOST_CHECK( MDW_STATUS_PILL::LabelFor( state ).IsEmpty() );
}


BOOST_AUTO_TEST_CASE( OfflineLabelAnnotatesOffline )
{
    MDW_STATUS_PILL::STATE state;
    state.bound = true;
    state.online = false;

    BOOST_CHECK( MDW_STATUS_PILL::LabelFor( state ).Lower().Contains( wxS( "offline" ) ) );
}


BOOST_AUTO_TEST_CASE( OnlineLabelShowsCategory )
{
    MDW_STATUS_PILL::STATE state;
    state.bound = true;
    state.online = true;
    state.category = MDW_LIFECYCLE_CATEGORY::EDITABLE;

    wxString expected = MDW_STATE_MAPPER::CategoryDisplayName( MDW_LIFECYCLE_CATEGORY::EDITABLE );

    BOOST_CHECK_EQUAL( MDW_STATUS_PILL::LabelFor( state ), expected );
}


BOOST_AUTO_TEST_CASE( LockedByMeIsDistinctFromLockedByOther )
{
    MDW_STATUS_PILL::STATE mine;
    mine.bound = true;
    mine.online = true;
    mine.locked = true;
    mine.locked_by_me = true;

    MDW_STATUS_PILL::STATE theirs = mine;
    theirs.locked_by_me = false;

    wxString mineLabel = MDW_STATUS_PILL::LabelFor( mine );
    wxString theirsLabel = MDW_STATUS_PILL::LabelFor( theirs );

    BOOST_CHECK( mineLabel.Lower().Contains( wxS( "you" ) ) );
    BOOST_CHECK( !theirsLabel.Lower().Contains( wxS( "you" ) ) );
    BOOST_CHECK( mineLabel != theirsLabel );
}


BOOST_AUTO_TEST_SUITE_END()
