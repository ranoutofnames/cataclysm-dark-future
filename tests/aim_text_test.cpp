// CDF: 8-6a (U9 part 1). DDA ranged.cpp:4265-4266 (the switch-ammo hint gate) and :4388-4396
// (the player's Ammo row) as pure functions in src/ranged_text.cpp. DDA has no test for this text;
// every case here is new. Literals are read from the CDF item JSON noted beside each case.
#include <string>

#include "calendar.h"
#include "catch/catch.hpp"
#include "color.h"
#include "gun_mode.h"
#include "item.h"
#include "itype.h"
#include "pocket_data.h"
#include "pocket_type.h"
#include "ranged_text.h"
#include "type_id.h"
#include "vehicle/vehicle.h"
#include "itype_slots/islot_gun.h"

namespace {

// A hand-built multimag_display_pocket (what turret_data::multimag_display_state returns).
multimag_display_pocket make_dp( const std::string &pocket, const itype_id &ammo, int local_qty,
                                 int effective_qty, int per_use_qty, bool vehicle_bound )
{
    multimag_display_pocket dp;
    dp.pocket_id = pocket;
    dp.ammo_itype = ammo;
    dp.local_qty = local_qty;
    dp.effective_qty = effective_qty;
    dp.per_use_qty = per_use_qty;
    dp.vehicle_bound = vehicle_bound;
    return dp;
}

// A glock_19 with glockmag loaded with `rounds` 9mm through its MAGAZINE_WELL (0 = empty gun).
detached_ptr<item> glock_with_rounds( int rounds )
{
    detached_ptr<item> gun = item::spawn( itype_id( "glock_19" ) );
    if( rounds > 0 ) {
        detached_ptr<item> mag = item::spawn( itype_id( "glockmag" ) );
        mag->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, rounds ) );
        REQUIRE( gun->put_in( std::move( mag ), pocket_type::MAGAZINE_WELL ).success() );
    }
    return gun;
}

} // namespace

TEST_CASE( "aim_switch_ammo_hint_gate", "[aim_text][p3]" )
{
    // DDA ranged.cpp:4265-4266: hidden only for a loaded RELOAD_AND_SHOOT gun.
    CHECK( ranged_text::switch_ammo_hint_shown( false, 5 ) );
    CHECK( ranged_text::switch_ammo_hint_shown( false, 0 ) );
    CHECK_FALSE( ranged_text::switch_ammo_hint_shown( true, 5 ) );
    CHECK( ranged_text::switch_ammo_hint_shown( true, 0 ) );
}

TEST_CASE( "aim_ammo_row_loaded_glock", "[aim_text][p3]" )
{
    detached_ptr<item> gun = glock_with_rounds( 15 );
    const itype &ammo = itype_id( "9mm" ).obj();
    // CDF: glockmag pocket ammo_restriction { "9mm": 15 } (data/json/items/magazine/9mm.json:80-100).
    const std::string row = ranged_text::aim_ammo_row( gun->gun_current_mode(), false, ammo );
    CHECK( row.find( "(15/15)" ) != std::string::npos );
    CHECK( row.find( ammo.nname( 15 ) ) != std::string::npos );
}

TEST_CASE( "aim_ammo_row_favourite_star", "[aim_text][p3]" )
{
    // The star term is DDA's `is_ammo_container() && first_ammo().is_favorite`, computed by the
    // caller (ranged.cpp). No item in the data satisfies is_ammo_container() and first_ammo() at once
    // (checked with a loose-round MAGAZINE fixture), so the star is tested through the formatter's is_favorite input with the TEST_DATA
    // aim_star_fixture (one MAGAZINE pocket) as the gun.
    detached_ptr<item> gun = item::spawn( itype_id( "aim_star_fixture" ) );
    gun->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, 2 ), pocket_type::MAGAZINE );
    const itype &ammo = itype_id( "9mm" ).obj();
    gun_mode mode( "", gun.get(), 1, {} );

    CHECK( ranged_text::aim_ammo_row( mode, false, ammo ).find( " *" ) == std::string::npos );
    CHECK( ranged_text::aim_ammo_row( mode, true, ammo ).find( " *" ) != std::string::npos );
}

TEST_CASE( "aim_ammo_row_empty_glock", "[aim_text][p3]" )
{
    detached_ptr<item> gun = glock_with_rounds( 0 );
    const itype &ammo = itype_id( "9mm" ).obj();
    const std::string row = ranged_text::aim_ammo_row( gun->gun_current_mode(), false, ammo );
    CHECK( row.find( "Ammo: " ) == 0 );
    CHECK( row.find( '(' ) == std::string::npos );
}

TEST_CASE( "aim_ammo_row_integral_revolver", "[aim_text][p3]" )
{
    // sw_619 is a converted integral cylinder: one MAGAZINE pocket, clip_size and the pocket
    // restriction are both read from its JSON here (data/json/items/gun/38.json:103-145).
    detached_ptr<item> gun = item::spawn( itype_id( "sw_619" ) );
    const itype &def = itype_id( "sw_619" ).obj();
    const int clip = def.gun->clip;
    int capacity = -1;
    for( const pocket_data &pd : def.pockets ) {
        if( pd.type == pocket_type::MAGAZINE ) {
            capacity = pd.ammo_restriction.begin()->second;
        }
    }
    REQUIRE( clip == 7 );
    REQUIRE( capacity == clip );

    const itype_id round = gun->ammo_default();
    gun->put_in( item::spawn( round, calendar::turn, clip ), pocket_type::MAGAZINE );
    REQUIRE( gun->ammo_remaining() == clip );
    REQUIRE( gun->ammo_data() != nullptr );
    const itype &ammo = *gun->ammo_data();
    const std::string row = ranged_text::aim_ammo_row( gun->gun_current_mode(), false, ammo );
    CHECK( row.find( "(" + std::to_string( clip ) + "/" + std::to_string( capacity ) + ")" ) !=
           std::string::npos );
    CHECK( row.find( ammo.nname( clip ) ) != std::string::npos );
}

TEST_CASE( "aim_ammo_row_empty_integral_revolver", "[aim_text][p3]" )
{
    detached_ptr<item> gun = item::spawn( itype_id( "sw_619" ) );
    const itype &ammo = gun->ammo_default().obj();
    const std::string row = ranged_text::aim_ammo_row( gun->gun_current_mode(), false, ammo );
    CHECK( row.find( "Ammo: " ) == 0 );
    CHECK( row.find( '(' ) == std::string::npos );
    CHECK( row.find( ammo.nname( 1 ) ) != std::string::npos );
}

TEST_CASE( "aim_ammo_row_capacity_is_the_glock_magazine", "[aim_text][p3]" )
{
    // A partly loaded glock: the row reads 4 of the magazine's 15 rounds.
    detached_ptr<item> gun = glock_with_rounds( 4 );
    const itype &ammo = itype_id( "9mm" ).obj();
    const std::string row = ranged_text::aim_ammo_row( gun->gun_current_mode(), false, ammo );
    CHECK( row.find( "(4/15)" ) != std::string::npos );
}

TEST_CASE( "aim_ammo_row_capacity_is_the_typed_pocket", "[aim_text][p3]" )
{
    // aim_two_ammo_fixture (TEST_DATA items.json): one MAGAZINE pocket, ammo_restriction { 9mm: 20, 40: 8 }.
    // The row must read the capacity of the ammo shown (item::ammo_capacity( ammotype ), item_gun_tool_ammo.cpp:372),
    // not the untyped item capacity (item.cpp:9438 gives 0 for this tool). Literals are the fixture's JSON.
    {
        detached_ptr<item> gun = item::spawn( itype_id( "aim_two_ammo_fixture" ) );
        gun->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, 2 ), pocket_type::MAGAZINE );
        const itype &ammo = itype_id( "9mm" ).obj();
        gun_mode mode( "", gun.get(), 1, {} );
        CHECK( ranged_text::aim_ammo_row( mode, false, ammo ).find( "(2/20)" ) != std::string::npos );
    }
    {
        detached_ptr<item> gun = item::spawn( itype_id( "aim_two_ammo_fixture" ) );
        gun->put_in( item::spawn( itype_id( "40fmj" ), calendar::turn, 3 ), pocket_type::MAGAZINE );
        const itype &ammo = itype_id( "40fmj" ).obj();
        gun_mode mode( "", gun.get(), 1, {} );
        CHECK( ranged_text::aim_ammo_row( mode, false, ammo ).find( "(3/8)" ) != std::string::npos );
    }
}

// CDF: 8-6b (U9 part 2). DDA ranged.cpp:4362-4387, the manual multimag turret rows. DDA has no test
// for this text; every case is new. "battery" is the AMMO item (data/json/items/ammo.json:4-9):
// name "battery" / "batteries", colour yellow.
TEST_CASE( "turret_row_is_red_when_short_of_one_use", "[aim_text][p3]" )
{
    // R1: nothing in the pocket, one round needed.
    const auto row = ranged_text::turret_row( make_dp( "ammo", itype_id( "9mm" ), 0, 0, 1, false ),
                     c_cyan );
    CHECK( row.second == c_red );
    // One short of a use (4 of 5) is still short.
    CHECK( ranged_text::turret_row( make_dp( "power", itype_id( "battery" ), 0, 4, 5, true ),
                                    c_cyan ).second == c_red );
}

TEST_CASE( "turret_row_keeps_the_base_colour_when_one_use_is_there", "[aim_text][p3]" )
{
    // R2: plenty, and the boundary (exactly one use is not short; kills "<" to "<=").
    CHECK( ranged_text::turret_row( make_dp( "ammo", itype_id( "9mm" ), 14, 14, 1, false ),
                                    c_cyan ).second == c_cyan );
    CHECK( ranged_text::turret_row( make_dp( "power", itype_id( "battery" ), 0, 5, 5, true ),
                                    c_cyan ).second == c_cyan );
    CHECK( ranged_text::turret_row( make_dp( "ammo", itype_id( "9mm" ), 14, 14, 1, false ),
                                    c_light_gray ).second == c_light_gray );
}

TEST_CASE( "turret_row_text_for_a_vehicle_bound_pocket", "[aim_text][p3]" )
{
    // R3: the battery pocket, supplied by the vehicle.
    const itype &battery = itype_id( "battery" ).obj();
    REQUIRE( battery.color == c_yellow );
    const auto row = ranged_text::turret_row( make_dp( "power", itype_id( "battery" ), 0, 40, 5,
                     true ), c_light_gray );
    CHECK( row.first == "power: " + colorize( "batteries", c_yellow ) + " (vehicle 40, need 5)" );
    CHECK( row.second == c_light_gray );
}

TEST_CASE( "turret_row_text_for_a_local_pocket_shows_the_effective_quantity", "[aim_text][p3]" )
{
    // R4: not vehicle-bound; local_qty (3) differs from effective_qty (14) so the wrong field shows.
    const itype &ammo = itype_id( "9mm" ).obj();
    REQUIRE( ammo.color == c_yellow );
    const auto row = ranged_text::turret_row( make_dp( "ammo", itype_id( "9mm" ), 3, 14, 1, false ),
                     c_light_gray );
    CHECK( row.first == "ammo: " + colorize( ammo.nname( 14 ), c_yellow ) + " (14, need 1)" );
    CHECK( row.first.find( "vehicle" ) == std::string::npos );
}

TEST_CASE( "turret_row_names_the_pocket_when_the_ammo_is_null", "[aim_text][p3]" )
{
    // R5: no ammo type: the name is the pocket id, in light gray whatever the base colour is.
    const auto row = ranged_text::turret_row( make_dp( "power", itype_id::NULL_ID(), 0, 40, 5,
                     true ), c_cyan );
    CHECK( row.first == "power: " + colorize( "power", c_light_gray ) + " (vehicle 40, need 5)" );
    const auto local = ranged_text::turret_row( make_dp( "ammo", itype_id::NULL_ID(), 0, 0, 1,
                       false ), c_cyan );
    CHECK( local.first == "ammo: " + colorize( "ammo", c_light_gray ) + " (0, need 1)" );
    CHECK( local.second == c_red );
}

TEST_CASE( "turret_row_names_an_empty_pocket_in_the_singular", "[aim_text][p3]" )
{
    // R6 (DDA std::max( effective_qty, 1 )): an empty battery pocket reads "battery", not "batteries".
    const itype &battery = itype_id( "battery" ).obj();
    REQUIRE( battery.nname( 0 ) != battery.nname( 1 ) );
    const auto row = ranged_text::turret_row( make_dp( "power", itype_id( "battery" ), 0, 0, 5,
                     true ), c_light_gray );
    CHECK( row.first == "power: " + colorize( "battery", c_yellow ) + " (vehicle 0, need 5)" );
}
