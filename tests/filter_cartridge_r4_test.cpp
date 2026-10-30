// FILT1 / R4 amendment step 4b: the AUTO-8 tools take DDA magazine wells (cases A1-A6).
// CDF: owner ruling 2026-10-31: dimensional_anchor(_on), holo_cloak and rm13_armor(_on) keep their
// integral plutonium pocket (no plutonium magazine item exists), so 18 tools take wells and
// 5 ids (3 tools + 2 _on variants) are asserted as still integral.
// Spec: planning\outbox\PL-06f\R4-amendment.md sections 2-4. DDA wells: b4b62254ae2
// (tool_armor.json, custom_headgear.json, items_holiday.json, rm13_combat_armor.json,
// mods/aftershock_exoplanet/items/tools.json). CDF-authored test.
#include <algorithm>
#include <list>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"
#include "iuse.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "pocket_data.h"
#include "pocket_type.h"
#include "state_helpers.h"
#include "type_id.h"

namespace
{
// One well of a tool: either a flag restriction or an item restriction.
struct well_expect {
    bool by_flag;
    std::string value;
    std::string default_magazine;
};

struct tool_expect {
    std::string id;
    std::vector<well_expect> wells;
    int initial_charges; // CDF initial_charges (mask_h20survivor: 180, see A4)
};

// CDF: owner ruling 2026-10-31: plutonium kept; id, CDF max_charges, CDF initial_charges.
struct integral_expect {
    std::string id;
    int max_charges;
    int initial_charges;
};

const std::vector<integral_expect> &plutonium_integral()
{
    static const std::vector<integral_expect> v = {
        { "dimensional_anchor", 1000, 1000 },
        { "dimensional_anchor_on", 1000, 1000 },
        { "holo_cloak", 1000, 500 },
        { "rm13_armor", 5000, 5000 },
        { "rm13_armor_on", 5000, 5000 },
    };
    return v;
}

const well_expect med_batt{ true, "BATTERY_MEDIUM", "medium_battery_cell" };
const well_expect gf_sm{ true, "GASFILTER_SM", "gasfilter_sm" };
const well_expect gf_med{ true, "GASFILTER_MED", "gasfilter_med" };
const well_expect rebreath{ true, "REBREATHER_CART", "rebreather_cartridge_o2" };

// R4 amendment section 2, minus the plutonium tools (owner ruling 2026-10-31).
const std::vector<tool_expect> &the_18()
{
    static const std::vector<tool_expect> v = {
        { "control_laptop", { med_batt }, 500 },
        { "control_laptop_screen_lit", { med_batt }, 500 },
        { "game_watch", { { true, "BATTERY_ULTRA_LIGHT", "light_minus_battery_cell" } }, 80 },
        { "helmet_scavenger", { gf_sm }, 100 },
        { "mask_filter", { gf_sm }, 100 },
        { "mask_fsurvivor", { gf_med }, 100 },
        { "mask_gas", { gf_med }, 100 },
        { "mask_gas_xl", { gf_med }, 100 },
        { "mask_h20survivor", { rebreath }, 180 },
        { "mask_h20survivor_on", { rebreath }, 180 },
        { "mask_hsurvivor", { gf_med }, 100 },
        { "mask_lsurvivor", { gf_med }, 100 },
        { "mask_survivor", { gf_med }, 100 },
        { "plastic_jack_o_lantern", { { true, "BATTERY_LIGHT", "light_battery_cell" } }, 100 },
        { "rebreather", { rebreath }, 60 },
        { "rebreather_on", { rebreath }, 60 },
        { "rebreather_xl", { rebreath }, 60 },
        { "rebreather_xl_on", { rebreath }, 60 },
    };
    return v;
}

auto wells_of( const itype &type ) -> std::vector<const pocket_data *>
{
    std::vector<const pocket_data *> ret;
    for( const pocket_data &p : type.pockets ) {
        if( p.type == pocket_type::MAGAZINE_WELL ) {
            ret.push_back( &p );
        }
    }
    return ret;
}

auto well_matches( const pocket_data &p, const well_expect &e ) -> bool
{
    if( e.by_flag ) {
        return p.get_flag_restrictions().size() == 1 &&
               p.get_flag_restrictions().contains( flag_id( e.value ) );
    }
    return p.item_id_restriction.size() == 1 &&
           p.item_id_restriction.contains( itype_id( e.value ) );
}

auto default_matches( const pocket_data &p, const well_expect &e ) -> bool
{
    return p.default_magazine == itype_id( e.default_magazine );
}

// A fresh tool with every magazine taken out, so the wells are empty and can_contain tests the
// restriction only (a spawned tool carries its default cartridge).
auto empty_tool( const std::string &id ) -> item *
{
    item *tool = item::spawn_temporary( itype_id( id ) );
    tool->contents.clear_magazines();
    return tool;
}

auto accepts( const item &tool, const std::string &mag ) -> bool
{
    const item *m = item::spawn_temporary( itype_id( mag ) );
    return tool.can_contain( *m ).success();
}

auto well_accepts( const item &tool, size_t well, const std::string &mag ) -> bool
{
    const std::vector<const item_pocket *> wells = tool.contents.get_pockets( pocket_type::MAGAZINE_WELL );
    REQUIRE( well < wells.size() );
    const item *m = item::spawn_temporary( itype_id( mag ) );
    return wells[well]->can_contain( *m ).success();
}
} // namespace

TEST_CASE( "r4_wells_load_for_all_18_and_plutonium_stays_integral", "[filt1][r4]" )
{
    clear_all_state();
    REQUIRE( the_18().size() == 18 );
    for( const tool_expect &e : the_18() ) {
        CAPTURE( e.id );
        REQUIRE( itype_id( e.id ).is_valid() );
        const item *tool = item::spawn_temporary( itype_id( e.id ) );
        const std::vector<const pocket_data *> wells = wells_of( *tool->type );
        REQUIRE( wells.size() == e.wells.size() );
        for( size_t i = 0; i < wells.size(); ++i ) {
            CAPTURE( i );
            CHECK( well_matches( *wells[i], e.wells[i] ) );
            CHECK( default_matches( *wells[i], e.wells[i] ) );
        }
        REQUIRE( tool->type->tool );
        // the integral pocket is gone: wells only.
        // CDF: `tool_ammo` stays as in DDA; the item constructor loads the default magazine only
        // for a tool with a non-empty ammo_types() and a `magazines` default (item.cpp:391, 9460).
        CHECK( tool->type->tool->max_charges == 0 );
        CHECK_FALSE( tool->type->tool->ammo_id.empty() );
        CHECK_FALSE( tool->contents.has_pocket_type( pocket_type::MAGAZINE ) );
    }
    // CDF: owner ruling 2026-10-31: the plutonium tools are still integral, no well.
    for( const integral_expect &e : plutonium_integral() ) {
        CAPTURE( e.id );
        REQUIRE( itype_id( e.id ).is_valid() );
        const item *tool = item::spawn_temporary( itype_id( e.id ) );
        REQUIRE( tool->type->tool );
        CHECK( wells_of( *tool->type ).empty() );
        CHECK( tool->type->tool->max_charges == e.max_charges );
        CHECK( tool->type->tool->ammo_id.contains( ammotype( "plutonium" ) ) );
        CHECK( tool->contents.has_pocket_type( pocket_type::MAGAZINE ) );
    }
}

TEST_CASE( "r4_gas_mask_takes_filter_not_battery", "[filt1][r4]" )
{
    clear_all_state();
    const item *mask = empty_tool( "mask_gas" );
    CHECK( accepts( *mask, "gasfilter_med" ) );
    CHECK_FALSE( accepts( *mask, "medium_battery_cell" ) );
    CHECK_FALSE( accepts( *mask, "gasfilter_sm" ) );

    const item *cowl = empty_tool( "helmet_scavenger" );
    CHECK( accepts( *cowl, "gasfilter_sm" ) );
    CHECK_FALSE( accepts( *cowl, "gasfilter_med" ) );

    // and a laptop does not take a canister
    const item *laptop = empty_tool( "control_laptop" );
    CHECK( accepts( *laptop, "medium_battery_cell" ) );
    CHECK_FALSE( accepts( *laptop, "gasfilter_med" ) );
}

TEST_CASE( "r4_rebreather_takes_cartridge", "[filt1][r4]" )
{
    clear_all_state();
    for( const std::string id : { "rebreather", "rebreather_xl", "mask_h20survivor" } ) {
        CAPTURE( id );
        const item *tool = empty_tool( id );
        CHECK( accepts( *tool, "rebreather_cartridge_o2" ) );
        CHECK( accepts( *tool, "rebreather_cartridge_air" ) );
        // CDF: the amendment (A3) wants the regenerator refused, but the well is DDA's flag
        // REBREATHER_CART and DDA gives the regenerator that flag (filter.json:96), so it fits.
    }
    // CDF: owner ruling 2026-10-31: rm13_armor keeps its integral plutonium pocket (was: well 1
    // rm13_battery, well 2 regenerator, plut_cell refused by both).
    const item *rm13 = item::spawn_temporary( itype_id( "rm13_armor" ) );
    CHECK( wells_of( *rm13->type ).empty() );
    CHECK( rm13->type->tool->ammo_id.contains( ammotype( "plutonium" ) ) );
}

TEST_CASE( "r4_spawn_with_default_magazine", "[filt1][r4]" )
{
    clear_all_state();
    for( const tool_expect &e : the_18() ) {
        CAPTURE( e.id );
        const item *tool = item::spawn_temporary( itype_id( e.id ) );
        // CDF: initial_charges; mask_h20survivor had none (spawned empty): ORCH-CONFIRM 2 follows
        // DDA, which loads the default cartridge, so it now has initial_charges 180 (the o2
        // cartridge capacity).
        CHECK( tool->ammo_remaining() == e.initial_charges );
        CHECK_FALSE( tool->magazines_current().empty() );
        // the default cartridge the well names (DDA default_magazine)
        const std::vector<const pocket_data *> wells = wells_of( *tool->type );
        REQUIRE( wells.size() == e.wells.size() );
        for( size_t i = 0; i < wells.size(); ++i ) {
            CHECK( default_matches( *wells[i], e.wells[i] ) );
        }
    }
    const item *mask = item::spawn_temporary( itype_id( "mask_gas" ) );
    CHECK( mask->ammo_remaining() == 100 );
    REQUIRE( mask->magazines_current().size() == 1 );
    CHECK( mask->magazines_current().front()->typeId() == itype_id( "gasfilter_med" ) );
    // CDF: owner ruling 2026-10-31: the plutonium tools spawn with their CDF initial_charges in the
    // integral pocket (was: rm13_armor default magazine rm13_battery).
    for( const integral_expect &e : plutonium_integral() ) {
        CAPTURE( e.id );
        const item *tool = item::spawn_temporary( itype_id( e.id ) );
        CHECK( tool->ammo_remaining() == e.initial_charges );
    }
}

TEST_CASE( "r4_gas_mask_runs_on_a_cartridge", "[filt1][r4]" )
{
    clear_all_state();
    clear_map();
    avatar &you = get_avatar();
    clear_avatar();
    detached_ptr<item> mask_ptr = item::spawn( itype_id( "mask_gas" ) );
    item &mask = *mask_ptr;
    REQUIRE( mask.ammo_remaining() == 100 );
    // the filter is a cartridge in the mask's well, not integral charges
    REQUIRE( mask.magazines_current().size() == 1 );
    REQUIRE( mask.contents.has_pocket_type( pocket_type::MAGAZINE_WELL ) );
    you.wear_item( std::move( mask_ptr ), false );
    REQUIRE( you.is_worn( mask ) );
    const tripoint_bub_ms pos = you.bub_pos();

    // activation with a loaded cartridge succeeds
    REQUIRE_FALSE( mask.is_active() );
    iuse::gasmask( &you, &mask, false, pos );
    CHECK( mask.is_active() );

    // gas absorbed >= 100 consumes one charge of the cartridge
    mask.set_var( "gas_absorbed", 100 );
    iuse::gasmask( &you, &mask, true, pos );
    CHECK( mask.ammo_remaining() == 99 );

    // an empty cartridge: activation says there is no filter
    mask.deactivate();
    mask.ammo_unset(); // the cartridge stays in the well, empty
    REQUIRE( mask.magazines_current().size() == 1 );
    REQUIRE( mask.ammo_remaining() == 0 );
    iuse::gasmask( &you, &mask, false, pos );
    CHECK_FALSE( mask.is_active() );
}

TEST_CASE( "r4_variants_inherit_wells", "[filt1][r4]" )
{
    clear_all_state();
    const std::vector<std::pair<std::string, std::string>> pairs = {
        { "control_laptop", "control_laptop_screen_lit" },
        { "mask_h20survivor", "mask_h20survivor_on" },
        { "rebreather", "rebreather_on" },
        { "rebreather_xl", "rebreather_xl_on" },
        { "mask_gas", "mask_gas_xl" },
    };
    for( const auto &pr : pairs ) {
        CAPTURE( pr.first, pr.second );
        const item *base = item::spawn_temporary( itype_id( pr.first ) );
        const item *var = item::spawn_temporary( itype_id( pr.second ) );
        const std::vector<const pocket_data *> bw = wells_of( *base->type );
        const std::vector<const pocket_data *> vw = wells_of( *var->type );
        REQUIRE( bw.size() >= 1 );
        REQUIRE( vw.size() == bw.size() );
        for( size_t i = 0; i < bw.size(); ++i ) {
            CHECK( bw[i]->get_flag_restrictions() == vw[i]->get_flag_restrictions() );
            CHECK( bw[i]->item_id_restriction == vw[i]->item_id_restriction );
            CHECK( bw[i]->default_magazine == vw[i]->default_magazine );
        }
    }

    // turning a rebreather on and back keeps the loaded cartridge (revert_to)
    detached_ptr<item> on_ptr = item::spawn( itype_id( "rebreather_on" ) );
    item &on = *on_ptr;
    REQUIRE_FALSE( on.magazines_current().empty() );
    const int before = on.ammo_remaining();
    REQUIRE( before > 0 );
    on.convert( itype_id( "rebreather" ) );
    CHECK( on.typeId() == itype_id( "rebreather" ) );
    CHECK_FALSE( on.magazines_current().empty() );
    CHECK( on.ammo_remaining() == before );
}
