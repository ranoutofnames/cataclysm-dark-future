// DDA source: tests/multimag_test.cpp (DDA commit b4b62254ae2).
// Ported TEST_CASEs: expected_cost_per_use sums effective_qty (DDA:1799);
// CDF-only guard: expected_cost_per_use method string is neutral (Decision 3).

#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "examine_item_menu.h"
#include "flag.h"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"
#include "json.h"
#include "map/map.h"
#include "map_helpers.h"
#include "npc.h"
#include "state_helpers.h"
#include "units.h"

#include <sstream>
#include <string>
#include <vector>

static const itype_id itype_glock_19( "glock_19" );
static const itype_id itype_test_multimag_gun_consume( "test_multimag_gun_consume" );

// DDA tests/multimag_test.cpp:106-113.
static item make_loaded_glock()
{
    item mag( itype_id( "glockmag" ) );
    mag.put_in( item( itype_id( "9mm" ), calendar::turn, 15 ), pocket_type::MAGAZINE );
    item gun( itype_id( "glock_19" ) );
    gun.put_in( mag, pocket_type::MAGAZINE_WELL );
    // CDF: item's move constructor is deleted (src/item.h:241); copy into the
    // return value like tests/cdf_item_multimag_p3_test.cpp does.
    return item( gun );
}

// DDA tests/multimag_test.cpp:1267-1277.
static item make_multimag_consume_gun( int bb_count = 15, int batt = 100 )
{
    item gun( itype_id( "test_multimag_gun_consume" ) );
    item bb_mag( itype_id( "glockmag" ) );
    bb_mag.put_in( item( itype_id( "9mm" ), calendar::turn, bb_count ), pocket_type::MAGAZINE );
    item batt_mag( itype_id( "heavy_battery_cell" ) );
    batt_mag.put_in( item( itype_id( "battery" ), calendar::turn, batt ),
                     pocket_type::MAGAZINE );
    REQUIRE( gun.put_in( bb_mag, pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( gun.put_in( batt_mag, pocket_type::MAGAZINE_WELL ).success() );
    return item( gun ); // CDF: item move ctor deleted
}

// DDA tests/multimag_test.cpp:1279-1289.
static item make_multimag_consume_tool( int well_a_charges = 12, int well_b_charges = 30 )
{
    item tool( itype_id( "test_multimag_tool_consume" ) );
    item mag_a( itype_id( "glockmag" ) );
    mag_a.put_in( item( itype_id( "9mm" ), calendar::turn, well_a_charges ),
                  pocket_type::MAGAZINE );
    item mag_b( itype_id( "stanag30" ) );
    mag_b.put_in( item( itype_id( "556" ), calendar::turn, well_b_charges ),
                  pocket_type::MAGAZINE );
    REQUIRE( tool.put_in( mag_a, pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( tool.put_in( mag_b, pocket_type::MAGAZINE_WELL ).success() );
    return item( tool ); // CDF: item move ctor deleted
}

// DDA tests/multimag_test.cpp:1799-1809. The DDA literal 1 for the legacy gun
// is replaced by ammo_required(), because the CDF value is read from data.
TEST_CASE( "expected_cost_per_use_sums_effective_qty", "[p3][firing]" )
{
    item gun( itype_test_multimag_gun_consume );
    CHECK( gun.expected_cost_per_use() == 6 );

    item legacy( itype_glock_19 );
    CHECK( legacy.expected_cost_per_use() == legacy.ammo_required() );
}

// Decision 3: itype::ammo_scale is absent, so method_scale is neutral and any
// method string keeps the same cost.
TEST_CASE( "expected_cost_per_use_method_string_is_neutral", "[p3][firing]" )
{
    item gun( itype_test_multimag_gun_consume );
    CHECK( gun.expected_cost_per_use( "no_such_method" ) == 6 );

    item legacy( itype_glock_19 );
    CHECK( legacy.expected_cost_per_use( "no_such_method" ) == legacy.ammo_required() );
}

// DDA tests/multimag_test.cpp:1785-1797.
TEST_CASE( "format_consumption_requirements_scaling", "[p3][firing]" )
{
    item tool = make_multimag_consume_tool();
    const std::string s = tool.format_consumption_requirements(
                              /*method=*/"", gun_mode_id( "DEFAULT" ), /*uses=*/3 );
    // qty 2 * 3 = 6 from well_a, qty 1 * 3 = 3 from well_b.
    CHECK( s.find( '6' ) != std::string::npos );
    CHECK( s.find( '3' ) != std::string::npos );
    CHECK( s.find( '+' ) != std::string::npos );
}

// DDA tests/multimag_test.cpp:1923-1931, format SECTION only: the
// consume_shots section is owned by 8-1b.
TEST_CASE( "gunmod_added_modes_inherit_DEFAULT_format", "[p3][firing]" )
{
    item gun = make_multimag_consume_gun();
    const std::string s = gun.format_consumption_requirements(
                              /*method=*/"", gun_mode_id( "AUTO" ), /*uses=*/1 );
    CHECK_FALSE( s.empty() );
    // DEFAULT qty=1 + 5 -> string contains both "1" and "5".
    CHECK( s.find( '1' ) != std::string::npos );
    CHECK( s.find( '5' ) != std::string::npos );
}

// DDA tests/multimag_test.cpp:1943-1956.
TEST_CASE( "format_consumption_requirements_legacy_item", "[p3][firing]" )
{
    item gun = make_loaded_glock();
    REQUIRE_FALSE( gun.uses_firing_requirements() );
    const std::string s_one = gun.format_consumption_requirements(
                                  /*method=*/"", gun_mode_id( "DEFAULT" ), /*uses=*/1 );
    // glock_19 ammo_required = 1, no energy_drain.
    CHECK( s_one.find( '1' ) != std::string::npos );

    const std::string s_three = gun.format_consumption_requirements(
                                    /*method=*/"", gun_mode_id( "DEFAULT" ), /*uses=*/3 );
    CHECK( s_three.find( '3' ) != std::string::npos );
}
