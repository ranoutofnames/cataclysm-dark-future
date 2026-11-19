// DDA source: tests/multimag_test.cpp (DDA commit b4b6225), cases
// gunmod_supplies_per_mode_firing_requirements (DDA:1415-1450, hidden-DEFAULT parts) and
// gunmod_hide_modes_global_kill_switch (DDA:1472-1495), plus CDF loader and reselect cases
// for islot_gunmod::hide_modes (DDA itype.h:1040, item_factory.cpp:3879,
// item_gun_tool_ammo.cpp:3177, item_container.cpp:172).

#include <map>
#include <set>

#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "gun_mode.h"
#include "item.h"
#include "itype.h"
#include "map/map.h"
#include "map_helpers.h"
#include "pocket_type.h"
#include "type_id.h"
#include "itype_slots/islot_gunmod.h"

static const gun_mode_id gun_mode_BURST( "BURST" );
static const gun_mode_id gun_mode_DEFAULT( "DEFAULT" );
static const gun_mode_id gun_mode_EXTRA( "EXTRA" );
static const gun_mode_id gun_mode_REGULATED( "REGULATED" );

static const itype_id itype_p3_multimag_gun_consume( "p3_multimag_gun_consume" );
static const itype_id itype_test_multimag_add_extra_mode_mod( "test_multimag_add_extra_mode_mod" );
static const itype_id itype_test_multimag_hide_all_mod( "test_multimag_hide_all_mod" );
static const itype_id itype_test_multimag_hide_extra_mode_mod( "test_multimag_hide_extra_mode_mod" );
static const itype_id itype_test_multimag_regulated_mod( "test_multimag_regulated_mod" );

namespace
{
// DDA tests/multimag_test.cpp:1267-1277.
// CDF: test_multimag_gun_consume has explicit pocket_data, which suppresses the generated
// MOD pocket, so the p3_ copy with a mod bay is used (data/mods/TEST_DATA/multimag_p3.json);
// item's move constructor is deleted, so the result is copied out.
item make_multimag_consume_gun( int bb_count = 15, int batt = 100 )
{
    item gun( itype_p3_multimag_gun_consume );
    item bb_mag( itype_id( "glockmag" ) );
    bb_mag.put_in( item( itype_id( "9mm" ), calendar::turn, bb_count ), pocket_type::MAGAZINE );
    item batt_mag( itype_id( "heavy_battery_cell" ) );
    batt_mag.put_in( item( itype_id( "battery" ), calendar::turn, batt ),
                     pocket_type::MAGAZINE );
    REQUIRE( gun.put_in( bb_mag, pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( gun.put_in( batt_mag, pocket_type::MAGAZINE_WELL ).success() );
    return item( gun );
}
} // namespace

TEST_CASE( "gunmod_hide_modes_loads_from_json", "[p3][gunmod][multimag]" )
{
    const auto hidden = []( const itype_id & id ) {
        const item it( id );
        const itype *t = it.type;
        REQUIRE( t != nullptr );
        REQUIRE( t->gunmod );
        return t->gunmod->hide_modes;
    };
    CHECK( hidden( itype_test_multimag_regulated_mod ) == std::set<gun_mode_id> { gun_mode_DEFAULT } );
    CHECK( hidden( itype_test_multimag_hide_all_mod ) ==
           std::set<gun_mode_id> { gun_mode_DEFAULT, gun_mode_BURST } );
    CHECK( hidden( itype_test_multimag_hide_extra_mode_mod ) == std::set<gun_mode_id> { gun_mode_EXTRA } );
    CHECK( hidden( itype_test_multimag_add_extra_mode_mod ).empty() );
}

TEST_CASE( "gunmod_supplies_per_mode_firing_requirements", "[p3][gunmod][multimag][consume][mod]" )
{
    clear_map();
    map &here = get_map();
    const tripoint_bub_ms pos = tripoint_bub_ms::zero();

    item gun = make_multimag_consume_gun( 15, 100 );
    REQUIRE( gun.put_in( item( itype_test_multimag_regulated_mod ),
                         pocket_type::MOD ).success() );

    GIVEN( "a gun mod that adds a REGULATED mode with its own per-pocket cost" ) {
        THEN( "the added mode appears and DEFAULT is hidden" ) {
            const std::map<gun_mode_id, gun_mode> modes = gun.gun_all_modes();
            CHECK( modes.count( gun_mode_REGULATED ) == 1 );
            CHECK( modes.count( gun_mode_DEFAULT ) == 0 );
        }
        WHEN( "fired in REGULATED mode" ) {
            const int got = gun.consume_shots( gun_mode_REGULATED, 3, here, pos, nullptr );
            THEN( "the mod's firing_requirements drive consumption, not DEFAULT" ) {
                CHECK( got == 3 );
                // 15 - 3*1
                CHECK( gun.ammo_remaining_in_pocket( "ammo" ) == 12 );
                // 100 - 3*1 (host DEFAULT would have drawn 5/shot)
                CHECK( gun.ammo_remaining_in_pocket( "power" ) == 97 );
            }
        }
        WHEN( "the hidden DEFAULT mode id is fired directly" ) {
            const int got = gun.consume_shots( gun_mode_DEFAULT, 3, here, pos, nullptr );
            THEN( "it is blocked and bills nothing, not the host DEFAULT cost" ) {
                CHECK( got == 0 );
                CHECK( gun.ammo_remaining_in_pocket( "ammo" ) == 15 );
                CHECK( gun.ammo_remaining_in_pocket( "power" ) == 100 );
            }
        }
    }
}

TEST_CASE( "gunmod_hide_modes_global_kill_switch", "[p3][gunmod][multimag][consume][mod]" )
{
    clear_map();
    map &here = get_map();
    const tripoint_bub_ms pos = tripoint_bub_ms::zero();

    SECTION( "hiding every mode leaves the gun unfireable, not crashing" ) {
        item gun = make_multimag_consume_gun( 15, 100 );
        REQUIRE( gun.put_in( item( itype_test_multimag_hide_all_mod ),
                             pocket_type::MOD ).success() );
        CHECK( gun.gun_all_modes().empty() );
        // No live mode: shot resolution refuses, consuming nothing.
        CHECK( gun.shots_remaining( here, nullptr ) == 0 );
        CHECK( gun.consume_shots( gun_mode_DEFAULT, 1, here, pos, nullptr ) == 0 );
        CHECK( gun.ammo_remaining_in_pocket( "ammo" ) == 15 );
        CHECK( gun.ammo_remaining_in_pocket( "power" ) == 100 );
    }

    SECTION( "one mod can hide a mode another mod added" ) {
        item gun = make_multimag_consume_gun( 15, 100 );
        REQUIRE( gun.put_in( item( itype_test_multimag_add_extra_mode_mod ),
                             pocket_type::MOD ).success() );
        REQUIRE( gun.gun_all_modes().count( gun_mode_EXTRA ) == 1 );
        REQUIRE( gun.put_in( item( itype_test_multimag_hide_extra_mode_mod ),
                             pocket_type::MOD ).success() );
        CHECK( gun.gun_all_modes().count( gun_mode_EXTRA ) == 0 );
    }
}

// CDF-only: DDA item_container.cpp:172-179 reselects a surviving mode when a gunmod hides
// the selected one. DDA has no test for it.
TEST_CASE( "gunmod_hide_modes_reselects_surviving_mode", "[p3][gunmod][multimag]" )
{
    item gun = make_multimag_consume_gun( 15, 100 );
    REQUIRE( gun.gun_get_mode_id() == gun_mode_DEFAULT );
    REQUIRE( gun.put_in( item( itype_test_multimag_regulated_mod ),
                         pocket_type::MOD ).success() );
    const std::map<gun_mode_id, gun_mode> modes = gun.gun_all_modes();
    REQUIRE( modes.count( gun_mode_DEFAULT ) == 0 );
    CHECK( modes.count( gun.gun_get_mode_id() ) == 1 );
    CHECK( gun.gun_get_mode_id() != gun_mode_DEFAULT );
}
