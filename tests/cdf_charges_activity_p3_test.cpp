#include "activity_handlers.h"
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "distribution_grid.h"
#include "flag.h"
#include "item.h"
#include "item_pocket.h"
#include "itype.h"
#include "iuse_actor.h"
#include "map/map.h"
#include "map_helpers.h"
#include "overmap/overmap.h"
#include "overmap/overmapbuffer.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "reload/reload.h"
#include "ret_val.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"
#include "vehicle/vehicle.h"

#include <algorithm>
#include <string>
#include <utility>

// P3-4-c-H7: after 4-b2 the fuel/battery/rounds of a tool live in its pocket. The repair
// "hack" fake tools, the fireweapon actors and item_reload_option::qty must read that pocket.

namespace hack = activity_handlers::repair_activity_hack;

namespace {

// repair_item_actor::use stores its subtype at str_values[0] (iuse_actor.cpp:3127); the hack then
// appends the fake tool's id at str_values[1], which get_fake_tool reads.
auto make_repair_activity() -> player_activity {
    player_activity act( activity_id( "ACT_REPAIR_ITEM" ), 0 );
    act.str_values.emplace_back( "repair_metal" );
    return act;
}

// CDF: shishkebab_off has max_charges 250 (swords_and_blades.json:2038) and charges_per_use 1.
auto full_blade( const itype_id &id ) -> detached_ptr<item> {
    detached_ptr<item> blade = item::spawn( id );
    blade->ammo_set( blade->ammo_default(), blade->ammo_capacity() );
    REQUIRE( blade->ammo_remaining() == blade->ammo_capacity() );
    return blade;
}

auto fireweapon_actor( const item &it, const char *method ) -> const iuse_actor & {
    const iuse_actor *actor = it.type->use_methods.at( method ).get_actor_ptr();
    REQUIRE( actor != nullptr );
    return *actor;
}

// CDF: shishkebab_on sets auto_extinguish_chance 300 (swords_and_blades.json:2080), so one in 300
// active ticks would put the blade out during the test. Zero it for the duration and restore it.
struct auto_extinguish_guard {
    explicit auto_extinguish_guard( fireweapon_on_actor *a )
        : actor( a ), saved( a->auto_extinguish_chance ) {
        actor->auto_extinguish_chance = 0;
    }
    ~auto_extinguish_guard() {
        actor->auto_extinguish_chance = saved;
    }
    auto_extinguish_guard( const auto_extinguish_guard & ) = delete;
    auto_extinguish_guard &operator=( const auto_extinguish_guard & ) = delete;

    fireweapon_on_actor *actor;
    int saved;
};

} // namespace

// H1: S0010, activity_handlers.cpp:2554. CDF: soldering_iron takes a battery and has a
// medium_battery_cell well (data/json/items/tool/workshop.json:1074-1100, battery.json:208-231).
TEST_CASE( "fake_tool_from_vehicle_gets_battery_in_its_pocket", "[p3][charges][activity]" ) {
    clear_all_state();
    build_test_map( ter_id( "t_pavement" ) );
    map &m = get_map();

    constexpr tripoint_bub_ms vehicle_origin( 10, 10, 0 );
    vehicle *veh = m.add_vehicle( vproto_id( "car" ), vehicle_origin, 0_degrees, 0, 0, false );
    REQUIRE( veh != nullptr );
    veh->charge_battery( 100, false );
    REQUIRE( veh->fuel_left( itype_id( "battery" ) ) == 100 );

    const tripoint_bub_ms part_pos = veh->bub_part_location( 0 );
    REQUIRE( m.veh_at( part_pos ) );

    player_activity act = make_repair_activity();
    hack::patch_activity_for_vehicle( act, part_pos, *veh, 0, itype_id( "soldering_iron" ) );

    item *fake = hack::get_fake_tool( hack::hack_type_t::vehicle, act );
    REQUIRE( fake != nullptr );
    CHECK( fake->ammo_remaining() == 100 );
    CHECK( fake->charges == 0 );
}

// H2: S0012, activity_handlers.cpp:2595. The fake tool is billed by what it used, not by its
// direct `charges`.
TEST_CASE( "discharge_bills_what_the_tool_used", "[p3][charges][activity]" ) {
    clear_all_state();
    build_test_map( ter_id( "t_pavement" ) );
    map &m = get_map();

    constexpr tripoint_bub_ms vehicle_origin( 10, 10, 0 );
    vehicle *veh = m.add_vehicle( vproto_id( "car" ), vehicle_origin, 0_degrees, 0, 0, false );
    REQUIRE( veh != nullptr );
    veh->charge_battery( 100, false );
    REQUIRE( veh->fuel_left( itype_id( "battery" ) ) == 100 );

    const tripoint_bub_ms part_pos = veh->bub_part_location( 0 );
    REQUIRE( m.veh_at( part_pos ) );

    player_activity act = make_repair_activity();
    hack::patch_activity_for_vehicle( act, part_pos, *veh, 0, itype_id( "soldering_iron" ) );

    item *fake = hack::get_fake_tool( hack::hack_type_t::vehicle, act );
    REQUIRE( fake != nullptr );
    REQUIRE( fake->ammo_remaining() == 100 );

    CHECK( fake->ammo_consume( 10, part_pos, nullptr ) == 10 );
    REQUIRE( fake->ammo_remaining() == 90 );

    hack::discharge_real_power_source( hack::hack_type_t::vehicle, part_pos, *fake, 100 );
    CHECK( veh->fuel_left( itype_id( "battery" ) ) == 90 );
}

// H3: S0011 guard, activity_handlers.cpp:2576. fake_oven has no ammo type and max_charges 2500
// (data/json/items/fake.json:323-331), so ammo_set keeps writing direct `charges`.
TEST_CASE( "fake_tool_from_grid_keeps_charges", "[p3][charges][activity]" ) {
    clear_all_state();
    map &m = get_map();
    avatar &u = get_avatar();
    constexpr tripoint_bub_ms start_pos( 60, 60, 0 );
    const tripoint_abs_ms start_pos_abs( map_local_to_abs( m, start_pos ) );
    u.setpos( start_pos );
    clear_avatar();
    auto om = ACTIVE_OVERMAP_BUFFER.get_om_global( u.abs_omt_pos() );
    om.om->set_electric_grid_connections( om.local, {} );

    const tripoint_bub_ms furn_pos = start_pos + point_east;
    m.furn_set( start_pos + point( 10, 0 ), furn_str_id( "f_battery" ) );
    m.furn_set( furn_pos, furn_str_id( "f_oven" ) );
    distribution_grid_tracker &tracker = get_distribution_grid_tracker();
    distribution_grid &grid = tracker.grid_at( start_pos_abs + point( 10, 0 ) );
    REQUIRE( !grid.empty() );
    REQUIRE( &grid == &tracker.grid_at( start_pos_abs + point_east ) );
    grid.mod_resource( 10 );
    REQUIRE( grid.get_resource( true ) == 10 );

    player_activity act = make_repair_activity();
    hack::patch_activity_for_furniture( act, furn_pos, itype_id( "fake_oven" ) );

    item *fake = hack::get_fake_tool( hack::hack_type_t::furniture, act );
    REQUIRE( fake != nullptr );
    CHECK( fake->ammo_remaining() == 10 );
    CHECK( fake->charges == 10 );
}

// H4: S0418, iuse_actor.cpp:2374. CDF: success_chance 4 (swords_and_blades.json:2046) only
// affects use(); can_use reads the pocket through ammo_sufficient.
TEST_CASE( "fireweapon_off_can_use_follows_the_pocket", "[p3][charges][activity]" ) {
    clear_all_state();
    avatar &u = get_avatar();
    detached_ptr<item> blade_owner = full_blade( itype_id( "shishkebab_off" ) );
    item &blade = *blade_owner;
    const iuse_actor &actor = fireweapon_actor( blade, "fireweapon_off" );

    CHECK( actor.can_use( u, blade, false, u.bub_pos() ).success() );

    blade.ammo_unset();
    REQUIRE( blade.ammo_remaining() == 0 );
    const ret_val<bool> r = actor.can_use( u, blade, false, u.bub_pos() );
    CHECK_FALSE( r.success() );
    CHECK( r.str().find( "doesn't have enough charges" ) != std::string::npos );
}

// H5: S0417, iuse_actor.cpp:2338. A full blade lights; an empty one keeps the CDF lacks-fuel
// message and stays off. Messages is stubbed in the test binary (tests/fake_messages.cpp), so the
// early return is observed through the move cost.
TEST_CASE( "fireweapon_off_lights_a_loaded_blade", "[p3][charges][activity]" ) {
    clear_all_state();
    avatar &u = get_avatar();
    const itype_id off_id( "shishkebab_off" );
    const itype_id on_id( "shishkebab_on" );
    detached_ptr<item> blade_owner = full_blade( off_id );
    item &blade = *blade_owner;
    const auto *actor = dynamic_cast<const fireweapon_off_actor *>(
                            &fireweapon_actor( blade, "fireweapon_off" ) );
    REQUIRE( actor != nullptr );

    SECTION( "a full blade catches fire" ) {
        u.moves = 100;
        // CDF: success_chance 4 makes one igniter pull a coin flip; retry until it catches.
        for( int i = 0; i < 100 && blade.typeId() == off_id; ++i ) {
            actor->use( u, blade, false, u.bub_pos() );
        }
        CHECK( blade.typeId() == on_id );
        // the fuel check passed, so the actor reached p.moves -= moves
        CHECK( u.moves < 100 );
    }

    SECTION( "an empty blade stays off" ) {
        blade.ammo_unset();
        REQUIRE( blade.ammo_remaining() == 0 );
        u.moves = 100;
        actor->use( u, blade, false, u.bub_pos() );
        CHECK( blade.typeId() == off_id );
        // CDF: CDF data still sets lacks_fuel_message (swords_and_blades.json:2047).
        CHECK_FALSE( actor->lacks_fuel_message.empty() );
        CHECK( u.moves == 100 );
    }
}

// H6: S0419, iuse_actor.cpp:2407. With fuel, an active tick keeps the blade lit and a voluntary
// use puts it out; an empty tick is left to item::process_tool, which owns the revert
// (item.cpp:11599-11668).
TEST_CASE( "fireweapon_on_stays_lit_with_fuel", "[p3][charges][activity]" ) {
    clear_all_state();
    avatar &u = get_avatar();
    const itype_id on_id( "shishkebab_on" );
    const itype_id off_id( "shishkebab_off" );
    detached_ptr<item> blade_owner = full_blade( on_id );
    item &blade = *blade_owner;
    const auto *const_actor = dynamic_cast<const fireweapon_on_actor *>(
                                  &fireweapon_actor( blade, "fireweapon_on" ) );
    REQUIRE( const_actor != nullptr );
    auto *actor = const_cast<fireweapon_on_actor *>( const_actor );
    auto_extinguish_guard guard( actor );

    SECTION( "an active tick with fuel keeps the blade lit" ) {
        blade.activate();
        REQUIRE( blade.is_active() );
        actor->use( u, blade, true, u.bub_pos() );
        CHECK( blade.typeId() == on_id );
        CHECK( blade.is_active() );
    }

    SECTION( "a voluntary use with fuel puts the blade out" ) {
        blade.activate();
        actor->use( u, blade, false, u.bub_pos() );
        CHECK( blade.typeId() == off_id );
        CHECK_FALSE( blade.is_active() );
    }

    SECTION( "an empty tick is left to process_tool" ) {
        blade.ammo_unset();
        REQUIRE( blade.ammo_remaining() == 0 );
        blade.activate();
        actor->use( u, blade, true, u.bub_pos() );
        // CDF: the actor only sets extinguish = false here; process_tool does the revert.
        CHECK( blade.typeId() == on_id );
        CHECK( blade.is_active() );
    }
}

// H7: S0499, reload/reload.cpp:187. The rounds of a loaded magazine are not reloadable rounds:
// one magazine is one reload option. CDF: the empty m4a1 holds an empty stanag30 so its
// ammo_capacity() is 30; without it both the old and new qty clamp to 1 and the test cannot bite
// (TASK.md stop condition 2).
TEST_CASE( "reload_option_qty_of_a_loaded_magazine_is_one", "[p3][charges][activity]" ) {
    clear_all_state();
    avatar &u = get_avatar();

    detached_ptr<item> gun_owner = item::spawn( itype_id( "m4a1" ) );
    item &gun = *gun_owner;
    detached_ptr<item> empty_mag = item::spawn( itype_id( "stanag30" ) );
    empty_mag->ammo_unset();
    REQUIRE( empty_mag->ammo_remaining() == 0 );
    REQUIRE( gun.put_in( std::move( empty_mag ), pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( gun.ammo_capacity() == 30 );
    REQUIRE( gun.ammo_remaining() == 0 );

    detached_ptr<item> mag_owner = item::spawn( itype_id( "stanag30" ) );
    mag_owner->ammo_unset();
    REQUIRE( mag_owner->put_in( item::spawn( itype_id( "556" ), calendar::turn, 30 ),
                                pocket_type::MAGAZINE ).success() );
    item &magazine = *mag_owner;
    REQUIRE( magazine.ammo_remaining() == 30 );

    item_reload_option opt( &u, &gun, &gun, magazine );
    // CDF: a large val, so the available_ammo line decides the result (TASK.md stop condition 2).
    opt.qty( 1000 );
    CHECK( opt.qty() == 1 );
}

// H8: S0499, reload/reload.cpp:186-188. A speedloader is one item but qty must be its rounds,
// capped by the gun's free chambers. CDF: 38_speedloader holds 7 (magazine/38.json:17-22) and
// sw_619 has clip_size 7 (gun/38.json:98).
TEST_CASE( "reload_option_qty_of_a_speedloader_is_its_rounds", "[p3][charges][activity]" ) {
    clear_all_state();
    avatar &u = get_avatar();
    const time_point bday = calendar::start_of_cataclysm;

    detached_ptr<item> loader_owner = item::spawn( itype_id( "38_speedloader" ), bday, false );
    loader_owner->ammo_unset();
    REQUIRE( loader_owner->ammo_remaining() == 0 );
    REQUIRE( loader_owner->put_in( item::spawn( itype_id( "38_special" ), bday, 7 ),
                                   pocket_type::MAGAZINE ).success() );
    item &loader = *loader_owner;
    REQUIRE( loader.has_flag( flag_SPEEDLOADER ) );
    const int rounds = loader.ammo_remaining();
    REQUIRE( rounds == 7 );

    detached_ptr<item> gun_owner = item::spawn( itype_id( "sw_619" ), bday, false );
    item &gun = *gun_owner;
    REQUIRE( gun.ammo_remaining() == 0 );
    REQUIRE( gun.ammo_capacity() == 7 );

    SECTION( "an empty cylinder takes every round the loader has" ) {
        item_reload_option opt( &u, &gun, &gun, loader );
        opt.qty( 1000 );
        CHECK( opt.qty() == rounds );
    }

    SECTION( "a partly loaded cylinder caps the qty at its room" ) {
        // CDF: the sw_619 is a legacy direct-charges gun (no MAGAZINE pocket, magazine_integral),
        // so its loaded rounds are `charges` (item_gun_tool_ammo.cpp:165-180).
        gun.charges = 2;
        REQUIRE( gun.ammo_remaining() == 2 );
        const int room = gun.ammo_capacity() - gun.ammo_remaining();
        REQUIRE( room == 5 );
        REQUIRE( room < rounds );
        item_reload_option opt( &u, &gun, &gun, loader );
        opt.qty( 1000 );
        CHECK( opt.qty() == room );
    }
}
