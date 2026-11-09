// MOD1: a gun or tool with explicit pocket_data keeps (gets) its MOD pocket, as in DDA
// (DDA item_factory.cpp:4136-4140); armor and items that declared their own MOD pocket are untouched.
#include <algorithm>
#include <string>

#include "catch/catch.hpp"
#include "itype.h"
#include "type_id.h"

static auto mod_pockets( const std::string &id ) -> int
{
    REQUIRE( itype_id( id ).is_valid() );
    const itype &t = itype_id( id ).obj();
    return std::count_if( t.pockets.begin(), t.pockets.end(), []( const pocket_data & pd ) {
        return pd.type == pocket_type::MOD;
    } );
}

TEST_CASE( "explicit_pocket_gun_has_one_mod_pocket", "[p3][modpocket]" )
{
    REQUIRE( itype_id( "test_multimag_gun_integral_ammo" ).obj().explicit_pockets );
    CHECK( mod_pockets( "test_multimag_gun_integral_ammo" ) == 1 );
}

TEST_CASE( "explicit_pocket_tool_has_one_mod_pocket", "[p3][modpocket]" )
{
    REQUIRE( itype_id( "test_multimag_tool_consume" ).obj().explicit_pockets );
    CHECK( mod_pockets( "test_multimag_tool_consume" ) == 1 );
}

TEST_CASE( "a_declared_mod_pocket_is_not_doubled", "[p3][modpocket]" )
{
    REQUIRE( itype_id( "p3_multimag_gun_integral_ammo" ).obj().explicit_pockets );
    CHECK( mod_pockets( "p3_multimag_gun_integral_ammo" ) == 1 );
    CHECK( mod_pockets( "p3_multimag_tool_consume" ) == 1 );
}

TEST_CASE( "explicit_pocket_armor_gets_no_mod_pocket", "[p3][modpocket]" )
{
    REQUIRE( itype_id( "modular_papr_strap" ).obj().explicit_pockets );
    CHECK( mod_pockets( "modular_papr_strap" ) == 0 );
}

TEST_CASE( "legacy_and_plain_guns_still_have_one_mod_pocket", "[p3][modpocket]" )
{
    CHECK( mod_pockets( "glock_19" ) == 1 );
    CHECK( mod_pockets( "m1903" ) == 1 );
}
