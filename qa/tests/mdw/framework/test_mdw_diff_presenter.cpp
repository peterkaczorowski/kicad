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
 * The diff presenter derives the document kind from the file extension and
 * routes to the mainline diff. The virtual dispatch seam lets that routing be
 * checked without a live kiface.
 */

#include <boost/test/unit_test.hpp>

#include <mdw/mdw_diff_presenter.h>


namespace
{
class RECORDING_PRESENTER : public MDW_DIFF_PRESENTER
{
public:
    RECORDING_PRESENTER() : MDW_DIFF_PRESENTER( nullptr ) {}

    KICAD_DIFF::DOC_KIND last_kind = KICAD_DIFF::DOC_KIND::UNKNOWN;
    bool                 dispatched = false;

protected:
    int dispatchDiff( KICAD_DIFF::DOC_KIND aKind, const wxString&, const wxString&, const wxString&,
                      const wxString& ) override
    {
        last_kind = aKind;
        dispatched = true;
        return 0;
    }
};
}


BOOST_AUTO_TEST_SUITE( MdwDiffPresenter )


BOOST_AUTO_TEST_CASE( RoutesByExtension )
{
    RECORDING_PRESENTER presenter;

    BOOST_CHECK( presenter.Compare( "board.kicad_pcb", "board.old.kicad_pcb", "r2", "r1" ) );
    BOOST_CHECK( presenter.last_kind == KICAD_DIFF::DOC_KIND::PCB );

    BOOST_CHECK( presenter.Compare( "sheet.kicad_sch", "sheet.old.kicad_sch", "r2", "r1" ) );
    BOOST_CHECK( presenter.last_kind == KICAD_DIFF::DOC_KIND::SCH );
}


BOOST_AUTO_TEST_CASE( SkipsUnknownExtension )
{
    RECORDING_PRESENTER presenter;

    BOOST_CHECK( !presenter.Compare( "notes.md", "notes.old.md", "r2", "r1" ) );
    BOOST_CHECK( !presenter.dispatched );
}


BOOST_AUTO_TEST_SUITE_END()
