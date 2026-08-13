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
 * The validation half of the schema-driven property editor (proposal tests 7
 * and 17): every attribute type validates its entries, required fields are
 * enforced, and optional empty values are accepted. The widget rendering is
 * verified interactively.
 */

#include <boost/test/unit_test.hpp>

#include <mdw/mdw_schema_validator.h>

namespace
{
MDW_ATTRIBUTE_SCHEMA attr( const wxString& aId, MDW_ATTR_TYPE aType, bool aRequired = false )
{
    MDW_ATTRIBUTE_SCHEMA a;
    a.attr_id = aId;
    a.display_name = aId;
    a.type = aType;
    a.required = aRequired;
    return a;
}
}


BOOST_AUTO_TEST_SUITE( MdwSchemaValidator )


BOOST_AUTO_TEST_CASE( AcceptsWellTypedValues )
{
    wxString error;

    BOOST_CHECK( MDW_SCHEMA_VALIDATOR::ValidateValue( attr( "n", MDW_ATTR_TYPE::INTEGER ), "42", error ) );
    BOOST_CHECK( MDW_SCHEMA_VALIDATOR::ValidateValue( attr( "f", MDW_ATTR_TYPE::FLOAT ), "3.14", error ) );
    BOOST_CHECK( MDW_SCHEMA_VALIDATOR::ValidateValue( attr( "b", MDW_ATTR_TYPE::BOOLEAN ), "true", error ) );
    BOOST_CHECK( MDW_SCHEMA_VALIDATOR::ValidateValue( attr( "d", MDW_ATTR_TYPE::DATE ), "2026-07-10", error ) );
    BOOST_CHECK( MDW_SCHEMA_VALIDATOR::ValidateValue( attr( "s", MDW_ATTR_TYPE::STRING ), "anything", error ) );
}


BOOST_AUTO_TEST_CASE( RejectsMistypedValues )
{
    wxString error;

    BOOST_CHECK( !MDW_SCHEMA_VALIDATOR::ValidateValue( attr( "n", MDW_ATTR_TYPE::INTEGER ), "abc", error ) );
    BOOST_CHECK( !MDW_SCHEMA_VALIDATOR::ValidateValue( attr( "f", MDW_ATTR_TYPE::FLOAT ), "x", error ) );
    BOOST_CHECK( !MDW_SCHEMA_VALIDATOR::ValidateValue( attr( "b", MDW_ATTR_TYPE::BOOLEAN ), "maybe", error ) );
    BOOST_CHECK( !MDW_SCHEMA_VALIDATOR::ValidateValue( attr( "d", MDW_ATTR_TYPE::DATE ), "not-a-date", error ) );
}


BOOST_AUTO_TEST_CASE( EnumMembership )
{
    MDW_ATTRIBUTE_SCHEMA choice = attr( "status", MDW_ATTR_TYPE::ENUM );
    choice.enum_values = { "draft", "review", "final" };

    wxString error;
    BOOST_CHECK( MDW_SCHEMA_VALIDATOR::ValidateValue( choice, "review", error ) );
    BOOST_CHECK( !MDW_SCHEMA_VALIDATOR::ValidateValue( choice, "shipped", error ) );
}


BOOST_AUTO_TEST_CASE( RequiredAndOptionalEmpty )
{
    wxString error;

    BOOST_CHECK( !MDW_SCHEMA_VALIDATOR::ValidateValue( attr( "r", MDW_ATTR_TYPE::STRING, true ), "", error ) );
    BOOST_CHECK( MDW_SCHEMA_VALIDATOR::ValidateValue( attr( "o", MDW_ATTR_TYPE::STRING, false ), "", error ) );
}


BOOST_AUTO_TEST_CASE( ValidatesFullSet )
{
    MDW_SCHEMA schema;
    schema.attributes.push_back( attr( "owner", MDW_ATTR_TYPE::STRING, true ) );
    schema.attributes.push_back( attr( "count", MDW_ATTR_TYPE::INTEGER ) );

    MDW_ATTRIBUTE_SET good;
    good.Set( "owner", "alice" );
    good.Set( "count", "7" );
    BOOST_CHECK( MDW_SCHEMA_VALIDATOR::Validate( schema, good ).empty() );

    MDW_ATTRIBUTE_SET bad;
    bad.Set( "count", "seven" );
    std::vector<MDW_SCHEMA_VALIDATOR::ERROR> errors = MDW_SCHEMA_VALIDATOR::Validate( schema, bad );
    BOOST_CHECK_EQUAL( errors.size(), 2u );
}


BOOST_AUTO_TEST_SUITE_END()
