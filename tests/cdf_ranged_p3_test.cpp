#include "avatar.h"
#include "avatar_action.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "debug.h"
#include "fault.h"
#include "flag.h"
#include "gun_mode.h"
#include "item.h"
#include "itype.h"
#include "map/map.h"
#include "map_helpers.h"
#include "messages.h"
#include "npc.h"
#include "player_helpers.h"
#include "ranged.h"
#include "rng.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"
#include "itype_slots/islot_gun.h"

#include <optional>
#include <string>
#include <vector>

namespace {

// CDF: P3-8-8a. Spawns `id` with a full stanag30 of 556 and the requested damage on the
// gun and on its magazine. The anonymous namespace of cdf_item_ammo_consume_p3_test.cpp is
// not shared, so this repeats the loaded_m4a1 pattern there (tests/cdf_item_ammo_consume_p3_test.cpp:43).
auto loaded_gun( const itype_id &id, int gun_damage, int mag_damage ) -> detached_ptr<item> {
    detached_ptr<item> gun = item::spawn( id );
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    mag->put_in( item::spawn( itype_id( "556" ), calendar::turn, 30 ) );
    gun->put_in( std::move( mag ) );
    gun->set_damage( gun_damage );
    REQUIRE( gun->damage() == gun_damage );
    REQUIRE( gun->magazine_current() != nullptr );
    gun->magazine_current()->set_damage( mag_damage );
    REQUIRE( gun->magazine_current()->damage() == mag_damage );
    return gun;
}

// One seed for the whole run, and dirt is reset before every shot, so the dirt misfire
// (0 at dirt 0) and the fouling branch stay out of the way and the jam model is the only
// per-shot randomness left. CDF: P3-8-8b: a jam now leaves fault_fail_to_feed on the gun, which
// refuses every later shot, so the fault is cleared before each shot unless keep_fault is set.
auto count_failures( item &gun, int shots, bool keep_fault = false ) -> int {
    rng_set_engine_seed( 4242424242 );
    int failures = 0;
    for( int shot = 0; shot < shots; ++shot ) {
        if( !keep_fault ) {
            gun.faults.erase( fault_id( "fault_fail_to_feed" ) );
        }
        gun.set_var( "dirt", 0 );
        if( !ranged::handle_gun_damage( get_player_character(), gun ) ) {
            ++failures;
        }
    }
    return failures;
}

} // namespace

TEST_CASE( "jam_magazine_damage_4_always_jams", "[p3][ranged]" ) {
    clear_avatar();
    // Magazine damage level 4 is 2.5 x mag_jam_mult (>= 1 for every magazine in CDF data).
    detached_ptr<item> gun = loaded_gun( itype_id( "m4a1" ), 0, 4000 );
    CHECK( count_failures( *gun, 300 ) == 300 );
}

TEST_CASE( "jam_gun_mult_scales_the_damage_table", "[p3][ranged]" ) {
    clear_avatar();
    // Gun damage level 0 is 0.0005 x gun_jam_mult, so 2000 makes every shot jam.
    detached_ptr<item> gun = loaded_gun( itype_id( "test_jam_gun_mult_high" ), 0, 0 );
    CHECK( count_failures( *gun, 300 ) == 300 );
}

TEST_CASE( "jam_NEVER_JAMS_never_jams", "[p3][ranged]" ) {
    clear_avatar();
    // Both tables are far above 1 here, so only the flag can stop the failures.
    detached_ptr<item> gun = loaded_gun( itype_id( "test_jam_gun_mult_high" ), 4000, 4000 );
    gun->set_flag( flag_id( "NEVER_JAMS" ) );
    CHECK( count_failures( *gun, 300 ) == 0 );
}

TEST_CASE( "jam_gun_mult_zero_has_no_table_jam", "[p3][ranged]" ) {
    clear_avatar();
    // gun_jam_mult 0 zeroes the gun table; only the durability malfunction (1/384 per shot)
    // and the pristine-magazine table (0.00027 per shot) remain.
    detached_ptr<item> gun = loaded_gun( itype_id( "test_jam_gun_mult_zero" ), 4000, 0 );
    CHECK( count_failures( *gun, 300 ) <= 8 );
}

TEST_CASE( "jam_gun_damage_2_follows_the_table", "[p3][ranged]" ) {
    clear_avatar();
    // Gun damage level 2 is 0.15 per shot; the durability malfunction adds 1/384 and the
    // pristine magazine adds 0.00027, so the expected count over 2000 shots is about 305.
    detached_ptr<item> gun = loaded_gun( itype_id( "m4a1" ), 2000, 0 );
    const int failures = count_failures( *gun, 2000 );
    CHECK( failures > 230 );
    CHECK( failures < 380 );
}

TEST_CASE( "gun_jam_mult_loads_with_default_one", "[p3][ranged]" ) {
    clear_avatar();
    CHECK( itype_id( "m4a1" ).obj().gun->gun_jam_mult == Approx( 1.0 ) );
    CHECK( itype_id( "test_jam_gun_mult_high" ).obj().gun->gun_jam_mult == Approx( 2000.0 ) );
    CHECK( itype_id( "test_jam_gun_mult_zero" ).obj().gun->gun_jam_mult == Approx( 0.0 ) );
}

namespace
{

// CDF: DDA multimag_test.cpp 2113-2121 (gun, 9mm round and charged heavy cell built as value items);
// detached_ptr<item>, round and charge counts as parameters. 0 means "do not load that pocket".
auto make_integral_multimag_gun( int rounds, int battery_charges ) -> detached_ptr<item>
{
    detached_ptr<item> gun = item::spawn( itype_id( "test_multimag_gun_integral_ammo" ) );
    REQUIRE( gun->uses_firing_requirements() );
    // CDF: the 8-8a jam model is random per shot; these cases are about consumption
    gun->set_flag( flag_id( "NEVER_JAMS" ) );
    if( rounds > 0 ) {
        REQUIRE( gun->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, rounds ),
                              pocket_type::MAGAZINE ).success() );
    }
    if( battery_charges > 0 ) {
        detached_ptr<item> cell = item::spawn( itype_id( "heavy_battery_cell" ) );
        cell->ammo_set( itype_id( "battery" ), battery_charges );
        REQUIRE( gun->put_in( std::move( cell ), pocket_type::MAGAZINE_WELL ).success() );
    }
    return gun;
}

// CDF: DDA wields with dummy.set_wielded_item( gun ) (multimag_test.cpp 2123); CDF wield() takes the
// detached item and returns it when the wield is refused (precedent tests/cdf_item_reload_p3_test.cpp 664).
// The avatar stands at a fixed tile and the target is five tiles east.
auto wield_for_firing( avatar &who, detached_ptr<item> &&gun ) -> item &
{
    who.setpos( tripoint_bub_ms( 60, 60, 0 ) );
    REQUIRE( !who.wield( std::move( gun ) ) );
    REQUIRE( who.is_armed() );
    return who.primary_weapon();
}

} // namespace

// CDF-only (lookup 8-2 section 7): the fire-loop consume branch. DDA ranged.cpp 1298-1306.
TEST_CASE( "cdf_ranged_multimag_fire_consumes_one_shot_per_pocket", "[p3][ranged]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    item &gun = wield_for_firing( you, make_integral_multimag_gun( 1, 100 ) );
    REQUIRE( gun.ammo_remaining_in_pocket( "ammo" ) == 1 );
    REQUIRE( gun.ammo_remaining_in_pocket( "power" ) == 100 );

    int fired = 0;
    const std::string dmsg = capture_debugmsg_during( [&]() {
        fired = ranged::fire_gun( you, you.bub_pos() + tripoint_rel_ms( 5, 0, 0 ), 1, gun, nullptr );
    } );
    CAPTURE( dmsg );
    // the old BN branch calls ammo_consume, whose multimag guard debugmsgs (item_gun_tool_ammo.cpp 975)
    CHECK( dmsg.empty() );
    CHECK( fired == 1 );
    // one DEFAULT-mode shot: ammo qty 1 and power qty 5 (items.json 1873)
    CHECK( gun.ammo_remaining_in_pocket( "ammo" ) == 0 );
    CHECK( gun.ammo_remaining_in_pocket( "power" ) == 95 );
}

// CDF-only (lookup 8-2 section 7): the burst clamp is shots_remaining, DDA ranged.cpp 1127-1128.
TEST_CASE( "cdf_ranged_multimag_fire_clamp_by_shots_remaining", "[p3][ranged]" )
{
    clear_all_state();
    avatar &you = get_avatar();

    SECTION( "three requested, one round loaded: one shot is fired" ) {
        item &gun = wield_for_firing( you, make_integral_multimag_gun( 1, 100 ) );
        REQUIRE( gun.shots_remaining( get_map(), &you ) == 1 );
        int fired = 0;
        const std::string dmsg = capture_debugmsg_during( [&]() {
            fired = ranged::fire_gun( you, you.bub_pos() + tripoint_rel_ms( 5, 0, 0 ), 3, gun, nullptr );
        } );
        CAPTURE( dmsg );
        CHECK( dmsg.empty() );
        CHECK( fired == 1 );
        CHECK( gun.ammo_remaining_in_pocket( "ammo" ) == 0 );
        CHECK( gun.ammo_remaining_in_pocket( "power" ) == 95 );
    }

    SECTION( "three requested, battery for one shot only: one shot is fired" ) {
        // 1 round (the pocket capacity) but only 5 charges: the power pocket limits the burst to one shot,
        // a second 5-charge shot would need 10
        item &gun = wield_for_firing( you, make_integral_multimag_gun( 1, 5 ) );
        REQUIRE( gun.shots_remaining( get_map(), &you ) == 1 );
        int fired = 0;
        const std::string dmsg = capture_debugmsg_during( [&]() {
            fired = ranged::fire_gun( you, you.bub_pos() + tripoint_rel_ms( 5, 0, 0 ), 3, gun, nullptr );
        } );
        CAPTURE( dmsg );
        CHECK( dmsg.empty() );
        CHECK( fired == 1 );
        CHECK( gun.ammo_remaining_in_pocket( "ammo" ) == 0 );
        CHECK( gun.ammo_remaining_in_pocket( "power" ) == 0 );
    }
}

// CDF-only (lookup 8-2 section 7): DDA ranged.cpp 1071-1075.
TEST_CASE( "cdf_ranged_multimag_npc_pretend_fire_consumes", "[p3][ranged]" )
{
    clear_all_state();
    standard_npc who( "carrier" );

    SECTION( "one shot consumes one round and five charges" ) {
        detached_ptr<item> held = make_integral_multimag_gun( 1, 100 );
        item &gun = *held;
        const std::string dmsg = capture_debugmsg_during( [&]() {
            who.pretend_fire( &who, 1, gun );
        } );
        CAPTURE( dmsg );
        CHECK( dmsg.empty() );
        CHECK( gun.ammo_remaining_in_pocket( "ammo" ) == 0 );
        CHECK( gun.ammo_remaining_in_pocket( "power" ) == 95 );
    }

    SECTION( "a second shot with an empty pocket stops the burst" ) {
        detached_ptr<item> held = make_integral_multimag_gun( 1, 100 );
        item &gun = *held;
        const std::string dmsg = capture_debugmsg_during( [&]() {
            who.pretend_fire( &who, 2, gun );
        } );
        CAPTURE( dmsg );
        // the second consume_one_shot returns 0: DDA's message, and no second drain of the battery
        CHECK( dmsg.find( "Unexpected shortage of ammo whilst firing" ) != std::string::npos );
        CHECK( gun.ammo_remaining_in_pocket( "ammo" ) == 0 );
        CHECK( gun.ammo_remaining_in_pocket( "power" ) == 95 );
    }
}

// DDA tests/multimag_test.cpp 2107-2133, name kept (D11); tag [p3][ranged] instead of [multimag][fire].
// Multimag gun with integral MAGAZINE ammo pocket trips the single-mag "incompatible ammunition" gate in
// fire_wielded_weapon (DDA commit 387e158bb1b); the gate must skip multimag guns.
// CDF: GUARD (no red run): CDF's gate compares ammo_data() (the identity round since 3-4c-c) with
// ammo_types(), so it does not block this fixture before the edit either. Messages are stubs in the test
// build (tests/fake_messages.cpp 20), so DDA's message loop below is vacuous here; the aim-activity CHECK
// is the CDF addition that can fail.
TEST_CASE( "multimag_fire_skips_incompatible_ammo_gate", "[p3][ranged]" )
{
    clear_all_state();
    avatar &dummy = get_avatar();
    wield_for_firing( dummy, make_integral_multimag_gun( 1, 100 ) );

    Messages::clear_messages();
    avatar_action::fire_wielded_weapon( dummy );

    for( const auto &msg : Messages::recent_messages( 0 ) ) {
        INFO( "message: " << msg.second );
        CHECK( msg.second.find( "incompatible ammunition" ) == std::string::npos );
    }
    // CDF: the gate returns before assign_activity, so the aim activity proves it did not trip
    CHECK( dummy.has_activity( activity_id( "ACT_AIM" ) ) );
}

// DDA tests/multimag_test.cpp 2263-2288, name kept (D11); tag [p3][ranged] instead of [multimag][fire].
// Firing a multimag gun with only the power source loaded must abort cleanly (shots_remaining = 0)
// without emitting "invalid recipe id" or ammo-shortage debugmsgs anywhere in the fire path.
TEST_CASE( "multimag_fire_without_projectile_does_not_crash", "[p3][ranged]" )
{
    clear_all_state();
    avatar &dummy = get_avatar();

    SECTION( "battery only" ) {
        // CDF: GUARD, see the red-first notes of the brief (the DDA CHECKs are kept as written; DDA builds
        // the gun and cell as value items, here detached_ptr via make_integral_multimag_gun)
        item &gun = wield_for_firing( dummy, make_integral_multimag_gun( 0, 100 ) );
        int fired = -1;
        const std::string dmsg = capture_debugmsg_during( [&]() {
            fired = ranged::fire_gun( dummy, dummy.bub_pos() + tripoint_rel_ms( 5, 0, 0 ), 1, gun,
                                      nullptr );
        } );
        INFO( "debugmsg: " << dmsg );
        CHECK( dmsg.find( "invalid recipe id" ) == std::string::npos );
        CHECK( dmsg.find( "Unexpected shortage of ammo" ) == std::string::npos );
        // CDF: nothing is fired and nothing is drained
        CHECK( fired == 0 );
        CHECK( gun.ammo_remaining_in_pocket( "power" ) == 100 );
    }

    SECTION( "CDF: slug only, no battery, clamps to zero shots" ) {
        // CDF-only: the clamp (DDA 1127-1128) reaches shots_remaining = 0 because the power pocket is
        // empty while ammo_remaining() is 1, so the old BN clamp (1 / 1) would fire and hit the guards
        item &gun = wield_for_firing( dummy, make_integral_multimag_gun( 1, 0 ) );
        REQUIRE( gun.shots_remaining( get_map(), &dummy ) == 0 );
        int fired = -1;
        const std::string dmsg = capture_debugmsg_during( [&]() {
            fired = ranged::fire_gun( dummy, dummy.bub_pos() + tripoint_rel_ms( 5, 0, 0 ), 1, gun,
                                      nullptr );
        } );
        CAPTURE( dmsg );
        CHECK( dmsg.find( "invalid recipe id" ) == std::string::npos );
        CHECK( dmsg.find( "Unexpected shortage of ammo" ) == std::string::npos );
        CHECK( dmsg.find( "called on multimag item" ) == std::string::npos );
        CHECK( fired == 0 );
        CHECK( gun.ammo_remaining_in_pocket( "ammo" ) == 1 );
    }
}

// CDF-only guard (pass before and after the edit): a non-multimag gun keeps the BN clamps and the
// ammo_consume + UPS branch (the `else` of Part C). Reuses loaded_gun from the 8-8a block above.
TEST_CASE( "cdf_ranged_legacy_gun_fire_path_unchanged", "[p3][ranged]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    detached_ptr<item> held = loaded_gun( itype_id( "m4a1" ), 0, 0 );
    held->set_flag( flag_id( "NEVER_JAMS" ) );
    item &gun = wield_for_firing( you, std::move( held ) );
    REQUIRE( !gun.uses_firing_requirements() );
    REQUIRE( gun.ammo_remaining() == 30 );

    int fired = 0;
    const std::string dmsg = capture_debugmsg_during( [&]() {
        fired = ranged::fire_gun( you, you.bub_pos() + tripoint_rel_ms( 5, 0, 0 ), 1, gun, nullptr );
    } );
    CAPTURE( dmsg );
    CHECK( dmsg.empty() );
    CHECK( fired == 1 );
    CHECK( gun.ammo_remaining() == 29 );
}

namespace
{

// CDF: the outcome of one ranged::gunmode_checks_weapon call; a named struct (no std::pair), the messages
// also joined with a newline so a CHECK can search them without indexing.
struct weapon_check {
    bool usable = false;
    std::vector<std::string> messages;
    std::string all;
};

auto run_weapon_check( avatar &you, item &gun ) -> weapon_check
{
    weapon_check out;
    out.usable = ranged::gunmode_checks_weapon( you, get_map(), out.messages, gun.gun_current_mode() );
    for( const std::string &msg : out.messages ) {
        out.all += msg + "\n";
    }
    return out;
}

// CDF: test_energy_gun (8-1d fixture): energy_drain 5 kJ, one battery well, no ammo type.
auto make_energy_gun() -> detached_ptr<item>
{
    detached_ptr<item> gun = item::spawn( itype_id( "test_energy_gun" ) );
    REQUIRE( gun->get_gun_energy_drain() == 5_kJ );
    REQUIRE( !gun->uses_firing_requirements() );
    return gun;
}

} // namespace

// CDF-only (no DDA test; DDA ranged.cpp 4622-4626, commit 91f07695e8a): the multimag message of the weapon checks.
TEST_CASE( "cdf_ranged_multimag_gunmode_weapon_message", "[p3][ranged]" )
{
    clear_all_state();
    avatar &you = get_avatar();

    SECTION( "slug only: the message names the firing requirements" ) {
        detached_ptr<item> held = make_integral_multimag_gun( 1, 0 );
        REQUIRE( held->shots_remaining( get_map(), &you ) == 0 );
        REQUIRE( held->ammo_remaining() == 1 );
        const weapon_check res = run_weapon_check( you, *held );
        CAPTURE( res.all );
        CHECK_FALSE( res.usable );
        CHECK( res.messages.size() == 1 );
        // CDF: [UNSURE] tail derived by reading (format_entry_name falls back to the pocket id for the empty well);
        // the red run cannot settle it (the old text differs), the green run does: a literal fixed to the actual
        // output is allowed only for the name part and is named in the report with before and after.
        // Settled by the green run: the empty `power` well is named by the battery it takes (before: " + 5 power.",
        // after: " + 5 batteries.", built from the battery nname( 5 ) like the battery-only section below).
        CHECK( res.all.find( "needs to fire: 1 " + itype_id( "9mm" )->nname( 1 ) + " + 5 " +
                             itype_id( "battery" )->nname( 5 ) + "." ) != std::string::npos );
        CHECK( res.all.find( "charges to fire" ) == std::string::npos );
    }

    SECTION( "battery only: the message names the firing requirements" ) {
        detached_ptr<item> held = make_integral_multimag_gun( 0, 100 );
        REQUIRE( held->shots_remaining( get_map(), &you ) == 0 );
        REQUIRE( held->ammo_remaining() > 0 );
        const weapon_check res = run_weapon_check( you, *held );
        CAPTURE( res.all );
        CHECK_FALSE( res.usable );
        CHECK( res.messages.size() == 1 );
        // CDF: [UNSURE] tail, same note as above (the empty ammo pocket falls back to its id).
        CHECK( res.all.find( "needs to fire: 1 ammo + 5 " + itype_id( "battery" )->nname( 5 ) + "." ) !=
               std::string::npos );
        CHECK( res.all.find( "charges to fire" ) == std::string::npos );
    }

    SECTION( "both pockets loaded: usable, no message" ) {
        // GUARD (passes before and after): the checks say nothing about a gun that can fire
        detached_ptr<item> held = make_integral_multimag_gun( 1, 100 );
        REQUIRE( held->shots_remaining( get_map(), &you ) == 1 );
        const weapon_check res = run_weapon_check( you, *held );
        CAPTURE( res.all );
        CHECK( res.usable );
        CHECK( res.messages.empty() );
    }

    SECTION( "nothing loaded: the unchanged empty message" ) {
        // GUARD: only a gun with nothing in any pocket says "is empty" (DDA 4620-4621 stays the first arm)
        detached_ptr<item> held = make_integral_multimag_gun( 0, 0 );
        REQUIRE( held->ammo_remaining() == 0 );
        const weapon_check res = run_weapon_check( you, *held );
        CAPTURE( res.all );
        CHECK_FALSE( res.usable );
        CHECK( res.messages.size() == 1 );
        CHECK( res.all.find( "is empty!" ) != std::string::npos );
        CHECK( res.all.find( "needs to fire" ) == std::string::npos );
    }
}

// CDF-only (no DDA test; DDA ranged.cpp 4634-4659, commit 6768e73568d): the energy checks of the weapon checks.
// The mech wording (DDA 4646-4648) needs a mounted mech monster and has no test (Ledger row B).
TEST_CASE( "cdf_ranged_gunmode_weapon_energy_checks", "[p3][ranged]" )
{
    clear_all_state();
    avatar &you = get_avatar();

    SECTION( "USE_UPS energy gun with an empty well names the UPS requirement" ) {
        detached_ptr<item> held = make_energy_gun();
        held->set_flag( flag_id( "USE_UPS" ) );
        REQUIRE( held->energy_remaining( &you ) == 0_kJ );
        const weapon_check res = run_weapon_check( you, *held );
        CAPTURE( res.all );
        CHECK_FALSE( res.usable );
        // the empty message (ammo block, unchanged) and the energy message (new)
        CHECK( res.messages.size() == 2 );
        CHECK( res.all.find( "is empty!" ) != std::string::npos );
        CHECK( res.all.find( "You need a UPS with at least 5 kJ to fire the" ) != std::string::npos );
    }

    SECTION( "USES_BIONIC_POWER below the drain names the bionic requirement" ) {
        detached_ptr<item> held = make_energy_gun();
        held->set_flag( flag_id( "USES_BIONIC_POWER" ) );
        you.set_max_power_level( 100_kJ );
        you.set_power_level( 4_kJ );
        REQUIRE( held->energy_remaining( &you ) == 4_kJ );
        const weapon_check res = run_weapon_check( you, *held );
        CAPTURE( res.all );
        CHECK_FALSE( res.usable );
        CHECK( res.messages.size() == 2 );
        CHECK( res.all.find( "You need at least 5 kJ of bionic power to fire the" ) != std::string::npos );
        CHECK( res.all.find( "UPS" ) == std::string::npos );
    }

    SECTION( "USES_BIONIC_POWER at the drain is usable" ) {
        // GUARD: 5 kJ against a 5 kJ drain passes; the DDA test is `<`, so a `<=` port fails this section
        detached_ptr<item> held = make_energy_gun();
        held->set_flag( flag_id( "USES_BIONIC_POWER" ) );
        you.set_max_power_level( 100_kJ );
        you.set_power_level( 5_kJ );
        REQUIRE( held->energy_remaining( &you ) == 5_kJ );
        const weapon_check res = run_weapon_check( you, *held );
        CAPTURE( res.all );
        CHECK( res.usable );
        CHECK( res.messages.empty() );
    }

    SECTION( "no power flag and an empty well: unusable, no energy message" ) {
        // GUARD: DDA sets result = false and prints nothing when the gun has neither flag (4644-4657)
        detached_ptr<item> held = make_energy_gun();
        const weapon_check res = run_weapon_check( you, *held );
        CAPTURE( res.all );
        CHECK_FALSE( res.usable );
        CHECK( res.messages.size() == 1 );
        CHECK( res.all.find( "is empty!" ) != std::string::npos );
    }

    SECTION( "a 30 kJ battery loaded: usable" ) {
        // GUARD: [UNSURE] settled by the run; 30 kJ / 5 kJ is 6 shots, the energy block passes
        detached_ptr<item> held = make_energy_gun();
        detached_ptr<item> cell = item::spawn( itype_id( "battery_test" ) );
        REQUIRE( cell->mod_energy( 30_kJ ) == 0_J );
        REQUIRE( held->put_in( std::move( cell ), pocket_type::MAGAZINE_WELL ).success() );
        REQUIRE( held->energy_remaining( &you ) == 30_kJ );
        const weapon_check res = run_weapon_check( you, *held );
        CAPTURE( res.all );
        CHECK( res.usable );
        CHECK( res.messages.empty() );
    }

    SECTION( "a UPS-charge BN gun keeps its own UPS message and gets no energy message" ) {
        // GUARD: plasma_gun has ups_charges 15 and no energy_drain (plasma.json 3); the BN UPS block stays
        detached_ptr<item> held = item::spawn( itype_id( "plasma_gun" ) );
        held->ammo_set( itype_id( "plasma" ), 30 );
        REQUIRE( held->get_gun_ups_drain() == 15 );
        REQUIRE( held->get_gun_energy_drain() == 0_kJ );
        REQUIRE( held->ammo_sufficient( &you ) );
        const weapon_check res = run_weapon_check( you, *held );
        CAPTURE( res.all );
        CHECK_FALSE( res.usable );
        CHECK( res.messages.size() == 1 );
        CHECK( res.all.find( "You need a UPS with at least 15 charges or an advanced UPS with at least 7 charges to fire the" ) !=
               std::string::npos );
        CHECK( res.all.find( " kJ " ) == std::string::npos );
    }

    SECTION( "a loaded legacy gun: usable, no message" ) {
        // GUARD: a BN gun without energy_drain or ups_charges is untouched by the new block
        detached_ptr<item> held = loaded_gun( itype_id( "m4a1" ), 0, 0 );
        REQUIRE( held->get_gun_energy_drain() == 0_kJ );
        const weapon_check res = run_weapon_check( you, *held );
        CAPTURE( res.all );
        CHECK( res.usable );
        CHECK( res.messages.empty() );
    }
}

namespace
{

// CDF: the outcome of one ranged::fire_gun call; a named struct (no std::pair).
struct burst_result {
    int fired = -1;
    std::string dmsg;
};

// CDF: test_energy_gun (8-1d fixture: energy_drain 5 kJ, one battery well, no ammo type) with a battery_test
// cell holding `cell_kj` kJ; 0 means no cell. NEVER_JAMS: the 8-8a jam model is random per shot and these
// cases are about billing. If `1_kJ * cell_kj` does not compile, use
// units::from_kilojoule( static_cast<std::int64_t>( cell_kj ) ) (the only allowed compile fix).
auto make_energy_fire_gun( int cell_kj ) -> detached_ptr<item>
{
    detached_ptr<item> gun = item::spawn( itype_id( "test_energy_gun" ) );
    REQUIRE( gun->get_gun_energy_drain() == 5_kJ );
    REQUIRE( !gun->uses_firing_requirements() );
    REQUIRE( gun->ammo_required() == 0 );
    gun->set_flag( flag_id( "NEVER_JAMS" ) );
    if( cell_kj > 0 ) {
        detached_ptr<item> cell = item::spawn( itype_id( "battery_test" ) );
        REQUIRE( cell->mod_energy( 1_kJ * cell_kj ) == 0_J );
        REQUIRE( gun->put_in( std::move( cell ), pocket_type::MAGAZINE_WELL ).success() );
    }
    return gun;
}

auto fire_burst( avatar &you, item &gun, int max_shots ) -> burst_result
{
    burst_result out;
    out.dmsg = capture_debugmsg_during( [&]() {
        out.fired = ranged::fire_gun( you, you.bub_pos() + tripoint_rel_ms( 5, 0, 0 ), max_shots, gun,
                                      nullptr );
    } );
    return out;
}

} // namespace

// CDF-only (no DDA test; DDA ranged.cpp 1313-1322 commit 6768e73568d, clamp 1127-1128): the fire loop bills
// get_gun_energy_drain() per shot and the burst is clamped by the energy left.
TEST_CASE( "cdf_ranged_energy_gun_fire_bills_energy", "[p3][ranged]" )
{
    clear_all_state();
    avatar &you = get_avatar();

    SECTION( "one shot drains the energy_drain from the battery" ) {
        item &gun = wield_for_firing( you, make_energy_fire_gun( 12 ) );
        REQUIRE( gun.energy_remaining( &you ) == 12_kJ );
        const burst_result res = fire_burst( you, gun, 1 );
        CAPTURE( res.dmsg );
        CHECK( res.dmsg.find( "Unexpected shortage" ) == std::string::npos );
        CHECK( res.fired == 1 );
        CHECK( gun.energy_remaining( &you ) == 7_kJ );
    }

    SECTION( "a burst of three is clamped to the two shots 12 kJ pays" ) {
        item &gun = wield_for_firing( you, make_energy_fire_gun( 12 ) );
        REQUIRE( gun.energy_remaining( &you ) == 12_kJ );
        const burst_result res = fire_burst( you, gun, 3 );
        CAPTURE( res.dmsg );
        CHECK( res.dmsg.find( "Unexpected shortage" ) == std::string::npos );
        CHECK( res.fired == 2 );
        CHECK( gun.energy_remaining( &you ) == 2_kJ );
    }

    SECTION( "exactly enough energy for the whole burst" ) {
        item &gun = wield_for_firing( you, make_energy_fire_gun( 15 ) );
        REQUIRE( gun.energy_remaining( &you ) == 15_kJ );
        const burst_result res = fire_burst( you, gun, 3 );
        CAPTURE( res.dmsg );
        CHECK( res.dmsg.find( "Unexpected shortage" ) == std::string::npos );
        CHECK( res.fired == 3 );
        CHECK( gun.energy_remaining( &you ) == 0_kJ );
    }

    SECTION( "no battery loaded: nothing is fired" ) {
        item &gun = wield_for_firing( you, make_energy_fire_gun( 0 ) );
        REQUIRE( gun.energy_remaining( &you ) == 0_kJ );
        const burst_result res = fire_burst( you, gun, 1 );
        CAPTURE( res.dmsg );
        // the clamp reaches 0 before the loop: no shot and no shortage message from the loop
        CHECK( res.dmsg.find( "Unexpected shortage" ) == std::string::npos );
        CHECK( res.fired == 0 );
    }

    SECTION( "less than one shot of energy: nothing is fired and nothing is drained" ) {
        item &gun = wield_for_firing( you, make_energy_fire_gun( 4 ) );
        REQUIRE( gun.energy_remaining( &you ) == 4_kJ );
        const burst_result res = fire_burst( you, gun, 1 );
        CAPTURE( res.dmsg );
        CHECK( res.dmsg.find( "Unexpected shortage" ) == std::string::npos );
        CHECK( res.fired == 0 );
        CHECK( gun.energy_remaining( &you ) == 4_kJ );
    }

    SECTION( "gunmod multiplier and modifier raise the cost per shot" ) {
        detached_ptr<item> held = make_energy_fire_gun( 30 );
        REQUIRE( held->put_in( item::spawn( itype_id( "test_energy_gunmod" ) ),
                               pocket_type::MOD ).success() );
        // 5 kJ * 2.0 + 1 kJ
        REQUIRE( held->get_gun_energy_drain() == 11_kJ );
        item &gun = wield_for_firing( you, std::move( held ) );
        REQUIRE( gun.energy_remaining( &you ) == 30_kJ );
        const burst_result res = fire_burst( you, gun, 3 );
        CAPTURE( res.dmsg );
        CHECK( res.dmsg.find( "Unexpected shortage" ) == std::string::npos );
        // 30 kJ / 11 kJ truncates to two shots, 22 kJ billed
        CHECK( res.fired == 2 );
        CHECK( gun.energy_remaining( &you ) == 8_kJ );
    }

    SECTION( "USES_BIONIC_POWER bills the carrier's power" ) {
        detached_ptr<item> held = make_energy_fire_gun( 0 );
        held->set_flag( flag_id( "USES_BIONIC_POWER" ) );
        item &gun = wield_for_firing( you, std::move( held ) );
        you.set_max_power_level( 100_kJ );
        you.set_power_level( 12_kJ );
        REQUIRE( gun.energy_remaining( &you ) == 12_kJ );
        const burst_result res = fire_burst( you, gun, 3 );
        CAPTURE( res.dmsg );
        CHECK( res.dmsg.find( "Unexpected shortage" ) == std::string::npos );
        CHECK( res.fired == 2 );
        CHECK( you.get_power_level() == 2_kJ );
    }

    SECTION( "GUARD VEHICLE flag: no clamp and no billing" ) {
        // GUARD (passes before and after): DDA skips the energy clamp (1127) and the energy billing (1313-1314) for a
        // VEHICLE gun (turrets drain the vehicle battery elsewhere). [UNSURE] whether an avatar can fire a
        // VEHICLE-flagged gun cleanly; this is the ONLY section the implementer may delete, and only if it
        // crashes or logs an unrelated debugmsg in the Step 1b run: say so in the report with the log line.
        detached_ptr<item> held = make_energy_fire_gun( 0 );
        held->set_flag( flag_id( "VEHICLE" ) );
        item &gun = wield_for_firing( you, std::move( held ) );
        REQUIRE( gun.energy_remaining( &you ) == 0_kJ );
        const burst_result res = fire_burst( you, gun, 1 );
        CAPTURE( res.dmsg );
        CHECK( res.dmsg.find( "Unexpected shortage" ) == std::string::npos );
        CHECK( res.fired == 1 );
    }
}

// CDF-only guard (passes before and after the edit): a BN gun keeps the BN ammo clamp over a burst and its
// ammo_consume + UPS branch; energy_consume( 0 ) bills nothing and logs nothing for it.
TEST_CASE( "cdf_ranged_legacy_gun_burst_clamp_unchanged", "[p3][ranged]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    // CDF: the loaded_gun shape of the 8-8a block (m4a1 with a stanag30), but with 2 rounds instead of 30
    detached_ptr<item> held = item::spawn( itype_id( "m4a1" ) );
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    mag->put_in( item::spawn( itype_id( "556" ), calendar::turn, 2 ) );
    held->put_in( std::move( mag ) );
    held->set_flag( flag_id( "NEVER_JAMS" ) );
    item &gun = wield_for_firing( you, std::move( held ) );
    REQUIRE( !gun.uses_firing_requirements() );
    REQUIRE( gun.get_gun_energy_drain() == 0_kJ );
    REQUIRE( gun.ammo_remaining() == 2 );

    const burst_result res = fire_burst( you, gun, 5 );
    CAPTURE( res.dmsg );
    CHECK( res.dmsg.find( "Unexpected shortage" ) == std::string::npos );
    // the BN clamp ammo_left / ammo_required: 2 rounds, 2 shots
    CHECK( res.fired == 2 );
    CHECK( gun.ammo_remaining() == 0 );
}

// CDF-only guard: magazine_current() returns a BATTERY cell in a power well (8-1d-0) and a BATTERY item has no
// `magazine` slot; handle_gun_damage must not read one (it crashed before the null guard). NEVER_JAMS is NOT set.
TEST_CASE( "cdf_ranged_handle_gun_damage_battery_cell_does_not_crash", "[p3][ranged]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    detached_ptr<item> held = make_energy_fire_gun( 12 );
    held->unset_flag( flag_id( "NEVER_JAMS" ) );
    item &gun = wield_for_firing( you, std::move( held ) );
    REQUIRE( !gun.has_flag( flag_id( "NEVER_JAMS" ) ) );
    REQUIRE( gun.magazine_current() != nullptr );
    REQUIRE( gun.magazine_current()->type->magazine == nullptr );
    for( int shot = 0; shot < 20; ++shot ) {
        gun.set_var( "dirt", 0 );
        // the return value is random (the jam model); reaching the next line without a crash is the check
        static_cast<void>( ranged::handle_gun_damage( you, gun ) );
    }
    SUCCEED();
}

// CDF: P3-8-8b. The jam arm sets fault_fail_to_feed and the gate refuses the next shots.
// 4000 on the magazine is level 4: every shot that reaches the jam arm jams.
TEST_CASE( "jam_sets_the_fail_to_feed_fault", "[p3][ranged]" ) {
    clear_avatar();
    const fault_id fail_to_feed( "fault_fail_to_feed" );
    detached_ptr<item> gun = loaded_gun( itype_id( "m4a1" ), 0, 4000 );
    REQUIRE( gun->faults_potential().contains( fail_to_feed ) );
    rng_set_engine_seed( 4242424242 );
    int with_fault = 0;
    int returned_true = 0;
    for( int shot = 0; shot < 300; ++shot ) {
        gun->faults.erase( fail_to_feed );
        gun->set_var( "dirt", 0 );
        if( ranged::handle_gun_damage( get_player_character(), *gun ) ) {
            ++returned_true;
        }
        if( gun->faults.contains( fail_to_feed ) ) {
            ++with_fault;
        }
    }
    CHECK( returned_true == 0 );
    // the rest are the durability malfunction that precedes the jam arm (at most 1 in 192)
    CHECK( with_fault >= 280 );
}

TEST_CASE( "fail_to_feed_blocks_the_next_shots", "[p3][ranged]" ) {
    clear_avatar();
    const fault_id fail_to_feed( "fault_fail_to_feed" );
    detached_ptr<item> gun = loaded_gun( itype_id( "m4a1" ), 0, 0 );
    // negative fixture: the same gun without the fault mostly fires
    CHECK( count_failures( *gun, 300 ) < 30 );
    gun->faults.insert( fail_to_feed );
    CHECK( count_failures( *gun, 300, true ) == 300 );
}

TEST_CASE( "jam_without_the_potential_leaves_no_fault", "[p3][ranged]" ) {
    clear_avatar();
    const fault_id fail_to_feed( "fault_fail_to_feed" );
    detached_ptr<item> gun = loaded_gun( itype_id( "test_jam_gun_no_fail_to_feed" ), 0, 4000 );
    REQUIRE( !gun->faults_potential().contains( fail_to_feed ) );
    rng_set_engine_seed( 4242424242 );
    int failures = 0;
    int with_fault = 0;
    for( int shot = 0; shot < 300; ++shot ) {
        gun->set_var( "dirt", 0 );
        if( !ranged::handle_gun_damage( get_player_character(), *gun ) ) {
            ++failures;
        }
        if( gun->faults.contains( fail_to_feed ) ) {
            ++with_fault;
        }
    }
    CHECK( failures == 300 );
    CHECK( with_fault == 0 );
}

TEST_CASE( "fail_to_feed_fault_data", "[p3][ranged]" ) {
    const fault_id fail_to_feed( "fault_fail_to_feed" );
    REQUIRE( fail_to_feed.is_valid() );
    detached_ptr<item> semi = item::spawn( itype_id( "m4a1" ) );
    CHECK( semi->faults_potential().contains( fail_to_feed ) );
    // CDF: marlin_9a copies rifle_manual (data/json/items/gun/22.json), which sets its own fault list
    detached_ptr<item> manual = item::spawn( itype_id( "marlin_9a" ) );
    CHECK( !manual->faults_potential().contains( fail_to_feed ) );
    const auto &mends = fail_to_feed.obj().mending_methods();
    CHECK( mends.count( "mend_fault_fail_to_feed_manual_feed" ) == 1 );
}
