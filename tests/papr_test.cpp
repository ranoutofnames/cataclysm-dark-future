// PAPR1: the powered air purifying respirator system, first part (modular blower, strap, two masks, MOLLE kit).
// DDA source: src/iuse.cpp:3613-3760 (papr_mask_activate, papr_mask, papr_blower), src/item_degrade.cpp:212-216
// (item::activation_success), data/json/items/tool_armor.json, items/armor/ammo_pouch.json,
// itemgroups/Clothing_Gear/gear.json (DDA commit b4b62254ae2).
// CDF-authored (the DDA tree has no test for this data). CDF has no tick_action: an active item ticks through its
// use_action list with t == true, so the DDA pairs (papr_mask + papr_mask_activate, papr_blower + turn-off) are one
// function each (amendment E1).
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "item.h"
#include "item_contents.h"
#include "item_factory.h"
#include "item_group.h"
#include "item_pocket.h"
#include "iteminfo_query.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "map/map.h"
#include "map/field_type.h"
#include "map_helpers.h"
#include "messages.h"
#include "player_helpers.h"
#include "pocket_data.h"
#include "pocket_type.h"
#include "state_helpers.h"
#include "type_id.h"

namespace
{
// CDF: Messages is stubbed in the test binary (tests/fake_messages.cpp: recent_messages is always empty), so the DDA
// message assertions (requires new filters, fails to function, stops working, is leaking, fail to adjust) cannot be
// observed and are dropped; the state each message accompanies (is_active, overwrite_env_resist) stays asserted.
// a bare spawn leaves a MAGAZINE_WELL empty on this base (as helmet_eod's does), so every well is loaded with its
// default_magazine here: the blower tests read the filter and battery of a working unit
auto spawn_item( const char *id ) -> detached_ptr<item>
{
    detached_ptr<item> spawned = item::spawn( itype_id( id ), calendar::turn );
    for( const pocket_data &pd : spawned->type->pockets ) {
        if( pd.type != pocket_type::MAGAZINE_WELL || pd.default_magazine.is_null() ) {
            continue;
        }
        detached_ptr<item> mag = item::spawn( pd.default_magazine, calendar::turn );
        mag->ammo_set( mag->ammo_default() );
        spawned->put_in( std::move( mag ), pocket_type::MAGAZINE_WELL );
    }
    return spawned;
}

auto well_pockets( const itype &type ) -> std::vector<const pocket_data *>
{
    std::vector<const pocket_data *> wells;
    for( const pocket_data &p : type.pockets ) {
        if( p.type == pocket_type::MAGAZINE_WELL ) {
            wells.push_back( &p );
        }
    }
    return wells;
}

// the size check of molle_attach_actor::use, as tests/pch1_pouches_test.cpp does it
auto attach_to_vest( item &vest, const char *pouch_id ) -> bool
{
    detached_ptr<item> pouch = spawn_item( pouch_id );
    const use_function *use = vest.get_use( "attach_molle" );
    if( use == nullptr || !pouch->can_attach_as_pocket() ) {
        return false;
    }
    const molle_attach_actor *actor = dynamic_cast<const molle_attach_actor *>( use->get_actor_ptr() );
    if( actor == nullptr ||
        pouch->get_pocket_size() > actor->size - vest.contents.get_additional_space_used() ) {
        return false;
    }
    vest.contents.add_pocket( std::move( pouch ) );
    return true;
}

// wears an item on the avatar and returns it
auto wear_it( avatar &you, const char *id ) -> item &
{
    detached_ptr<item> holder = spawn_item( id );
    item &worn = *holder;
    REQUIRE( !you.wear_item( std::move( holder ), false ) );
    return worn;
}

// takes a worn item off and destroys it
void take_off_it( avatar &you, item &worn )
{
    std::vector<detached_ptr<item>> removed;
    REQUIRE( you.takeoff( worn, &removed ) );
}
} // namespace

// DDA: iuse.cpp:3735-3740 (the filter, not the battery, pays for absorbed gas)
TEST_CASE( "papr_p1_blower_consumes_filter_pocket", "[papr][iuse]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_character( you, true );
    map &here = get_map();
    detached_ptr<item> holder = spawn_item( "molle_papr_blower_on" );
    item &blower = *holder;
    blower.activate();
    const int filter_before = blower.ammo_remaining_in_pocket( "papr_blower_filter" );
    const int battery_before = blower.ammo_remaining_in_pocket( "papr_blower_battery" );
    REQUIRE( filter_before > 0 );
    REQUIRE( battery_before > 0 );
    // CDF: fd_smoke gas_absorption_factor 12 (data/json/field_type.json:336); CDF's gas mask adds the bare factor each
    // tick and spends one charge at 100 (iuse.cpp gasmask), so 9 ticks (9 * 12 = 108) spend exactly one charge
    REQUIRE( fd_smoke.obj().gas_absorption_factor == 12 );
    REQUIRE( here.add_field( you.bub_pos(), fd_smoke, 1 ) );
    for( int i = 0; i < 9; i++ ) {
        iuse::papr_blower( &you, &blower, true, you.bub_pos() );
    }
    CHECK( blower.ammo_remaining_in_pocket( "papr_blower_filter" ) == filter_before - 1 );
    CHECK( blower.ammo_remaining_in_pocket( "papr_blower_battery" ) == battery_before );
}

// DDA: iuse.cpp:3748-3758
TEST_CASE( "papr_p2_empty_filter_turns_blower_off", "[papr][iuse]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_character( you, true );
    detached_ptr<item> holder = spawn_item( "molle_papr_blower_on" );
    item &blower = *holder;
    blower.activate();
    blower.ammo_consume_in_pocket( "papr_blower_filter", 1000, get_map(), you.bub_pos() );
    REQUIRE( blower.ammo_remaining_in_pocket( "papr_blower_filter" ) == 0 );
    REQUIRE( blower.is_active() );
    iuse::papr_blower( &you, &blower, true, you.bub_pos() );
    CHECK( !blower.is_active() );
}

// DDA: iuse.cpp:3613-3631 (papr_mask_activate), t == false here
TEST_CASE( "papr_p3_mask_activate_needs_blower", "[papr][iuse]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_character( you, true );
    item &mask = wear_it( you, "papr_half_mask" );

    iuse::papr_mask( &you, &mask, false, you.bub_pos() );
    CHECK( !mask.is_active() );
    CHECK( mask.get_var( "overwrite_env_resist", 0 ) == 0 );

    // the off-state blower does not carry the flag, so it does not count either
    item &off = wear_it( you, "molle_papr_blower_off" );
    iuse::papr_mask( &you, &mask, false, you.bub_pos() );
    CHECK( !mask.is_active() );
    take_off_it( you, off );

    wear_it( you, "molle_papr_blower_on" );
    iuse::papr_mask( &you, &mask, false, you.bub_pos() );
    CHECK( mask.is_active() );
    // CDF: papr_half_mask environmental_protection_with_filter 16 (cdf_papr.json, DDA tool_armor.json)
    CHECK( mask.get_var( "overwrite_env_resist", 0 ) == 16 );
}

// DDA: iuse.cpp:3695-3714 (papr_mask tick)
TEST_CASE( "papr_p4_mask_stops_without_blower", "[papr][iuse]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_character( you, true );
    item &mask = wear_it( you, "papr_half_mask" );
    item &blower = wear_it( you, "molle_papr_blower_on" );
    iuse::papr_mask( &you, &mask, false, you.bub_pos() );
    REQUIRE( mask.is_active() );
    REQUIRE( mask.get_var( "overwrite_env_resist", 0 ) == 16 );

    // with the blower worn the tick keeps the mask running
    iuse::papr_mask( &you, &mask, true, you.bub_pos() );
    CHECK( mask.is_active() );

    take_off_it( you, blower );
    iuse::papr_mask( &you, &mask, true, you.bub_pos() );
    CHECK( !mask.is_active() );
    CHECK( mask.get_var( "overwrite_env_resist", 0 ) == 0 );
}

// DDA: item_degrade.cpp:212-216 (rng( 0, 3999 ) - damage_ >= 0), iuse.cpp:3697-3703 and 3616-3621
TEST_CASE( "papr_p5_damaged_mask_leaks", "[papr][iuse]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_character( you, true );
    item &mask = wear_it( you, "papr_half_mask" );
    wear_it( you, "molle_papr_blower_on" );

    // an undamaged item always succeeds (rng( 0, 3999 ) >= 0)
    for( int i = 0; i < 50; i++ ) {
        REQUIRE( mask.activation_success() );
    }
    // CDF: itype.h damage_max_ = 4000, so 4000 is the largest damage and rng( 0, 3999 ) - 4000 < 0 every time
    mask.set_damage( 4000 );
    REQUIRE( mask.damage() == 4000 );
    for( int i = 0; i < 50; i++ ) {
        CHECK( !mask.activation_success() );
    }

    iuse::papr_mask( &you, &mask, false, you.bub_pos() );
    CHECK( !mask.is_active() );

    // a running mask whose damage grows leaks on the next tick and drops its resistance
    mask.set_damage( 0 );
    iuse::papr_mask( &you, &mask, false, you.bub_pos() );
    REQUIRE( mask.is_active() );
    REQUIRE( mask.get_var( "overwrite_env_resist", 0 ) == 16 );
    mask.set_damage( 4000 );
    iuse::papr_mask( &you, &mask, true, you.bub_pos() );
    CHECK( mask.get_var( "overwrite_env_resist", 0 ) == 0 );
}

// DDA: tool_armor.json molle_papr_blower_off / _on
TEST_CASE( "papr_p6_blower_data", "[papr][item]" )
{
    clear_all_state();
    REQUIRE( itype_id( "molle_papr_blower_off" ).is_valid() );
    REQUIRE( itype_id( "molle_papr_blower_on" ).is_valid() );
    const itype &off = itype_id( "molle_papr_blower_off" ).obj();
    const itype &on = itype_id( "molle_papr_blower_on" ).obj();
    for( const itype *t : { &off, &on } ) {
        const std::vector<const pocket_data *> wells = well_pockets( *t );
        REQUIRE( wells.size() == 2 );
        CHECK( wells[0]->pocket_id == "papr_blower_filter" );
        CHECK( wells[0]->default_magazine == itype_id( "gasfilter_med" ) );
        CHECK( wells[1]->pocket_id == "papr_blower_battery" );
        CHECK( wells[1]->default_magazine == itype_id( "heavy_plus_battery_cell" ) );
    }
    CHECK( !off.has_flag( flag_id( "PAPR_BLOWER" ) ) );
    CHECK( on.has_flag( flag_id( "PAPR_BLOWER" ) ) );
    // DDA: turns_per_charge 240, revert_to the off form (CDF: tool slot fields, itype.h)
    REQUIRE( on.tool != nullptr );
    CHECK( on.tool->turns_per_charge == 240 );
    REQUIRE( on.tool->revert_to.has_value() );
    CHECK( *on.tool->revert_to == itype_id( "molle_papr_blower_off" ) );
    CHECK( on.get_use( "PAPR_BLOWER" ) != nullptr );
}

// DDA: ammo_pouch.json modular_papr_strap
TEST_CASE( "papr_p7_strap_accepts_only_blowers", "[papr][item]" )
{
    clear_all_state();
    REQUIRE( itype_id( "modular_papr_strap" ).is_valid() );
    detached_ptr<item> strap = spawn_item( "modular_papr_strap" );
    // CDF: the strap covers the hip sub-locations (body_parts_sub.json leg_hip_l / leg_hip_r) and is sided
    CHECK( strap->covers( sub_bodypart_str_id( "leg_hip_l" ) ) );
    CHECK( strap->covers( sub_bodypart_str_id( "leg_hip_r" ) ) );
    CHECK( strap->is_sided() );
    CHECK( strap->has_flag( flag_id( "PALS_LARGE" ) ) );
    // CDF: unloaded blowers: a loaded one (920 g + 365 g filter + 1500 g cell) exceeds the strap's 2 kg (DDA data kept)
    CHECK( strap->can_contain( *item::spawn( itype_id( "molle_papr_blower_off" ), calendar::turn ) ).success() );
    CHECK( strap->can_contain( *item::spawn( itype_id( "molle_papr_blower_on" ), calendar::turn ) ).success() );
    // a filter and a battery fit the volume of the pocket but are not on its item list
    CHECK( !strap->can_contain( *spawn_item( "gasfilter_med" ) ).success() );
    CHECK( !strap->can_contain( *spawn_item( "heavy_plus_battery_cell" ) ).success() );
    CHECK( !strap->can_contain( *spawn_item( "papr_half_mask" ) ).success() );
}

// the kit and the carrier
TEST_CASE( "papr_p8_strap_attaches_and_kit_spawns", "[papr][item]" )
{
    clear_all_state();
    REQUIRE( itype_id( "load_bearing_vest" ).is_valid() );
    detached_ptr<item> vest = spawn_item( "load_bearing_vest" );
    // CDF: load_bearing_vest attach_molle size 8; PALS_LARGE counts 3 (tests/pch1_pouches_test.cpp)
    CHECK( attach_to_vest( *vest, "modular_papr_strap" ) );
    CHECK( vest->contents.get_additional_space_used() == 3 );

    std::vector<std::string> ids;
    for( const detached_ptr<item> &it : item_group::items_from( item_group_id( "molle_papr_kit" ) ) ) {
        ids.push_back( it->typeId().str() );
    }
    // DDA: gear.json molle_papr_kit = mask + blower + wrapped filter + strap (the wrapper holds the filter)
    CHECK( ids.size() == 4 );
    for( const char *want : { "military_papr_mask", "molle_papr_blower_off", "modular_papr_strap",
                              "wrapping_vacuumed" } ) {
        CAPTURE( want );
        CHECK( std::find( ids.begin(), ids.end(), want ) != ids.end() );
    }
}

// PAPR1b: the strap carries the blower, so its weight limit follows the LOADED blower (battery cell and
// filter inside), and the kit's blower spawns loaded (an unloaded kit blower is an obtainability bug)
TEST_CASE( "papr_p8b_strap_holds_the_loaded_blower_and_kit_blower_is_loaded", "[papr][item]" )
{
    clear_all_state();
    detached_ptr<item> strap_holder = spawn_item( "modular_papr_strap" );
    item &strap = *strap_holder;
    bool found_blower = false;
    for( detached_ptr<item> &it : item_group::items_from( item_group_id( "molle_papr_kit" ) ) ) {
        if( it->typeId() != itype_id( "molle_papr_blower_off" ) ) {
            continue;
        }
        found_blower = true;
        CHECK( it->ammo_remaining_in_pocket( "papr_blower_battery" ) > 0 );
        CHECK( it->ammo_remaining_in_pocket( "papr_blower_filter" ) > 0 );
        // the loaded blower is heavier than the old 2 kg strap limit
        REQUIRE( it->weight() > 2_kilogram );
        CHECK( strap.can_contain( *it ).success() );
    }
    CHECK( found_blower );
}

// amendment E4: off to on keeps the item active (CDF has no SPAWN_ACTIVE reader)
TEST_CASE( "papr_p9_turn_on_leaves_blower_active", "[papr][iuse]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_character( you, true );
    detached_ptr<item> holder = spawn_item( "molle_papr_blower_off" );
    item &blower = *holder;
    REQUIRE( blower.ammo_remaining_in_pocket( "papr_blower_battery" ) > 0 );
    REQUIRE( !blower.is_active() );
    const use_function *use = blower.get_use( "transform" );
    REQUIRE( use != nullptr );
    use->call( you, blower, false, you.bub_pos() );
    CHECK( blower.typeId() == itype_id( "molle_papr_blower_on" ) );
    CHECK( blower.is_active() );
}

// amendment E4: item.cpp:3457 must accept PAPR_MASK_ACTIVATE
TEST_CASE( "papr_p10_mask_info_shows_protection_when_active", "[papr][iteminfo]" )
{
    clear_all_state();
    for( const char *id : { "military_papr_mask", "papr_half_mask" } ) {
        CAPTURE( id );
        detached_ptr<item> mask = spawn_item( id );
        CHECK( mask->info_string( iteminfo_query::all, 1 ).find( "Protection when active" ) != std::string::npos );
    }
}
