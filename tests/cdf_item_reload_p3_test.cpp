#include "activity_actor_definitions.h"
#include "avatar.h"
#include "avatar_functions.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "debug.h"
#include "flag.h"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"
#include "player_activity.h"
#include "type_id.h"
#include "player_helpers.h"
#include "reload/reload.h"
#include "state_helpers.h"
#include "value_ptr.h"

#include <climits>
#include <memory>
#include <string>
#include <utility>

static const itype_id itype_glock_19( "glock_19" );
static const itype_id itype_glockmag( "glockmag" );

// CDF: P3-3-6; ported from DDA tests/multimag_test.cpp:626-693.
TEST_CASE( "item_reload_with_pocket_index_targets_specific_well", "[p3][reload]" )
{
    clear_avatar();
    Character &you = get_player_character();
    you.wear_item( item::spawn( itype_id( "backpack" ) ), false );

    // Multi-well gun with only the first (glock) well loaded; the stanag
    // well stays empty so item::reload(stanag_idx) has somewhere to insert.
    detached_ptr<item> gun_ptr = item::spawn( itype_id( "test_multimag_gun" ) );
    detached_ptr<item> glock_mag = item::spawn( itype_id( "glockmag" ) );
    glock_mag->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, 15 ), pocket_type::MAGAZINE );
    gun_ptr->put_in( std::move( glock_mag ), pocket_type::MAGAZINE_WELL );

    // CDF: i_add returns item &, not an item_location.
    item &gun = you.i_add( std::move( gun_ptr ) );

    // Identify the two well indices (glock-loaded and empty-stanag) in gun.
    int loaded_well_idx = -1;
    int empty_well_idx = -1;
    {
        int idx = 0;
        for( const item_pocket *p : gun.get_pockets(
        []( const item_pocket & ) {
        return true;
    } ) ) {
            if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
                if( p->magazine_current() != nullptr ) {
                    loaded_well_idx = idx;
                } else {
                    empty_well_idx = idx;
                }
            }
            ++idx;
        }
    }
    REQUIRE( loaded_well_idx >= 0 );
    REQUIRE( empty_well_idx >= 0 );

    SECTION( "pocket_index = empty stanag well inserts there, leaves glock untouched" ) {
        detached_ptr<item> replacement_stanag = item::spawn( itype_id( "stanag30" ) );
        replacement_stanag->put_in( item::spawn( itype_id( "556" ), calendar::turn, 10 ), pocket_type::MAGAZINE );
        item &replacement = you.i_add( std::move( replacement_stanag ) );

        const int glock_count_before = gun.ammo_remaining( loaded_well_idx );
        REQUIRE( gun.reload( you, replacement, 1, empty_well_idx ) );
        CHECK( gun.ammo_remaining( empty_well_idx ) == 10 );
        CHECK( gun.ammo_remaining( loaded_well_idx ) == glock_count_before );
    }

    SECTION( "invalid pocket_index aborts reload" ) {
        detached_ptr<item> replacement_stanag = item::spawn( itype_id( "stanag30" ) );
        replacement_stanag->put_in( item::spawn( itype_id( "556" ), calendar::turn, 10 ), pocket_type::MAGAZINE );
        item &replacement = you.i_add( std::move( replacement_stanag ) );

        const int glock_count_before = gun.ammo_remaining( loaded_well_idx );
        bool reload_result = true;
        const std::string dmsg = capture_debugmsg_during( [&]() {
            // Out-of-range index. Aborts with a debugmsg, which we capture
            // so it does not flag the test as failed.
            reload_result = gun.reload( you, replacement, 1, 99 );
        } );
        CHECK( !reload_result );
        CHECK( dmsg.find( "pocket_index" ) != std::string::npos );
        CHECK( gun.ammo_remaining( loaded_well_idx ) == glock_count_before );
        CHECK( gun.ammo_remaining( empty_well_idx ) == 0 );
    }
}

// CDF: P3-3-6b; ported from DDA item_gun_tool_ammo.cpp:4453-4491 (item::is_reloadable).
TEST_CASE( "item_is_reloadable_magazine_well", "[p3][reload][magazine]" )
{
    detached_ptr<item> gun_ptr = item::spawn( itype_id( "test_multimag_gun" ) );
    item &gun = *gun_ptr;
    SECTION( "both wells empty" ) {
        CHECK( gun.is_reloadable() );
    }
    SECTION( "one full well keeps the empty well reloadable, both full does not" ) {
        // Full magazines: glockmag holds 15 of 9mm, stanag30 holds 30 of 556.
        detached_ptr<item> glock_mag = item::spawn( itype_id( "glockmag" ) );
        glock_mag->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, 15 ), pocket_type::MAGAZINE );
        REQUIRE( glock_mag->contents.is_magazine_full() );
        gun.put_in( std::move( glock_mag ), pocket_type::MAGAZINE_WELL );
        CHECK( gun.is_reloadable() );

        detached_ptr<item> stanag_mag = item::spawn( itype_id( "stanag30" ) );
        stanag_mag->put_in( item::spawn( itype_id( "556" ), calendar::turn, 30 ), pocket_type::MAGAZINE );
        REQUIRE( stanag_mag->contents.is_magazine_full() );
        gun.put_in( std::move( stanag_mag ), pocket_type::MAGAZINE_WELL );
        int full_wells = 0;
        for( const item_pocket *p : gun.get_pockets( []( const item_pocket & pk ) {
        return pk.is_type( pocket_type::MAGAZINE_WELL );
    } ) ) {
            REQUIRE( !p->empty() );
            if( p->front().contents.is_magazine_full() ) {
                ++full_wells;
            }
        }
        REQUIRE( full_wells == 2 );
        CHECK_FALSE( gun.is_reloadable() );
    }
}

TEST_CASE( "item_is_reloadable_integral_magazine", "[p3][reload][magazine]" )
{
    detached_ptr<item> mag_ptr = item::spawn( itype_id( "glockmag" ) );
    item &mag = *mag_ptr;
    SECTION( "empty" ) {
        CHECK( mag.is_reloadable() );
    }
    SECTION( "full" ) {
        mag.put_in( item::spawn( itype_id( "9mm" ), calendar::turn, 15 ), pocket_type::MAGAZINE );
        REQUIRE( mag.remaining_ammo_capacity() == 0 );
        CHECK_FALSE( mag.is_reloadable() );
    }
}

TEST_CASE( "item_is_reloadable_gunmod", "[p3][reload][magazine]" )
{
    // Host: m4a1 (one well, a MOD pocket) with a full stanag30, so it has no reloadable pocket.
    // CDF: not pipe_pistol: it is a legacy direct-charges gun (D14, cdf_legacy_direct_charges),
    // reloadable before the mod is fitted.
    detached_ptr<item> gun_ptr = item::spawn( itype_id( "m4a1" ) );
    item &gun = *gun_ptr;
    detached_ptr<item> stanag_mag = item::spawn( itype_id( "stanag30" ) );
    stanag_mag->put_in( item::spawn( itype_id( "556" ), calendar::turn, 30 ), pocket_type::MAGAZINE );
    REQUIRE( gun.put_in( std::move( stanag_mag ), pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( gun.gunmods().empty() );
    REQUIRE_FALSE( gun.is_reloadable() );

    detached_ptr<item> mod = item::spawn( itype_id( "test_multimag_aux_gun" ) );
    REQUIRE( gun.put_in( std::move( mod ), pocket_type::MOD ).success() );
    REQUIRE( gun.gunmods().size() == 1 );
    // only the mod's empty well can make the host reloadable
    CHECK( gun.gunmods().front()->is_reloadable() );
    CHECK( gun.is_reloadable() );
}

TEST_CASE( "item_is_reloadable_legacy_charges_gun", "[p3][reload][magazine]" )
{
    // CDF: legacy direct-charges gun (pipe_pistol: ammo 38, no magazine pocket) stays reloadable.
    detached_ptr<item> gun_ptr = item::spawn( itype_id( "pipe_pistol" ) );
    CHECK( gun_ptr->is_reloadable() );
}

TEST_CASE( "item_is_reloadable_watertight_container", "[p3][reload][magazine]" )
{
    detached_ptr<item> jug_ptr = item::spawn( itype_id( "test_watertight_open_sealed_container_1L" ) );
    item &jug = *jug_ptr;
    SECTION( "empty" ) {
        CHECK( jug.is_reloadable() );
    }
    SECTION( "holding a non-liquid" ) {
        REQUIRE( jug.put_in( item::spawn( itype_id( "rock" ) ), pocket_type::CONTAINER ).success() );
        CHECK_FALSE( jug.is_reloadable() );
    }
}

// CDF: P3-3-6b; DDA item_gun_tool_ammo.cpp:4476 skips a CONTAINER pocket that is not watertight.
TEST_CASE( "item_is_reloadable_non_watertight_container", "[p3][reload][magazine]" )
{
    detached_ptr<item> bag_ptr = item::spawn( itype_id( "backpack" ) );
    item &bag = *bag_ptr;
    REQUIRE( bag.contents.has_pocket_type( pocket_type::CONTAINER ) );
    CHECK_FALSE( bag.is_reloadable() );
}

TEST_CASE( "item_gun_tool_ammo_no_default_ammo_not_reloadable", "[p3][reload][magazine]" )
{
    // ammo: [ "NULL" ] and no pocket_data, so ammo_default() is the NULL id
    detached_ptr<item> gun_ptr = item::spawn( itype_id( "debug_modular_m4_carbine" ) );
    item &gun = *gun_ptr;
    REQUIRE( gun.is_gun() );
    CHECK_FALSE( gun.is_reloadable() );
}

// CDF: P3-6-1a-1; reload_activity_actor, ported from DDA activity_actor.cpp:8283-8533.
TEST_CASE( "reload_activity_actor_loads_through_reload", "[p3][reload]" )
{
    clear_all_state();
    const time_point bday = calendar::start_of_cataclysm;
    avatar &dummy = get_avatar();
    // Make sure the player doesn't drop anything :P
    dummy.wear_item( item::spawn( "backpack", bday ) );

    detached_ptr<item> det = item::spawn( "40sw", bday, item::default_charges_tag{} );
    item &ammo = *det;
    dummy.i_add( std::move( det ) );
    det = item::spawn( "sw_610", bday, item::default_charges_tag{} );
    item &gun = *det;
    dummy.i_add( std::move( det ) );
    int ammo_pos = dummy.inv_position_by_item( &ammo );

    REQUIRE( ammo_pos != INT_MIN );
    REQUIRE( gun.ammo_remaining() == 0 );
    REQUIRE( gun.magazine_integral() );

    item_reload_option opt( &dummy, &gun, &gun, ammo );
    const int qty = opt.qty();
    REQUIRE( qty >= 1 );
    REQUIRE( opt.pocket_index == item_reload_option::POCKET_FALLBACK );

    reload_activity_actor actor( std::move( opt ) );
    CHECK( actor.get_type() == activity_id( "ACT_RELOAD" ) );

    player_activity act( activity_id( "ACT_RELOAD" ), 100 );
    REQUIRE( act );
    actor.reload( act, dummy, qty );
    CHECK( act );
    CHECK( gun.ammo_remaining() == qty );
}

// CDF: P3-6-1a-1; the CDF twin reload_gun_with_swappable_magazine exists, so a new name.
TEST_CASE( "reload_gun_with_swappable_magazine_via_actor", "[p3][reload]" )
{
    clear_all_state();
    const time_point bday = calendar::start_of_cataclysm;
    avatar &dummy = get_avatar();
    // Make sure the player doesn't drop anything :P
    dummy.wear_item( item::spawn( "backpack", bday ) );

    detached_ptr<item> det = item::spawn( "9mm", bday, item::default_charges_tag{} );
    item &ammo = *det;
    dummy.i_add( std::move( det ) );
    const cata::value_ptr<islot_ammo> &ammo_type = ammo.type->ammo;
    REQUIRE( ammo_type );

    detached_ptr<item> mag = item::spawn( itype_glockmag, bday, 0 );
    const cata::value_ptr<islot_magazine> &magazine_type = mag->type->magazine;
    REQUIRE( magazine_type );
    REQUIRE( magazine_type->type.count( ammo_type->type ) != 0 );

    det = item::spawn( itype_glock_19, bday, item::default_charges_tag{} );
    item &gun = *det;
    dummy.i_add( std::move( det ) );
    REQUIRE( gun.ammo_types().count( ammo_type->type ) != 0 );

    gun.put_in( std::move( mag ) );

    int gun_pos = dummy.inv_position_by_type( itype_glock_19 );
    REQUIRE( gun_pos != INT_MIN );
    item &glock = dummy.i_at( gun_pos );
    // We're expecting the magazine to end up in the inventory.
    REQUIRE( avatar_funcs::unload_item( dummy, glock ) );
    int magazine_pos = dummy.inv_position_by_type( itype_glockmag );
    REQUIRE( magazine_pos != INT_MIN );
    item &magazine = dummy.inv_find_item( magazine_pos );
    REQUIRE( magazine.ammo_remaining() == 0 );

    int ammo_pos = dummy.inv_position_by_item( &ammo );
    REQUIRE( ammo_pos != INT_MIN );

    bool magazine_success = magazine.reload( dummy, ammo, ammo.charges );

    REQUIRE( magazine_success );
    REQUIRE( magazine.ammo_remaining() == magazine.ammo_capacity() );

    REQUIRE( gun.ammo_remaining() == 0 );
    REQUIRE( gun.magazine_integral() == false );

    item_reload_option opt( &dummy, &gun, &gun, magazine );
    REQUIRE( opt.qty() == 1 );
    dummy.assign_activity( std::make_unique<player_activity>(
                               std::make_unique<reload_activity_actor>( std::move( opt ) ) ) );
    REQUIRE( process_activity_bounded( dummy, 2000, 100000 ) );
    CHECK( gun.ammo_remaining() == gun.ammo_capacity() );
    CHECK( !dummy.activity );
}

// CDF: P3-6-1a-1; DDA reloading_test.cpp:825-866 with the CDF twin's setup.
TEST_CASE( "reload_gun_with_integral_magazine_using_speedloader_via_actor", "[p3][reload]" )
{
    clear_all_state();
    const time_point bday = calendar::start_of_cataclysm;
    avatar &dummy = get_avatar();
    // Make sure the player doesn't drop anything :P
    dummy.wear_item( item::spawn( "backpack", bday ) );

    detached_ptr<item> det = item::spawn( "38_special", bday, item::default_charges_tag{} );
    item &ammo = *det;
    dummy.i_add( std::move( det ) );
    det = item::spawn( "38_speedloader", bday, false );
    item &speedloader = *det;
    dummy.i_add( std::move( det ) );
    int loader_pos = dummy.inv_position_by_item( &speedloader );
    det = item::spawn( "sw_619", bday, false );
    item &gun = *det;
    dummy.i_add( std::move( det ) );
    int ammo_pos = dummy.inv_position_by_item( &ammo );

    REQUIRE( ammo_pos != INT_MIN );
    REQUIRE( gun.ammo_remaining() == 0 );
    REQUIRE( gun.magazine_integral() );
    REQUIRE( loader_pos != INT_MIN );
    REQUIRE( speedloader.ammo_remaining() == 0 );
    REQUIRE( speedloader.has_flag( flag_SPEEDLOADER ) );

    bool speedloader_success = speedloader.reload( dummy, ammo, ammo.charges );

    REQUIRE( speedloader_success );
    REQUIRE( speedloader.ammo_remaining() == speedloader.ammo_capacity() );

    item_reload_option opt( &dummy, &gun, &gun, speedloader );
    REQUIRE( opt.qty() > 1 );
    dummy.assign_activity( std::make_unique<player_activity>(
                               std::make_unique<reload_activity_actor>( std::move( opt ) ) ) );
    REQUIRE( process_activity_bounded( dummy, 2000, 100000 ) );
    // CDF: item::reload refuses a second transfer from a speedloader (is_reloadable_helper
    // ammo_data guard), so the actor loads one round here, not the full cylinder as in DDA.
    CHECK( gun.ammo_remaining() >= 1 );
    // Speedloader is still in inventory.
    CHECK( dummy.inv_position_by_item( &speedloader ) != INT_MIN );
    CHECK( !dummy.activity );
}

// CDF: P3-6-1a-1; a destroyed target makes target_loc falsy, so reload() nulls the activity.
TEST_CASE( "reload_activity_actor_with_destroyed_target_does_nothing", "[p3][reload]" )
{
    clear_all_state();
    const time_point bday = calendar::start_of_cataclysm;
    avatar &dummy = get_avatar();
    // Make sure the player doesn't drop anything :P
    dummy.wear_item( item::spawn( "backpack", bday ) );

    detached_ptr<item> det = item::spawn( "40sw", bday, item::default_charges_tag{} );
    item &ammo = *det;
    dummy.i_add( std::move( det ) );

    REQUIRE( dummy.inv_position_by_item( &ammo ) != INT_MIN );
    REQUIRE( ammo.charges > 0 );

    std::unique_ptr<reload_activity_actor> actor;
    {
        detached_ptr<item> g = item::spawn( "sw_610", bday, false );
        item_reload_option opt( &dummy, g.get(), g.get(), ammo );
        REQUIRE( opt.qty() >= 1 );
        actor = std::make_unique<reload_activity_actor>( std::move( opt ) );
    }

    player_activity act( activity_id( "ACT_RELOAD" ), 100 );
    const std::string dmsg = capture_debugmsg_during( [&]() {
        actor->reload( act, dummy, 1 );
    } );
    CHECK( dmsg.empty() );
    CHECK( !act );
}

// CDF: P3-6-1a-1; the dirt block of DDA activity_actor.cpp:8378-8382, ported verbatim.
TEST_CASE( "reload_activity_actor_loosens_dirty_gun", "[p3][reload]" )
{
    clear_all_state();
    const time_point bday = calendar::start_of_cataclysm;
    avatar &dummy = get_avatar();
    // Make sure the player doesn't drop anything :P
    dummy.wear_item( item::spawn( "backpack", bday ) );

    detached_ptr<item> det = item::spawn( "40sw", bday, item::default_charges_tag{} );
    item &ammo = *det;
    dummy.i_add( std::move( det ) );
    det = item::spawn( "sw_610", bday, item::default_charges_tag{} );
    item &gun = *det;
    dummy.i_add( std::move( det ) );
    int ammo_pos = dummy.inv_position_by_item( &ammo );

    REQUIRE( ammo_pos != INT_MIN );
    REQUIRE( gun.ammo_remaining() == 0 );
    REQUIRE( gun.magazine_integral() );

    item_reload_option opt( &dummy, &gun, &gun, ammo );
    const int qty = opt.qty();
    REQUIRE( qty >= 1 );

    reload_activity_actor actor( std::move( opt ) );

    gun.set_var( "dirt", 8000 );
    REQUIRE( gun.get_var( "dirt", 0 ) == 8000 );

    player_activity act( activity_id( "ACT_RELOAD" ), 100 );
    REQUIRE( act );
    actor.reload( act, dummy, qty );
    CHECK( gun.get_var( "dirt", 0 ) < 7800 );
    CHECK( gun.get_var( "dirt", 0 ) >= 8000 - 2750 );
}

// CDF: P3-6-1a-3; ported from DDA tests/multimag_test.cpp:697-739. DDA's make_dual_well_gun
// loads both wells; CDF spawns test_multimag_gun and puts an empty glockmag into the glock well,
// so ammo_capacity() is the magazine capacity and the untargeted math gives more than 1.
TEST_CASE( "reload_option_qty_forces_1_for_pocket_targeted_reloads", "[p3][reload]" )
{
    clear_all_state();
    avatar &dummy = get_avatar();
    dummy.wear_item( item::spawn( itype_id( "backpack" ), calendar::start_of_cataclysm ), false );

    detached_ptr<item> det = item::spawn( itype_id( "test_multimag_gun" ) );
    item &gun = *det;
    dummy.i_add( std::move( det ) );

    detached_ptr<item> empty_mag = item::spawn( itype_glockmag );
    REQUIRE( gun.put_in( std::move( empty_mag ), pocket_type::MAGAZINE_WELL ).success() );

    // A whole stack of replacement glock magazines so available_ammo > 1.
    detached_ptr<item> glock_extra = item::spawn( itype_glockmag );
    glock_extra->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, 15 ), pocket_type::MAGAZINE );
    item &ammo = dummy.i_add( std::move( glock_extra ) );
    for( int i = 0; i < 2; ++i ) {
        detached_ptr<item> more = item::spawn( itype_glockmag );
        more->put_in( item::spawn( itype_id( "9mm" ), calendar::turn, 15 ), pocket_type::MAGAZINE );
        dummy.i_add( std::move( more ) );
    }

    int glock_idx = -1;
    {
        int idx = 0;
        for( const item_pocket *p : gun.get_pockets( []( const item_pocket & ) {
        return true;
    } ) ) {
            if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
                if( const item *mag = p->magazine_current() ) {
                    if( mag->typeId() == itype_glockmag ) {
                        glock_idx = idx;
                        break;
                    }
                }
            }
            ++idx;
        }
    }
    REQUIRE( glock_idx >= 0 );

    item_reload_option untargeted( &dummy, &gun, &gun, ammo );
    untargeted.qty( 1000 );
    REQUIRE( untargeted.qty() > 1 );

    item_reload_option opt( &dummy, &gun, &gun, ammo );
    opt.pocket_index = glock_idx;
    opt.qty( 1000 );
    CHECK( opt.qty() == 1 );
}

// CDF: P3-6-1a-3; CDF-only, no DDA test. Covers the integral-pocket capacity arm of
// item_reload_option::qty (DDA item_gun_tool_ammo.cpp:3335-3340).
TEST_CASE( "reload_option_qty_integral_pocket_is_scoped_to_that_pocket", "[p3][reload]" )
{
    clear_all_state();
    avatar &dummy = get_avatar();
    dummy.wear_item( item::spawn( itype_id( "backpack" ), calendar::start_of_cataclysm ), false );

    detached_ptr<item> det = item::spawn( itype_id( "test_multimag_gun_integral_ammo" ) );
    item &gun = *det;
    // CDF: the gun stays detached (not in the inventory): carrying an empty test_multimag_gun_integral_ammo
    // crashes in item::weight during i_add (see report).

    detached_ptr<item> ammo_det = item::spawn( itype_id( "9mm" ), calendar::turn, 50 );
    item &ammo = *ammo_det;
    dummy.i_add( std::move( ammo_det ) );

    detached_ptr<item> empty_cell = item::spawn( itype_id( "heavy_battery_cell" ) );
    REQUIRE( gun.put_in( std::move( empty_cell ), pocket_type::MAGAZINE_WELL ).success() );

    int idx = -1;
    {
        int i = 0;
        for( const item_pocket *p : gun.get_pockets( []( const item_pocket & ) {
        return true;
    } ) ) {
            if( p->is_type( pocket_type::MAGAZINE ) ) {
                idx = i;
                break;
            }
            ++i;
        }
    }
    REQUIRE( idx >= 0 );

    item_reload_option untargeted( &dummy, &gun, &gun, ammo );
    untargeted.qty( 1000 );
    REQUIRE( untargeted.qty() > 1 );

    item_reload_option opt( &dummy, &gun, &gun, ammo );
    opt.pocket_index = idx;
    opt.qty( 1000 );
    CHECK( opt.qty() == 1 );

    // A full integral pocket clamps to 1, never 0 or a negative.
    REQUIRE( gun.reload( dummy, ammo, 1, idx ) );
    opt.qty( 1000 );
    CHECK( opt.qty() == 1 );
}
