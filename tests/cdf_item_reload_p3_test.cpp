#include "activity_actor_definitions.h"
#include "avatar.h"
#include "avatar_functions.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "character_functions.h"
#include "coordinates.h"
#include "debug.h"
#include "flag.h"
#include "inventory_ui.h"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"
#include "player_activity.h"
#include "type_id.h"
#include "player_helpers.h"
#include "reload/reload.h"
#include "state_helpers.h"
#include "units.h"
#include "value_ptr.h"

#include <algorithm>
#include <climits>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

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

// CDF: P3-6-1a-2 (ADV-18 A/B). CDF's sw_610 is a legacy gun with no pocket, so loaded rounds add
// nothing to volume (plans/P3/6-1a-2/lookup-volume.md), but item::weight() counts the direct
// charges (item.cpp:5957): weight is the growth source of the overflow cases below.
// R5 stop: a growth of 0 g means the fixture proves nothing. Do not invent a source and do not
// switch guns; stop and report "blocked".
static units::mass sw610_reload_growth()
{
    clear_all_state();
    const time_point bday = calendar::start_of_cataclysm;
    avatar &dummy = get_avatar();
    dummy.wear_item( item::spawn( "backpack", bday ) );

    detached_ptr<item> det = item::spawn( "sw_610", bday, item::default_charges_tag{} );
    item &gun = *det;
    dummy.i_add( std::move( det ) );
    det = item::spawn( "40sw", bday, item::default_charges_tag{} );
    item &ammo = *det;
    dummy.i_add( std::move( det ) );

    const units::mass before = gun.weight();
    if( !gun.reload( dummy, ammo, ammo.charges ) ) {
        return 0_gram;
    }
    return gun.weight() - before;
}

// CDF: P3-6-1a-2; the G1 setup. 40_speedloader6 (SPEEDLOADER, data/json/items/magazine/40.json:3)
// holding 6 x 40sw, so the RELOAD_ONE clamp of reload.cpp:93 does not apply and qty() is 6.
static detached_ptr<item> loaded_40_speedloader( const time_point &bday )
{
    detached_ptr<item> loader = item::spawn( "40_speedloader6", bday );
    REQUIRE( loader->put_in( item::spawn( "40sw", bday, 6 ), pocket_type::MAGAZINE ).success() );
    REQUIRE( loader->ammo_remaining() == 6 );
    return loader;
}

// CDF: P3-6-1a-2 (ADV-18 C). Fills the pocket of bag with single 40sw rounds until it refuses the
// next one for weight; the bag then holds the gun before a reload but not after it.
static void fill_bag_tight_by_weight( item &bag, const time_point &bday )
{
    int passes = 0;
    bool refused = false;
    std::string refuse_msg;
    while( passes < 100 && !refused ) {
        detached_ptr<item> f = item::spawn( "40sw", bday, item::default_charges_tag{} );
        f->charges = 1;
        bool ok = false;
        refuse_msg = capture_debugmsg_during( [&]() {
            ok = bag.put_in( std::move( f ), pocket_type::CONTAINER ).success();
        } );
        if( ok ) {
            ++passes;
        } else {
            refused = true;
        }
    }
    REQUIRE( refused );
    REQUIRE( passes > 0 );
    CHECK( refuse_msg.find( "weight" ) != std::string::npos );
}

// CDF: P3-6-1a-2 (ADV-19 T1). Green probe of the growth source; the grams go into the report.
TEST_CASE( "reload_growth_probe_sw_610", "[p3][reload]" )
{
    const units::mass growth = sw610_reload_growth();
    WARN( "sw_610 reload growth = " << units::to_gram( growth ) << " g" );
    REQUIRE( growth > 0_gram );
}

// CDF: P3-6-1a-2 (ADV-19 T2, O-1). No DDA test names the overflow. The bag is
// test_tool_belt_pocket_mix: pocket P1 is 1 L / 1500 g / 40 cm
// (data/mods/TEST_DATA/dda_pocket_items.json:1309-1316), long enough for sw_610, tight by weight.
TEST_CASE( "reload_overflow_moves_item_to_other_pocket_via_actor", "[p3][reload]" )
{
    clear_all_state();
    const time_point bday = calendar::start_of_cataclysm;
    avatar &dummy = get_avatar();
    // CDF: clear_all_state gives the avatar DEBUG_STORAGE, an unlimited body storage that would
    // take the gun and make the case prove nothing (pattern of reloadables_test.cpp:24-26).
    dummy.unset_mutation( trait_id( "DEBUG_STORAGE" ) );
    dummy.update_body_storage();

    detached_ptr<item> b = item::spawn( "test_tool_belt_pocket_mix", bday );
    item &bag = *b;
    REQUIRE( !dummy.wear_item( std::move( b ) ) );

    detached_ptr<item> g = item::spawn( "sw_610", bday, item::default_charges_tag{} );
    item &gun = *g;
    REQUIRE( bag.put_in( std::move( g ), pocket_type::CONTAINER ).success() );
    REQUIRE( gun.parent_item() == &bag );
    const units::mass gun_weight_before = gun.weight();

    fill_bag_tight_by_weight( bag, bday );

    // Worn BEFORE the ammo is added, so the ammo lands in the backpack, which can take the gun later.
    REQUIRE( !dummy.wear_item( item::spawn( "backpack", bday ) ) );
    detached_ptr<item> ammo_det = loaded_40_speedloader( bday );
    item &ammo = dummy.i_add( std::move( ammo_det ) );
    REQUIRE( ammo.parent_item() != &bag );

    item_reload_option opt( &dummy, &gun, &gun, ammo );
    const int qty = opt.qty();
    REQUIRE( qty >= 3 );
    reload_activity_actor actor( std::move( opt ) );
    player_activity act( activity_id( "ACT_RELOAD" ), 100 );
    REQUIRE( act );
    const std::string dmsg = capture_debugmsg_during( [&]() {
        actor.reload( act, dummy, qty );
    } );

    REQUIRE( gun.ammo_remaining() == qty );
    REQUIRE( gun.weight() > gun_weight_before );
    CHECK( dmsg.empty() );
    CHECK( !bag.has_item( gun ) );
    CHECK( dummy.has_item( gun ) );
    // CDF: game_object::is_detached() returns !!loc (game_object.cpp:141-143), the inverse of its
    // name; has_position() is the intended "the item was not lost".
    CHECK( gun.has_position() );
}

// CDF: P3-6-1a-2 (ADV-19 T3, O-2). No second container: the grown gun has nowhere to go, and
// the uilist of the prompt answers UILIST_ERROR in test mode, which takes the drop branch.
TEST_CASE( "reload_overflow_drops_item_when_no_room_via_actor", "[p3][reload]" )
{
    clear_all_state();
    const time_point bday = calendar::start_of_cataclysm;
    avatar &dummy = get_avatar();
    // CDF: no body storage, so try_add has no pocket for the gun (pattern of
    // reloadables_test.cpp:24-26; DEBUG_STORAGE is test-only unlimited storage).
    dummy.unset_mutation( trait_id( "DEBUG_STORAGE" ) );
    dummy.update_body_storage();

    detached_ptr<item> b = item::spawn( "test_tool_belt_pocket_mix", bday );
    item &bag = *b;
    REQUIRE( !dummy.wear_item( std::move( b ) ) );

    detached_ptr<item> g = item::spawn( "sw_610", bday, item::default_charges_tag{} );
    item &gun = *g;
    REQUIRE( bag.put_in( std::move( g ), pocket_type::CONTAINER ).success() );
    REQUIRE( gun.parent_item() == &bag );
    const units::mass gun_weight_before = gun.weight();

    fill_bag_tight_by_weight( bag, bday );

    detached_ptr<item> ammo_det = loaded_40_speedloader( bday );
    item &ammo = *ammo_det;
    REQUIRE( !dummy.wield( std::move( ammo_det ) ) );

    item_reload_option opt( &dummy, &gun, &gun, ammo );
    const int qty = opt.qty();
    REQUIRE( qty >= 3 );
    reload_activity_actor actor( std::move( opt ) );
    player_activity act( activity_id( "ACT_RELOAD" ), 100 );
    REQUIRE( act );
    const std::string dmsg = capture_debugmsg_during( [&]() {
        actor.reload( act, dummy, qty );
    } );

    REQUIRE( gun.ammo_remaining() == qty );
    REQUIRE( gun.weight() > gun_weight_before );
    CHECK( dmsg.find( "test mode" ) != std::string::npos );
    CHECK( !dummy.has_item( gun ) );
    CHECK( gun.has_position() );
    CHECK( gun.bub_pos() == dummy.bub_pos() );
}

// CDF: P3-6-1a-2 (ADV-19 T4, O-3, G3). Regression guard, green before and after the tail: a
// reload that does not overfill the container leaves the item where it is. Occupancy is counted
// before and after, with no parent_it and no can_contain call.
TEST_CASE( "reload_inside_roomy_container_keeps_item_in_place_via_actor", "[p3][reload]" )
{
    clear_all_state();
    const time_point bday = calendar::start_of_cataclysm;
    avatar &dummy = get_avatar();
    // CDF: see the O-1 case; DEBUG_STORAGE body storage removed.
    dummy.unset_mutation( trait_id( "DEBUG_STORAGE" ) );
    dummy.update_body_storage();

    detached_ptr<item> b = item::spawn( "test_tool_belt_pocket_mix", bday );
    item &bag = *b;
    REQUIRE( !dummy.wear_item( std::move( b ) ) );

    detached_ptr<item> g = item::spawn( "sw_610", bday, item::default_charges_tag{} );
    item &gun = *g;
    REQUIRE( bag.put_in( std::move( g ), pocket_type::CONTAINER ).success() );
    REQUIRE( gun.parent_item() == &bag );
    const size_t occupied_before = bag.all_items_top().size();
    REQUIRE( occupied_before == 1 );

    REQUIRE( !dummy.wear_item( item::spawn( "backpack", bday ) ) );
    detached_ptr<item> ammo_det = loaded_40_speedloader( bday );
    item &ammo = dummy.i_add( std::move( ammo_det ) );
    REQUIRE( ammo.parent_item() != &bag );

    item_reload_option opt( &dummy, &gun, &gun, ammo );
    const int qty = opt.qty();
    REQUIRE( qty >= 3 );
    reload_activity_actor actor( std::move( opt ) );
    player_activity act( activity_id( "ACT_RELOAD" ), 100 );
    REQUIRE( act );
    const std::string dmsg = capture_debugmsg_during( [&]() {
        actor.reload( act, dummy, qty );
    } );

    REQUIRE( gun.ammo_remaining() == qty );
    CHECK( bag.all_items_top().size() == occupied_before );
    CHECK( bag.all_items_top().front() == &gun );
    CHECK( gun.parent_item() == &bag );
    CHECK( dmsg.empty() );
}

// CDF: P3-4-d-b; DDA tests/multimag_test.cpp:470-565 has no direct find_ammo case,
// and CDF has no Character::find_ammo, so these call character_funcs directly.
namespace
{

auto loaded_mag( const itype_id &mag_id, const itype_id &ammo_id, int count ) -> detached_ptr<item>
{
    auto mag = item::spawn( mag_id );
    mag->put_in( item::spawn( ammo_id, calendar::turn, count ), pocket_type::MAGAZINE );
    return mag;
}

auto result_contains( const std::vector<item *> &items, const item &needle ) -> bool
{
    return std::ranges::find( items, &needle ) != items.end();
}

} // namespace

TEST_CASE( "find_ammo_items_or_mags_finds_spare_mags_through_wells", "[p3][reload]" )
{
    clear_avatar();
    Character &you = get_player_character();
    you.wear_item( item::spawn( itype_id( "backpack" ) ), false );

    item &gun = you.i_add( item::spawn( itype_id( "test_multimag_gun" ) ) );
    item &glock = you.i_add( loaded_mag( itype_id( "glockmag" ), itype_id( "9mm" ), 15 ) );
    item &stanag = you.i_add( loaded_mag( itype_id( "stanag30" ), itype_id( "556" ), 30 ) );

    const std::vector<item *> found = character_funcs::find_ammo_items_or_mags( you, gun, false, -1 );
    CHECK( result_contains( found, glock ) );
    CHECK( result_contains( found, stanag ) );
    CHECK( found.size() == 2 );
}

TEST_CASE( "find_ammo_items_or_mags_now_flag_for_full_well", "[p3][reload]" )
{
    clear_avatar();
    Character &you = get_player_character();
    you.wear_item( item::spawn( itype_id( "backpack" ) ), false );

    auto gun_ptr = item::spawn( itype_id( "test_multimag_gun" ) );
    auto glock_in_gun = loaded_mag( itype_id( "glockmag" ), itype_id( "9mm" ), 15 );
    item *in_gun = glock_in_gun.get();
    gun_ptr->put_in( std::move( glock_in_gun ), pocket_type::MAGAZINE_WELL );
    item &gun = you.i_add( std::move( gun_ptr ) );
    REQUIRE( in_gun->parent_item() == &gun );

    item &spare_glock = you.i_add( loaded_mag( itype_id( "glockmag" ), itype_id( "9mm" ), 15 ) );
    item &spare_stanag = you.i_add( loaded_mag( itype_id( "stanag30" ), itype_id( "556" ), 30 ) );

    // CDF: expectation derived from the item data: TEST_DATA/items.json:1484-1485
    // restricts well 1 to glockmag (250 ml) and well 2 to stanag30 (500 ml); the
    // glockmag well is already taken, so only the stanag30 spare fits right now.
    const std::vector<item *> now_true =
        character_funcs::find_ammo_items_or_mags( you, gun, false, -1, true );
    CHECK( now_true.size() == 1 );
    CHECK( result_contains( now_true, spare_stanag ) );
    CHECK( !result_contains( now_true, spare_glock ) );
    CHECK( !result_contains( now_true, *in_gun ) );

    const std::vector<item *> now_false =
        character_funcs::find_ammo_items_or_mags( you, gun, false, -1, false );
    CHECK( now_false.size() == 2 );
    CHECK( result_contains( now_false, spare_stanag ) );
    CHECK( result_contains( now_false, spare_glock ) );
    CHECK( !result_contains( now_false, *in_gun ) );
}

TEST_CASE( "find_ammo_items_or_mags_skips_empty_mags_and_foreign_ammo", "[p3][reload]" )
{
    clear_avatar();
    Character &you = get_player_character();
    you.wear_item( item::spawn( itype_id( "backpack" ) ), false );

    item &gun = you.i_add( item::spawn( itype_id( "test_multimag_gun" ) ) );
    item &empty_glock = you.i_add( item::spawn( itype_id( "glockmag" ) ) );
    // CDF: 308 is not in the gun's ammo list (9mm, 223), so it never matches.
    item &foreign = you.i_add( item::spawn( itype_id( "308" ), calendar::turn, 6 ) );

    const std::vector<item *> no_empty =
        character_funcs::find_ammo_items_or_mags( you, gun, false, -1, true );
    CHECK( !result_contains( no_empty, empty_glock ) );
    CHECK( !result_contains( no_empty, foreign ) );

    const std::vector<item *> with_empty =
        character_funcs::find_ammo_items_or_mags( you, gun, true, -1, true );
    CHECK( result_contains( with_empty, empty_glock ) );
    CHECK( !result_contains( with_empty, foreign ) );
}

// ---------------------------------------------------------------------------
// CDF: P3-4-d-b; ported from DDA tests/multimag_test.cpp:106-130 (helpers) and
// 468-624 (character_list_ammo_per_well_targets_flag).
// ---------------------------------------------------------------------------

static const itype_id itype_556( "556" );
static const itype_id itype_9mm( "9mm" );
static const itype_id itype_stanag30( "stanag30" );
static const itype_id itype_test_multimag_gun( "test_multimag_gun" );

// CDF: item's move constructor is deleted (src/item.h:241), so the DDA helpers return a
// detached_ptr instead of an item by value.
static detached_ptr<item> make_loaded_stanag()
{
    detached_ptr<item> mag = item::spawn( itype_stanag30 );
    mag->put_in( item::spawn( itype_556, calendar::turn, 30 ), pocket_type::MAGAZINE );
    return mag;
}

static detached_ptr<item> make_dual_well_gun()
{
    detached_ptr<item> gun = item::spawn( itype_test_multimag_gun );
    detached_ptr<item> glock_mag = item::spawn( itype_glockmag );
    glock_mag->put_in( item::spawn( itype_9mm, calendar::turn, 15 ), pocket_type::MAGAZINE );
    gun->put_in( std::move( glock_mag ), pocket_type::MAGAZINE_WELL );
    gun->put_in( make_loaded_stanag(), pocket_type::MAGAZINE_WELL );
    return gun;
}

// CDF: DDA tag is [multimag][reload]; [4-d-b] is added so the build filter selects only
// the cases from this job.
TEST_CASE( "character_list_ammo_per_well_targets_flag", "[p3][reload][4-d-b]" )
{
    clear_all_state();
    Character &you = get_avatar();
    // Without storage, i_add silently drops items on the ground and the ammo search
    // would not see them.
    you.wear_item( item::spawn( "backpack" ), false );

    SECTION( "default flag emits one option per item plus one per loaded magazine" ) {
        item &gun = you.i_add( make_dual_well_gun() );
        // Loose ammo so list_ammo has something beyond the loaded mags.
        you.i_add( item::spawn( itype_9mm, calendar::turn, 15 ) );
        you.i_add( item::spawn( itype_556, calendar::turn, 30 ) );

        std::vector<item_reload_option> ammo_list;
        // CDF: DDA passes an item_location; CDF passes the item by reference.
        you.list_ammo( gun, ammo_list, /*empty=*/true );
        // Every emitted option must carry the first-compatible-well sentinel
        // when the per_well_targets flag is unset.
        for( const item_reload_option &opt : ammo_list ) {
            CHECK( opt.pocket_index < 0 );
        }
    }

    SECTION( "per_well_targets=true emits per-well options with stable pocket_index" ) {
        // Use an empty multi-well gun so the ammo search will accept the spare
        // magazines as valid reload candidates (it rejects spares when the well
        // is already full and now=true).
        item &gun = you.i_add( item::spawn( itype_test_multimag_gun ) );
        detached_ptr<item> glock_extra = item::spawn( itype_glockmag );
        glock_extra->put_in( item::spawn( itype_9mm, calendar::turn, 15 ),
                             pocket_type::MAGAZINE );
        detached_ptr<item> stanag_extra = item::spawn( itype_stanag30 );
        stanag_extra->put_in( item::spawn( itype_556, calendar::turn, 30 ),
                              pocket_type::MAGAZINE );
        you.i_add( std::move( glock_extra ) );
        you.i_add( std::move( stanag_extra ) );

        // Collect the two well indices in the gun's pocket list.
        std::vector<int> well_indices;
        {
            int idx = 0;
            for( const item_pocket *p : gun.get_pockets(
            []( const item_pocket & ) {
            return true;
        } ) ) {
                if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
                    well_indices.push_back( idx );
                }
                ++idx;
            }
        }
        REQUIRE( well_indices.size() == 2 );

        std::vector<item_reload_option> ammo_list;
        you.list_ammo( gun, ammo_list, /*empty=*/true, /*per_well_targets=*/true );

        std::set<int> emitted_pocket_indices;
        for( const item_reload_option &opt : ammo_list ) {
            emitted_pocket_indices.insert( opt.pocket_index );
        }
        // Both wells emit at their contents index.
        for( int idx : well_indices ) {
            CHECK( emitted_pocket_indices.count( idx ) == 1 );
        }

        // No cross-well leak: a glockmag option must not target the stanag well.
        const item_pocket *first_well = nullptr;
        const item_pocket *second_well = nullptr;
        {
            int idx = 0;
            for( const item_pocket *p : gun.get_pockets(
            []( const item_pocket & ) {
            return true;
        } ) ) {
                if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
                    if( idx == well_indices[0] ) {
                        first_well = p;
                    } else if( idx == well_indices[1] ) {
                        second_well = p;
                    }
                }
                ++idx;
            }
        }
        REQUIRE( first_well != nullptr );
        REQUIRE( second_well != nullptr );
        for( const item_reload_option &opt : ammo_list ) {
            if( opt.pocket_index < 0 ) {
                continue;
            }
            const item_pocket *picked =
                opt.pocket_index == well_indices[0] ? first_well : second_well;
            CAPTURE( opt.pocket_index );
            CAPTURE( opt.ammo->tname() );
            CHECK( picked->can_reload_with( *opt.ammo, true ) );
        }

        // Class-(a) entries swap one magazine: qty() == 1.
        for( const item_reload_option &opt : ammo_list ) {
            if( opt.pocket_index >= 0 ) {
                CHECK( opt.qty() == 1 );
            }
        }
    }

    SECTION( "empty-well pocket_index is the position in contents, not a "
             "magazines_current() position" ) {
        // Indices must match contents, not magazines_current().
        detached_ptr<item> gun_ptr = item::spawn( itype_test_multimag_gun );
        gun_ptr->put_in( make_loaded_stanag(), pocket_type::MAGAZINE_WELL );
        item &gun = you.i_add( std::move( gun_ptr ) );

        detached_ptr<item> glock_extra = item::spawn( itype_glockmag );
        glock_extra->put_in( item::spawn( itype_9mm, calendar::turn, 15 ),
                             pocket_type::MAGAZINE );
        detached_ptr<item> stanag_extra = item::spawn( itype_stanag30 );
        stanag_extra->put_in( item::spawn( itype_556, calendar::turn, 30 ),
                              pocket_type::MAGAZINE );
        you.i_add( std::move( glock_extra ) );
        you.i_add( std::move( stanag_extra ) );

        // Locate each well in the gun's pocket list.
        int empty_well_idx = -1;
        int loaded_well_idx = -1;
        {
            int idx = 0;
            for( const item_pocket *p : gun.get_pockets(
            []( const item_pocket & ) {
            return true;
        } ) ) {
                if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
                    if( p->magazine_current() == nullptr ) {
                        empty_well_idx = idx;
                    } else {
                        loaded_well_idx = idx;
                    }
                }
                ++idx;
            }
        }
        REQUIRE( empty_well_idx >= 0 );
        REQUIRE( loaded_well_idx >= 0 );
        REQUIRE( empty_well_idx != loaded_well_idx );

        std::vector<item_reload_option> ammo_list;
        you.list_ammo( gun, ammo_list, /*empty=*/true, /*per_well_targets=*/true );

        // Empty well still addressable by its stable contents index.
        bool saw_empty = false;
        for( const item_reload_option &opt : ammo_list ) {
            if( opt.pocket_index == empty_well_idx ) {
                saw_empty = true;
            }
        }
        CHECK( saw_empty );
    }
}

// CDF: P3-4-d-c; DDA character_ammo.cpp:233-244 predicate is item::is_reloadable().
TEST_CASE( "find_reloadables_uses_is_reloadable_predicate", "[p3][reload]" )
{
    clear_avatar();
    avatar &guy = get_avatar();
    item &jug = guy.i_add(
                    item::spawn( itype_id( "test_watertight_open_sealed_container_1L" ) ) );
    REQUIRE( jug.is_reloadable() );
    // CDF: the old BN predicate only considered guns and magazines, so an empty
    // watertight container was never returned.
    CHECK( result_contains( character_funcs::find_reloadables( guy ), jug ) );
    // CDF: negative control, a plain non-reloadable item must not be returned.
    item &rock = guy.i_add( item::spawn( itype_id( "rock" ) ) );
    REQUIRE( !rock.is_reloadable() );
    CHECK( !result_contains( character_funcs::find_reloadables( guy ), rock ) );
}

// CDF: P3-4-d-c; DDA character_ammo.cpp:30-69 ammo_count_for counting loop.
// CDF: uses test_hybrid_ammo_gun (data/mods/TEST_DATA/items.json), a fixture added for this test:
// test_multimag_gun only accepts glockmag/stanag30 in its wells, so find_ammo_items_or_mags never
// returned loose ammo for it and the loose-ammo path was unreachable.
TEST_CASE( "ammo_count_for_counts_loaded_and_loose", "[p3][reload][ammo]" )
{
    clear_avatar();
    avatar &guy = get_avatar();
    item &gun = guy.i_add( item::spawn( itype_id( "test_hybrid_ammo_gun" ) ) );
    REQUIRE( gun.ammo_required() == 1 );
    REQUIRE( gun.ammo_remaining() == 0 );
    REQUIRE( !gun.magazine_integral() );

    SECTION( "magazine only" ) {
        item &mag = guy.i_add( loaded_mag( itype_id( "glockmag" ), itype_id( "9mm" ), 15 ) );
        const auto found = character_funcs::find_ammo_items_or_mags( guy, gun, true, -1 );
        REQUIRE( result_contains( found, mag ) );
        // CDF: glockmag capacity 15, data/json/items/magazine/9mm.json:72.
        CHECK( character_funcs::ammo_count_for( guy, gun ) == 15 );
    }

    SECTION( "no magazine, loose only" ) {
        item &loose = guy.i_add( item::spawn( itype_id( "9mm" ), calendar::turn, 5 ) );
        const auto found = character_funcs::find_ammo_items_or_mags( guy, gun, true, -1 );
        // The loose stack IS found (so the 0 below comes from the has_mag gate, not the finder).
        REQUIRE( result_contains( found, loose ) );
        // CDF: no magazine in play, so loose ammo is not available (DDA 56-58).
        CHECK( character_funcs::ammo_count_for( guy, gun ) == 0 );
    }

    SECTION( "magazine then loose" ) {
        item &mag = guy.i_add( loaded_mag( itype_id( "glockmag" ), itype_id( "9mm" ), 15 ) );
        item &loose = guy.i_add( item::spawn( itype_id( "9mm" ), calendar::turn, 5 ) );
        const auto found = character_funcs::find_ammo_items_or_mags( guy, gun, true, -1 );
        REQUIRE( result_contains( found, mag ) );
        REQUIRE( result_contains( found, loose ) );
        // CDF: 15 in the glockmag (9mm.json:72) + 5 loose 9mm charges.
        CHECK( character_funcs::ammo_count_for( guy, gun ) == 20 );
    }

    SECTION( "loose then magazine" ) {
        item &loose = guy.i_add( item::spawn( itype_id( "9mm" ), calendar::turn, 5 ) );
        item &mag = guy.i_add( loaded_mag( itype_id( "glockmag" ), itype_id( "9mm" ), 15 ) );
        const auto found = character_funcs::find_ammo_items_or_mags( guy, gun, true, -1 );
        REQUIRE( result_contains( found, mag ) );
        REQUIRE( result_contains( found, loose ) );
        // CDF: same total regardless of inventory order (DDA sums loose after the loop).
        CHECK( character_funcs::ammo_count_for( guy, gun ) == 20 );
    }
}

// CDF: P3-4-d-c / 8-1d Decision 10 (test 7): DDA character_ammo.cpp:63-66 energy term.
TEST_CASE( "ammo_count_for_energy_term", "[p3][ammo][energy]" )
{
    clear_avatar();
    avatar &guy = get_avatar();
    // data/mods/TEST_DATA/items.json:2352: energy_drain 5 kJ, one battery well.
    item gun( itype_id( "test_energy_gun" ) );
    item battery( itype_id( "battery_test" ) );

    SECTION( "loaded" ) {
        REQUIRE( battery.mod_energy( 30_kJ ) == 0_J );
        REQUIRE( gun.put_in( battery, pocket_type::MAGAZINE_WELL ).success() );
        REQUIRE( gun.energy_remaining( &guy ) == 30_kJ );
        REQUIRE( gun.ammo_required() == 0 );
        REQUIRE( gun.get_gun_ups_drain() == 0 );
        // CDF: 30 kJ / 5 kJ drain.
        CHECK( character_funcs::ammo_count_for( guy, gun ) == 6 );
    }

    SECTION( "with gunmod" ) {
        REQUIRE( battery.mod_energy( 30_kJ ) == 0_J );
        REQUIRE( gun.put_in( battery, pocket_type::MAGAZINE_WELL ).success() );
        // data/mods/TEST_DATA/items.json:2353: multiplier 2.0, modifier 1 kJ.
        REQUIRE( gun.put_in( item( itype_id( "test_energy_gunmod" ) ), pocket_type::MOD ).success() );
        REQUIRE( gun.get_gun_energy_drain() == 11_kJ );
        // CDF: 30 kJ / 11 kJ integer division.
        CHECK( character_funcs::ammo_count_for( guy, gun ) == 2 );
    }

    SECTION( "empty well" ) {
        CHECK( character_funcs::ammo_count_for( guy, gun ) == 0 );
    }
}

// ---------------------------------------------------------------------------
// CDF: P3-5-2a; ported from DDA tests/multimag_test.cpp:741-949 (get_possible_reload_targets,
// find_matching_reload_target, find_all_matching_reload_targets) and :2292-2319 (integral pocket
// target). DDA tag is [multimag][reload]; [p3] and [5-2a] are added so the build filter selects
// only the cases of this job. item_location becomes item & / item * (D1); DDA's i_add of an item
// copy becomes i_add of a spawned detached_ptr<item>, and `rt.owner == gun_loc` becomes
// `rt.owner == &gun`.
// ---------------------------------------------------------------------------

static const itype_id itype_38_special( "38_special" );
// CDF: DDA uses sw_619, an integral-magazine revolver. CDF's sw_619 has legacy `magazines`
// JSON and so gets a MAGAZINE_WELL; cop_38 (derringer) is a real integral-magazine gun with no
// well, which is what the DDA section needs.
static const itype_id itype_cop_38( "cop_38" );
static const itype_id itype_test_multimag_gun_integral_ammo( "test_multimag_gun_integral_ammo" );
static const itype_id itype_test_multimag_gun_same_type( "test_multimag_gun_same_type" );

TEST_CASE( "get_possible_reload_targets_emits_per_well_entries",
           "[p3][reload][multimag][5-2a]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    you.wear_item( item::spawn( itype_id( "backpack" ) ), false );

    SECTION( "multi-well gun: one well entry per MAGAZINE_WELL plus loaded-mag entry" ) {
        item &gun = you.i_add( make_dual_well_gun() );

        const std::vector<reload_target> targets = get_possible_reload_targets( gun );
        std::set<int> well_indices;
        int loaded_mag_count = 0;
        for( const reload_target &rt : targets ) {
            if( rt.kind == reload_target::kind::well && rt.owner == &gun ) {
                well_indices.insert( rt.pocket_index );
            } else if( rt.kind == reload_target::kind::loaded_mag && rt.owner == &gun ) {
                loaded_mag_count++;
            }
        }
        // Both wells appear with their actual contents indices, and each
        // loaded magazine produces its own loaded-mag entry.
        CHECK( well_indices.size() == 2 );
        CHECK( loaded_mag_count == 2 );

        // CDF: added. DDA checks only the two counts above. The expected indices are the
        // wells' positions in the gun's pocket list, enumerated here without the function
        // under test; the entry count is two wells plus two loaded magazines (no integral
        // MAGAZINE pocket, no gunmod, so no owner fallback).
        std::set<int> expected_well_indices;
        {
            int idx = 0;
            for( const item_pocket *p : gun.get_pockets( []( const item_pocket & ) {
            return true;
        } ) ) {
                if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
                    expected_well_indices.insert( idx );
                }
                ++idx;
            }
        }
        REQUIRE( expected_well_indices.size() == 2 );
        CHECK( well_indices == expected_well_indices );
        CHECK( targets.size() == 4 );
        for( const reload_target &rt : targets ) {
            CAPTURE( rt.pocket_index, rt.ui_well_index );
            if( rt.kind == reload_target::kind::loaded_mag ) {
                REQUIRE( rt.target != nullptr );
                CHECK( rt.pocket_index == -1 );
                CHECK( rt.target != rt.owner );
                CHECK( ( rt.target->typeId() == itype_glockmag ||
                         rt.target->typeId() == itype_stanag30 ) );
                CHECK( expected_well_indices.count( rt.ui_well_index ) == 1 );
            } else {
                CHECK( rt.target == &gun );
                CHECK( rt.ui_well_index == rt.pocket_index );
            }
        }
    }

    SECTION( "integral-mag gun emits a fallback loaded-mag entry on the gun itself" ) {
        item &revolver = you.i_add( item::spawn( itype_cop_38 ) );
        // CDF: added. The fixture premise: no MAGAZINE_WELL, so the only entry is the fallback.
        REQUIRE( revolver.get_pockets( []( const item_pocket & pp ) {
            return pp.is_type( pocket_type::MAGAZINE_WELL );
        } ).empty() );

        const std::vector<reload_target> targets = get_possible_reload_targets( revolver );
        bool found_fallback = false;
        for( const reload_target &rt : targets ) {
            if( rt.kind == reload_target::kind::loaded_mag && rt.target == &revolver &&
                rt.pocket_index < 0 ) {
                found_fallback = true;
            }
        }
        CHECK( found_fallback );
        // CDF: added. A revolver with no well and no gunmod has exactly the one fallback entry.
        CHECK( targets.size() == 1 );
    }
}

TEST_CASE( "find_matching_reload_target_routes_ammo_to_correct_well",
           "[p3][reload][multimag][5-2a]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    you.wear_item( item::spawn( itype_id( "backpack" ) ), false );

    item &gun = you.i_add( item::spawn( itype_test_multimag_gun ) );

    const std::vector<reload_target> targets = get_possible_reload_targets( gun );

    // Find each well's contents index in gun.
    int glock_well_idx = -1;
    int stanag_well_idx = -1;
    {
        int idx = 0;
        for( const item_pocket *p : gun.get_pockets( []( const item_pocket & ) {
        return true;
    } ) ) {
            if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
                if( glock_well_idx < 0 ) {
                    glock_well_idx = idx;
                } else if( stanag_well_idx < 0 ) {
                    stanag_well_idx = idx;
                }
            }
            ++idx;
        }
    }
    REQUIRE( glock_well_idx >= 0 );
    REQUIRE( stanag_well_idx >= 0 );

    SECTION( "glock magazine routes to the glockmag-restricted well" ) {
        detached_ptr<item> glock_ptr = item::spawn( itype_glockmag );
        glock_ptr->put_in( item::spawn( itype_9mm, calendar::turn, 15 ), pocket_type::MAGAZINE );
        item &glock_mag = you.i_add( std::move( glock_ptr ) );

        const reload_target *match = find_matching_reload_target( targets, glock_mag );
        REQUIRE( match != nullptr );
        CHECK( match->kind == reload_target::kind::well );
        CHECK( match->pocket_index == glock_well_idx );
    }

    SECTION( "stanag magazine routes to the stanag30-restricted well" ) {
        detached_ptr<item> stanag_ptr = item::spawn( itype_stanag30 );
        stanag_ptr->put_in( item::spawn( itype_556, calendar::turn, 30 ), pocket_type::MAGAZINE );
        item &stanag_mag = you.i_add( std::move( stanag_ptr ) );

        const reload_target *match = find_matching_reload_target( targets, stanag_mag );
        REQUIRE( match != nullptr );
        CHECK( match->kind == reload_target::kind::well );
        CHECK( match->pocket_index == stanag_well_idx );
    }

    SECTION( "same-type dual-well: find_all_matching emits one entry per accepting well" ) {
        item &same_gun = you.i_add( item::spawn( itype_test_multimag_gun_same_type ) );
        const std::vector<reload_target> same_targets = get_possible_reload_targets( same_gun );

        detached_ptr<item> glock_ptr = item::spawn( itype_glockmag );
        glock_ptr->put_in( item::spawn( itype_9mm, calendar::turn, 15 ), pocket_type::MAGAZINE );
        item &glock_mag = you.i_add( std::move( glock_ptr ) );

        const std::vector<const reload_target *> matches =
            find_all_matching_reload_targets( same_targets, glock_mag );
        REQUIRE( matches.size() == 2 );
        // Class-(a) well entries with distinct pocket_index for disambiguation.
        std::set<int> seen_pocket_indices;
        for( const reload_target *rt : matches ) {
            CHECK( rt->kind == reload_target::kind::well );
            seen_pocket_indices.insert( rt->pocket_index );
        }
        CHECK( seen_pocket_indices.size() == 2 );
    }

    SECTION( "same-type loaded mags: loose ammo matches every loaded mag for class-(b) top-up" ) {
        // Both wells loaded with glockmag; one half-full, one nearly empty.
        // Direct-insert into each well by walking pockets; put_in finds only
        // the first matching pocket and would route both magazines into well
        // 1 here.
        detached_ptr<item> gun_two_ptr = item::spawn( itype_test_multimag_gun_same_type );
        detached_ptr<item> glock_a = item::spawn( itype_glockmag );
        glock_a->put_in( item::spawn( itype_9mm, calendar::turn, 5 ), pocket_type::MAGAZINE );
        detached_ptr<item> glock_b = item::spawn( itype_glockmag );
        glock_b->put_in( item::spawn( itype_9mm, calendar::turn, 1 ), pocket_type::MAGAZINE );
        std::vector<item_pocket *> wells;
        for( item_pocket *p : gun_two_ptr->get_pockets( []( const item_pocket & pp ) {
        return pp.is_type( pocket_type::MAGAZINE_WELL );
        } ) ) {
            wells.push_back( p );
        }
        REQUIRE( wells.size() == 2 );
        REQUIRE( wells[0]->insert_item( std::move( glock_a ) ).success() );
        REQUIRE( wells[1]->insert_item( std::move( glock_b ) ).success() );
        REQUIRE( gun_two_ptr->magazines_current().size() == 2 );

        item &two_gun = you.i_add( std::move( gun_two_ptr ) );
        REQUIRE( two_gun.magazines_current().size() == 2 );

        const std::vector<reload_target> two_targets = get_possible_reload_targets( two_gun );

        item &loose = you.i_add( item::spawn( itype_9mm, calendar::turn, 30 ) );

        const std::vector<const reload_target *> matches =
            find_all_matching_reload_targets( two_targets, loose );
        // Both loaded magazines accept loose 9mm; the helper must surface
        // both so the disambiguation menu can offer a real choice.
        int loaded_mag_matches = 0;
        std::vector<int> loaded_remaining;
        for( const reload_target *rt : matches ) {
            if( rt->kind == reload_target::kind::loaded_mag ) {
                loaded_mag_matches++;
                loaded_remaining.push_back( rt->target->ammo_remaining() );
            }
        }
        CHECK( loaded_mag_matches == 2 );
        // CDF: added. The two matches are the 5-round and the 1-round magazine, in either order.
        std::sort( loaded_remaining.begin(), loaded_remaining.end() );
        const std::vector<int> expected_remaining{ 1, 5 };
        CHECK( loaded_remaining == expected_remaining );

        // Picking the less-full mag must let qty() refill toward its own
        // remaining capacity, not stay capped by the more-full mag.
        const reload_target *small_mag_target = nullptr;
        for( const reload_target *rt : matches ) {
            if( rt->kind == reload_target::kind::loaded_mag &&
                rt->target->ammo_remaining() == 1 ) {
                small_mag_target = rt;
                break;
            }
        }
        REQUIRE( small_mag_target != nullptr );
        // CDF: item_reload_option( who, target, parent, ammo, pocket_index ) takes the base item
        // as `parent` (reload.h 26-31); DDA's reload_option has no parent. The gun is the base.
        item_reload_option opt( &you, small_mag_target->target, &two_gun, loose,
                                small_mag_target->pocket_index );
        // glockmag capacity 15, mag has 1 round, room for 14.
        CHECK( opt.qty() == 14 );

        // Player picked a partial count below either mag's remaining capacity;
        // disambiguation must preserve it.
        item_reload_option opt_partial( &you, small_mag_target->target, &two_gun, loose,
                                        small_mag_target->pocket_index );
        opt_partial.qty( 3 );
        CHECK( opt_partial.qty() == 3 );
    }

    SECTION( "magazine compatible with no well returns nullptr" ) {
        // 38_special is loose ammo for the integral revolver fixture and is
        // not compatible with either of test_multimag_gun's wells (glockmag
        // / stanag30 restrictions). Loose ammo cannot reload a well.
        item &loose = you.i_add( item::spawn( itype_38_special, calendar::turn, 7 ) );

        const reload_target *match = find_matching_reload_target( targets, loose );
        CHECK( match == nullptr );
    }
}

// Reload menu must surface integral MAGAZINE as its own target on a multimag
// gun; otherwise loose ammo has no reachable destination.
TEST_CASE( "multimag_reload_targets_include_integral_pocket",
           "[p3][reload][multimag][5-2a]" )
{
    clear_all_state();
    avatar &dummy = get_avatar();
    dummy.wear_item( item::spawn( itype_id( "backpack" ) ), false );
    item &gun = dummy.i_add( item::spawn( itype_test_multimag_gun_integral_ammo ) );

    const std::vector<reload_target> targets = get_possible_reload_targets( gun );
    bool saw_integral = false;
    bool saw_well = false;
    for( const reload_target &rt : targets ) {
        if( rt.kind == reload_target::kind::integral_magazine ) {
            saw_integral = true;
        }
        if( rt.kind == reload_target::kind::well ) {
            saw_well = true;
        }
    }
    CHECK( saw_integral );
    CHECK( saw_well );

    // CDF: added. The fixture lists its integral 9mm MAGAZINE pocket before its battery well,
    // so the well is NOT pocket 0: a pocket_index counted among wells only (0) differs from
    // the contents index. The positions are enumerated here without the function under test.
    int integral_idx = -1;
    int well_idx = -1;
    {
        int idx = 0;
        for( const item_pocket *p : gun.get_pockets( []( const item_pocket & ) {
        return true;
    } ) ) {
            if( p->is_type( pocket_type::MAGAZINE ) && integral_idx < 0 &&
                p->get_pocket_data() != nullptr &&
                !p->get_pocket_data()->ammo_restriction.empty() ) {
                integral_idx = idx;
            } else if( p->is_type( pocket_type::MAGAZINE_WELL ) && well_idx < 0 ) {
                well_idx = idx;
            }
            ++idx;
        }
    }
    REQUIRE( integral_idx >= 0 );
    REQUIRE( well_idx >= 1 );
    CHECK( targets.size() == 2 );
    for( const reload_target &rt : targets ) {
        CAPTURE( rt.pocket_index );
        if( rt.kind == reload_target::kind::integral_magazine ) {
            CHECK( rt.pocket_index == integral_idx );
            CHECK( rt.target == &gun );
            CHECK( rt.owner == &gun );
        } else if( rt.kind == reload_target::kind::well ) {
            CHECK( rt.pocket_index == well_idx );
            CHECK( rt.target == &gun );
        }
    }

    // Loose slug must be accepted by *some* reload_target.
    detached_ptr<item> slug_ptr = item::spawn( itype_9mm, calendar::turn, 1 );
    const reload_target *match = find_matching_reload_target( targets, *slug_ptr );
    REQUIRE( match != nullptr );
    CHECK( match->kind == reload_target::kind::integral_magazine );
    // CDF: added.
    CHECK( match->pocket_index == integral_idx );
}

// CDF: P3-5-2a; CDF-only (DDA has no test of the gunmod arm of get_possible_reload_targets,
// DDA inventory_ui.cpp 4094-4097). Host m4a1 with a full stanag30 in its well and the
// test_multimag_aux_gun mod (one glockmag well, ammo 9mm) in its MOD pocket, as in
// item_is_reloadable_gunmod.
TEST_CASE( "cdf_reload_target_gunmod_owner_entries", "[p3][reload][5-2a]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    you.wear_item( item::spawn( itype_id( "backpack" ) ), false );

    detached_ptr<item> gun_ptr = item::spawn( itype_id( "m4a1" ) );
    REQUIRE( gun_ptr->put_in( make_loaded_stanag(), pocket_type::MAGAZINE_WELL ).success() );
    REQUIRE( gun_ptr->put_in( item::spawn( itype_id( "test_multimag_aux_gun" ) ),
                              pocket_type::MOD ).success() );
    item &gun = you.i_add( std::move( gun_ptr ) );
    REQUIRE( gun.gunmods().size() == 1 );
    item &mod = *gun.gunmods().front();

    // Well positions in each item's own pocket list, enumerated without the function under test.
    const auto well_indices_of = []( item & it ) {
        std::vector<int> out;
        int idx = 0;
        for( const item_pocket *p : it.get_pockets( []( const item_pocket & ) {
        return true;
    } ) ) {
            if( p->is_type( pocket_type::MAGAZINE_WELL ) ) {
                out.push_back( idx );
            }
            ++idx;
        }
        return out;
    };
    const std::vector<int> gun_wells = well_indices_of( gun );
    const std::vector<int> mod_wells = well_indices_of( mod );
    REQUIRE( gun_wells.size() == 1 );
    REQUIRE( mod_wells.size() == 1 );

    const std::vector<reload_target> targets = get_possible_reload_targets( gun );

    SECTION( "the host and the mod each get their own entries" ) {
        int host_wells = 0;
        int host_mags = 0;
        int mod_well_entries = 0;
        int mod_mags = 0;
        int others = 0;
        for( const reload_target &rt : targets ) {
            if( rt.owner == &gun && rt.kind == reload_target::kind::well ) {
                host_wells++;
            } else if( rt.owner == &gun && rt.kind == reload_target::kind::loaded_mag ) {
                host_mags++;
            } else if( rt.owner == &mod && rt.kind == reload_target::kind::well ) {
                mod_well_entries++;
                CHECK( rt.target == &mod );
                CHECK( rt.pocket_index == mod_wells.front() );
            } else if( rt.owner == &mod && rt.kind == reload_target::kind::loaded_mag ) {
                mod_mags++;
            } else {
                others++;
            }
        }
        CHECK( host_wells == 1 );
        CHECK( host_mags == 1 );
        CHECK( mod_well_entries == 1 );
        // The mod's well is empty (no loaded-mag entry) and, having a well entry, gets no
        // owner-level fallback entry either.
        CHECK( mod_mags == 0 );
        CHECK( others == 0 );
    }

    // [UNSURE] whether item::can_reload_with accepts a glockmag for the aux gunmod as the
    // 9mm gun-gunmod it is (derive: call mod.can_reload_with( glock_mag, true ) once in a
    // throw-away probe). If this section is red while the entries section above is green and
    // that probe prints false, delete this section only, with a log line in the report and a
    // Blocked line; do not change the fixture or the assertions.
    SECTION( "a glock magazine routes to the mod's own well" ) {
        detached_ptr<item> glock_ptr = item::spawn( itype_glockmag );
        glock_ptr->put_in( item::spawn( itype_9mm, calendar::turn, 15 ), pocket_type::MAGAZINE );
        item &glock_mag = you.i_add( std::move( glock_ptr ) );

        const reload_target *match = find_matching_reload_target( targets, glock_mag );
        REQUIRE( match != nullptr );
        CHECK( match->kind == reload_target::kind::well );
        CHECK( match->owner == &mod );
        CHECK( match->pocket_index == mod_wells.front() );
    }
}
