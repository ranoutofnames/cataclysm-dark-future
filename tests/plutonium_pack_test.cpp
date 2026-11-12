// CDF: PLUT1. The plutonium fuel packs (plut_pack_small, plut_pack_large) and the three tools that
// take them (holo_cloak, dimensional_anchor, rm13_armor and the _on copies).
// DDA reference: item::reload into a magazine scales a plutonium cell by PLUTONIUM_CHARGES
// (DDA item_gun_tool_ammo.cpp:3526-3531); the unload gives whole cells back (DDA activity_actor.cpp:6476-6477).
#include <algorithm>
#include <string>
#include <vector>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "ammo.h"
#include "avatar_functions.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "crafting.h"
#include "game_constants.h"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "player_helpers.h"
#include "pocket_data.h"
#include "pocket_type.h"
#include "reload/reload.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"

namespace
{
const itype_id itype_plut_cell( "plut_cell" );
const itype_id itype_plut_cell_charge( "plut_cell_charge" );
const itype_id itype_plut_pack_small( "plut_pack_small" );
const itype_id itype_plut_pack_large( "plut_pack_large" );
const itype_id itype_plut_pack_medium( "plut_pack_medium" );

auto give_cells( avatar &you, int cells ) -> void
{
    you.i_add( item::spawn( itype_plut_cell, calendar::turn, cells ) );
}

auto give_pack( avatar &you, const itype_id &pack, int charges ) -> item &
{
    item &it = you.i_add( item::spawn( pack ) );
    if( charges > 0 ) {
        it.ammo_set( itype_plut_cell_charge, charges );
    }
    return it;
}

// the first cell stack the avatar carries
auto cell_stack( avatar &you ) -> item &
{
    const std::vector<item *> found = you.items_with( []( const item & it ) {
        // CDF: a cell stack lying in the open; a tool's own plut_cell contents are not a source
        return it.typeId() == itype_plut_cell && ( it.parent_item() == nullptr || !it.parent_item()->is_tool() );
    } );
    REQUIRE( !found.empty() );
    return *found.front();
}

auto charge_items_in( const item &pack ) -> std::vector<const item *>
{
    std::vector<const item *> ret;
    for( const item *it : pack.contents.all_items_top() ) {
        ret.push_back( it );
    }
    return ret;
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

// A tool with its pack taken out, so can_contain tests the restriction only.
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

} // namespace
// T1, T2
TEST_CASE( "plutonium_cells_load_into_a_pack_as_500_charges", "[plut][pocket][reload]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_avatar();

    SECTION( "two cells fill a small pack to 1000, as one charge item" ) {
        item &pack = give_pack( you, itype_plut_pack_small, 0 );
        REQUIRE( pack.ammo_remaining() == 0 );
        give_cells( you, 2 );
        REQUIRE( pack.reload( you, cell_stack( you ), 2 ) );
        // CDF: 2 cells * PLUTONIUM_CHARGES (500), DDA item_gun_tool_ammo.cpp:3526-3531
        CHECK( pack.ammo_remaining() == 1000 );
        const std::vector<const item *> inside = charge_items_in( pack );
        REQUIRE( inside.size() == 1 );
        CHECK( inside.front()->typeId() == itype_plut_cell_charge );
        CHECK( inside.front()->charges == 1000 );
        CHECK( you.charges_of( itype_plut_cell ) == 0 );
    }
    SECTION( "three cells into an empty small pack: two go in, one stays" ) {
        item &pack = give_pack( you, itype_plut_pack_small, 0 );
        give_cells( you, 3 );
        REQUIRE( pack.reload( you, cell_stack( you ), 3 ) );
        CHECK( pack.ammo_remaining() == 1000 );
        CHECK( you.charges_of( itype_plut_cell ) == 1 );
    }
    SECTION( "a pack at 700 charges loaded with one cell ends at 1000, the excess is wasted" ) {
        item &pack = give_pack( you, itype_plut_pack_small, 700 );
        REQUIRE( pack.ammo_remaining() == 700 );
        give_cells( you, 2 );
        REQUIRE( pack.reload( you, cell_stack( you ), 1 ) );
        CHECK( pack.ammo_remaining() == 1000 );
        CHECK( you.charges_of( itype_plut_cell ) == 1 );
    }
    SECTION( "a second load merges into the one charge item" ) {
        item &pack = give_pack( you, itype_plut_pack_large, 0 );
        give_cells( you, 4 );
        REQUIRE( pack.reload( you, cell_stack( you ), 1 ) );
        REQUIRE( pack.reload( you, cell_stack( you ), 2 ) );
        CHECK( pack.ammo_remaining() == 1500 );
        CHECK( charge_items_in( pack ).size() == 1 );
    }
}

// T3
TEST_CASE( "holo_cloak_spawns_with_a_500_charge_small_pack", "[plut][pocket][tool]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_avatar();
    item &cloak = you.i_add( item::spawn( itype_id( "holo_cloak" ) ) );
    // CDF: tool_armor.json holo_cloak initial_charges 500, charges_per_use 25
    REQUIRE( cloak.magazine_current() != nullptr );
    CHECK( cloak.magazine_current()->typeId() == itype_plut_pack_small );
    CHECK( cloak.ammo_remaining() == 500 );
    CHECK( cloak.ammo_sufficient( &you ) );
    cloak.ammo_consume( 25, tripoint_bub_ms::zero(), &you );
    CHECK( cloak.ammo_remaining() == 475 );
    cloak.ammo_set( itype_plut_cell_charge, 0 );
    CHECK( cloak.ammo_remaining() == 0 );
    CHECK_FALSE( cloak.ammo_sufficient( &you ) );
}

// T4
TEST_CASE( "rm13_armor_keeps_its_large_pack_when_switched_on", "[plut][pocket][tool]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_avatar();
    item &armor = you.i_add( item::spawn( itype_id( "rm13_armor" ) ) );
    // CDF: tool_armor.json rm13_armor initial_charges 5000, charges_per_use 5
    REQUIRE( armor.magazine_current() != nullptr );
    CHECK( armor.magazine_current()->typeId() == itype_plut_pack_large );
    CHECK( armor.ammo_remaining() == 5000 );
    armor.ammo_consume( 5, tripoint_bub_ms::zero(), &you );
    CHECK( armor.ammo_remaining() == 4995 );
    armor.convert( itype_id( "rm13_armor_on" ) );
    REQUIRE( armor.magazine_current() != nullptr );
    CHECK( armor.magazine_current()->typeId() == itype_plut_pack_large );
    CHECK( armor.ammo_remaining() == 4995 );

    // the _on copy has the well by copy-from
    const item *on = item::spawn_temporary( itype_id( "rm13_armor_on" ) );
    CHECK( wells_of( *on->type ).size() == 1 );
    CHECK( on->type->tool->max_charges == 0 );
    CHECK( on->ammo_remaining() == 5000 );
}

// T5
TEST_CASE( "dimensional_anchor_needs_a_charged_pack_to_switch_on", "[plut][pocket][tool][iuse]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_avatar();

    SECTION( "an empty pack: it does not switch on" ) {
        item &anchor = you.i_add( item::spawn( itype_id( "dimensional_anchor" ) ) );
        REQUIRE( anchor.magazine_current() != nullptr );
        // CDF: tool_armor.json dimensional_anchor initial_charges 1000
        REQUIRE( anchor.ammo_remaining() == 1000 );
        anchor.ammo_set( itype_plut_cell_charge, 0 );
        REQUIRE( anchor.ammo_remaining() == 0 );
        anchor.type->invoke( you, anchor, you.bub_pos() );
        CHECK( anchor.typeId() == itype_id( "dimensional_anchor" ) );
        // CDF: messages are unobservable in tests (fake_messages.cpp), so check the actor data that carries the text
        const use_function *use = anchor.type->get_use( "transform" );
        REQUIRE( use != nullptr );
        const iuse_transform *actor = dynamic_cast<const iuse_transform *>( use->get_actor_ptr() );
        REQUIRE( actor != nullptr );
        CHECK( actor->need_charges == 1 );
        CHECK( actor->need_charges_msg.translated().find( "needs power" ) != std::string::npos );
    }
    SECTION( "one charge: it switches on and keeps the pack" ) {
        item &anchor = you.i_add( item::spawn( itype_id( "dimensional_anchor" ) ) );
        anchor.ammo_set( itype_plut_cell_charge, 1 );
        REQUIRE( anchor.ammo_remaining() == 1 );
        anchor.type->invoke( you, anchor, you.bub_pos() );
        CHECK( anchor.typeId() == itype_id( "dimensional_anchor_on" ) );
        REQUIRE( anchor.magazine_current() != nullptr );
        CHECK( anchor.magazine_current()->typeId() == itype_plut_pack_small );
    }
}

// T6
TEST_CASE( "a loaded plutonium pack weighs and measures from the charge item data", "[plut][pocket]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_avatar();

    item &small_empty = give_pack( you, itype_plut_pack_small, 0 );
    item &small_full = give_pack( you, itype_plut_pack_small, 1000 );
    item &large_empty = give_pack( you, itype_plut_pack_large, 0 );
    item &large_full = give_pack( you, itype_plut_pack_large, 5000 );

    // CDF: the body is the copied light/medium atomic cell: 150 g / 100 ml and 750 g / 500 ml
    CHECK( small_empty.weight() == small_empty.type->weight );
    CHECK( large_empty.weight() == large_empty.type->weight );
    CHECK( small_empty.type->weight == 150_gram );
    CHECK( large_empty.type->weight == 750_gram );
    // CDF: one plut_cell is 80 g and 50 ml (250 ml per count 5); 1000 charges are 2 cells, 5000 are 10
    CHECK( small_full.weight() - small_empty.weight() == 160_gram );
    CHECK( large_full.weight() - large_empty.weight() == 800_gram );
    // CDF: the magazine pocket is rigid, so item_size_modifier is 0 and the cells add no volume (as any DDA
    // magazine). Decision for the owner: Option A (non-rigid pocket) would give +100 ml / +500 ml.
    CHECK( small_full.volume() == small_empty.volume() );
    CHECK( large_full.volume() == large_empty.volume() );
}

// T7
// CDF: PLUT1 re-port. A pack unloads through unload_activity_actor (P3-6-4a), which hands the whole
// plut_cell_charge stack back as charges / PLUTONIUM_CHARGES plut_cell, as DDA activity_actor.cpp:6476-6477.
TEST_CASE( "unloading a plutonium pack returns whole cells", "[plut][pocket][reload]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_avatar();

    SECTION( "1250 charges: two cells come out and the remainder goes with the stack" ) {
        item &pack = give_pack( you, itype_plut_pack_large, 1250 );
        REQUIRE( pack.ammo_remaining() == 1250 );
        unload_activity_actor::unload( you, pack );
        // CDF: 1250 / PLUTONIUM_CHARGES (500) = 2 cells (DDA divides the stack, the remainder is lost)
        CHECK( you.charges_of( itype_plut_cell ) == 2 );
        CHECK( pack.ammo_remaining() == 0 );
    }
    SECTION( "400 charges: no whole cell, none comes out" ) {
        item &pack = give_pack( you, itype_plut_pack_small, 400 );
        REQUIRE( pack.ammo_remaining() == 400 );
        unload_activity_actor::unload( you, pack );
        CHECK( you.charges_of( itype_plut_cell ) == 0 );
        CHECK( pack.ammo_remaining() == 0 );
        // CDF: nothing at all is handed over, not even a zero-count plut_cell stack
        const std::vector<item *> held = you.items_with( []( const item & it ) {
            return it.typeId() == itype_plut_cell;
        } );
        CHECK( held.empty() );
        CHECK( charge_items_in( pack ).empty() );
    }
    SECTION( "avatar_funcs::unload_item with bypass_activity takes the same path" ) {
        item &pack = give_pack( you, itype_plut_pack_large, 1000 );
        REQUIRE( avatar_funcs::unload_item( you, pack, true ) );
        CHECK( you.charges_of( itype_plut_cell ) == 2 );
        CHECK( pack.ammo_remaining() == 0 );
    }
}

// T8
TEST_CASE( "disassembling a holo_cloak drops its pack with the charge inside", "[plut][pocket][crafting]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_avatar();
    detached_ptr<item> cloak_ptr = item::spawn( itype_id( "holo_cloak" ) );
    item &cloak = *cloak_ptr;
    REQUIRE( cloak.ammo_remaining() == 500 );
    remove_ammo( cloak, you );

    const std::vector<item *> packs = you.items_with( []( const item & it ) {
        return it.typeId() == itype_plut_pack_small;
    } );
    REQUIRE( packs.size() == 1 );
    CHECK( packs.front()->ammo_remaining() == 500 );
    CHECK( you.charges_of( itype_plut_cell ) == 0 );
    // the one charge item is inside the pack; none lies loose
    const std::vector<item *> charge_stacks = you.items_with( []( const item & it ) {
        return it.typeId() == itype_plut_cell_charge && it.parent_item() == nullptr;
    } );
    CHECK( charge_items_in( *packs.front() ).size() == 1 );
    CHECK( charge_stacks.empty() );
}

// T9
TEST_CASE( "plutonium wells take their pack and nothing else", "[plut][pocket]" )
{
    clear_all_state();
    const item *cloak = empty_tool( "holo_cloak" );
    const item *anchor = empty_tool( "dimensional_anchor" );
    const item *rm13 = empty_tool( "rm13_armor" );
    CHECK( accepts( *cloak, "plut_pack_small" ) );
    CHECK( accepts( *anchor, "plut_pack_small" ) );
    CHECK( accepts( *rm13, "plut_pack_large" ) );
    CHECK_FALSE( accepts( *cloak, "medium_battery_cell" ) );
    CHECK_FALSE( accepts( *cloak, "plut_pack_large" ) );
    // the 8 L carry pocket of rm13_armor takes any small item, so only its pack well is checked
    const std::vector<const pocket_data *> rm13_wells = wells_of( *rm13->type );
    REQUIRE( rm13_wells.size() == 1 );
    CHECK( rm13_wells.front()->item_id_restriction.contains( itype_plut_pack_large ) );
    CHECK_FALSE( rm13_wells.front()->item_id_restriction.contains( itype_plut_pack_small ) );
    CHECK_FALSE( accepts( *anchor, "medium_atomic_battery_cell" ) );
    // and the other way round: a battery well does not take a pack
    const item *laptop = empty_tool( "control_laptop" );
    CHECK( accepts( *laptop, "medium_battery_cell" ) );
    CHECK_FALSE( accepts( *laptop, "plut_pack_small" ) );
    CHECK_FALSE( accepts( *laptop, "plut_pack_large" ) );
}

// T10' is gone: CDF: REACTORPACK1 (ruled edit, conversion rule 7).  The minireactor item is no
// longer an integral plutonium magazine; its well, pack and vehicle path are pinned by
// tests/vehicle_reactor_pack_test.cpp K1, K4 and K6.

// T11
TEST_CASE( "plutonium constants and the ammo type default are unchanged", "[plut][pocket]" )
{
    clear_all_state();
    // CDF: game_constants.h:80 and ammo_types.json plutonium default
    CHECK( PLUTONIUM_CHARGES == 500 );
    CHECK( ammotype( "plutonium" )->default_ammotype() == itype_plut_cell );
    CHECK( itype_plut_cell->count_by_charges() );
    CHECK( itype_plut_cell_charge->count_by_charges() );
    CHECK( itype_plut_pack_small->magazine->capacity == 1000 );
    CHECK( itype_plut_pack_large->magazine->capacity == 5000 );
    CHECK( itype_plut_pack_small->magazine->default_ammo == itype_plut_cell_charge );
}

// T12
TEST_CASE( "the reload selection offers whole cells only up to the pack's room", "[plut][pocket][reload]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_avatar();

    give_cells( you, 20 );
    item &small_pack = give_pack( you, itype_plut_pack_small, 0 );
    item &large_pack = give_pack( you, itype_plut_pack_large, 0 );
    item &cells = cell_stack( you );

    item_reload_option small_opt( &you, &small_pack, &small_pack, cells );
    small_opt.qty( 100 );
    CHECK( small_opt.qty() == 2 );

    item_reload_option large_opt( &you, &large_pack, &large_pack, cells );
    large_opt.qty( 100 );
    CHECK( large_opt.qty() == 10 );

    // CDF: a room that is not a multiple of 500 rounds up to a whole cell (the excess is wasted
    // by the reload): 250 charges in a small pack leave room for 750, offered as 2 cells.
    item &part_pack = give_pack( you, itype_plut_pack_small, 250 );
    REQUIRE( part_pack.ammo_remaining() == 250 );
    item_reload_option part_opt( &you, &part_pack, &part_pack, cells );
    part_opt.qty( 100 );
    CHECK( part_opt.qty() == 2 );
}

// T13 (audit amendment): an explicit pocket list suppresses the synthesized garment pocket
// (item_factory.cpp:1031), so rm13_armor must carry its 8 L container pocket explicitly.
TEST_CASE( "rm13_armor keeps its 8 L carry pocket", "[plut][pocket][armor]" )
{
    clear_all_state();
    for( const char *id : { "rm13_armor", "rm13_armor_on" } ) {
        CAPTURE( id );
        detached_ptr<item> armor = item::spawn( itype_id( id ) );
        CHECK( armor->get_storage() == 8000_ml );
        // CDF: AMENDMENT-v3 pocket list: MAGAZINE_WELL, MOD, CONTAINER, in that order
        const std::vector<pocket_data> &pockets = armor->type->pockets;
        REQUIRE( pockets.size() == 3 );
        CHECK( pockets[0].type == pocket_type::MAGAZINE_WELL );
        CHECK( pockets[1].type == pocket_type::MOD );
        CHECK( pockets[2].type == pocket_type::CONTAINER );
    }
}

// U1
TEST_CASE( "phase_immersion_suit_spawns_with_a_1000_charge_medium_pack", "[plut][pocket][tool][armor]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_avatar();
    item &suit = you.i_add( item::spawn( itype_id( "phase_immersion_suit" ) ) );
    REQUIRE( suit.magazine_current() != nullptr );
    // CDF: tool_armor.json phase_immersion_suit initial_charges 1000, pack plut_pack_medium (2500)
    CHECK( suit.magazine_current()->typeId() == itype_plut_pack_medium );
    CHECK( suit.ammo_remaining() == 1000 );
    suit.ammo_consume( 1, tripoint_bub_ms::zero(), &you );
    CHECK( suit.ammo_remaining() == 999 );
}

// U2
TEST_CASE( "phase_immersion_suit_switches_on_only_with_charge_and_keeps_the_pack", "[plut][pocket][tool][iuse]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_avatar();

    SECTION( "a 0-charge pack: the minimal power message and no transform" ) {
        item &suit = you.i_add( item::spawn( itype_id( "phase_immersion_suit" ) ) );
        suit.ammo_set( itype_plut_cell_charge, 0 );
        REQUIRE( suit.ammo_remaining() == 0 );
        suit.type->invoke( you, suit, you.bub_pos() );
        CHECK( suit.typeId() == itype_id( "phase_immersion_suit" ) );
        // CDF: messages are unobservable in tests (fake_messages.cpp), so check the actor data that carries the text
        const use_function *use = suit.type->get_use( "transform" );
        REQUIRE( use != nullptr );
        const iuse_transform *actor = dynamic_cast<const iuse_transform *>( use->get_actor_ptr() );
        REQUIRE( actor != nullptr );
        CHECK( actor->need_charges == 1 );
        CHECK( actor->need_charges_msg.translated().find( "minimal power" ) != std::string::npos );
    }
    SECTION( "one charge: it transforms to the _on form and keeps the pack" ) {
        item &suit = you.i_add( item::spawn( itype_id( "phase_immersion_suit" ) ) );
        suit.ammo_set( itype_plut_cell_charge, 1 );
        REQUIRE( suit.ammo_remaining() == 1 );
        suit.type->invoke( you, suit, you.bub_pos() );
        CHECK( suit.typeId() == itype_id( "phase_immersion_suit_on" ) );
        REQUIRE( suit.magazine_current() != nullptr );
        CHECK( suit.magazine_current()->typeId() == itype_plut_pack_medium );
        CHECK( suit.ammo_remaining() == 1 );
    }
}

// U3
TEST_CASE( "a medium plutonium pack takes five cells and no more", "[plut][pocket][reload]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_avatar();

    SECTION( "five cells fill it to 2500 and none are left" ) {
        item &pack = give_pack( you, itype_plut_pack_medium, 0 );
        give_cells( you, 5 );
        REQUIRE( pack.reload( you, cell_stack( you ), 5 ) );
        // CDF: 5 cells * PLUTONIUM_CHARGES (500) = 2500, the medium pack capacity
        CHECK( pack.ammo_remaining() == 2500 );
        CHECK( you.charges_of( itype_plut_cell ) == 0 );
    }
    SECTION( "six cells into an empty medium pack: five go in, one stays" ) {
        item &pack = give_pack( you, itype_plut_pack_medium, 0 );
        give_cells( you, 6 );
        REQUIRE( pack.reload( you, cell_stack( you ), 6 ) );
        CHECK( pack.ammo_remaining() == 2500 );
        CHECK( you.charges_of( itype_plut_cell ) == 1 );
    }
}

// U4
TEST_CASE( "the medium pack fits the suit's well and the small and large packs do not", "[plut][pocket][tool]" )
{
    clear_all_state();
    const item *suit = empty_tool( "phase_immersion_suit" );
    CHECK( accepts( *suit, "plut_pack_medium" ) );
    // the 8 L carry pocket takes the small and large packs too, so only the well's restriction is checked
    const std::vector<const pocket_data *> wells = wells_of( *suit->type );
    REQUIRE( wells.size() == 1 );
    CHECK( wells.front()->item_id_restriction.contains( itype_plut_pack_medium ) );
    CHECK_FALSE( wells.front()->item_id_restriction.contains( itype_plut_pack_small ) );
    CHECK_FALSE( wells.front()->item_id_restriction.contains( itype_plut_pack_large ) );
    CHECK_FALSE( accepts( *empty_tool( "holo_cloak" ), "plut_pack_medium" ) );
}

// U5
TEST_CASE( "phase_immersion_suit keeps its 8 L carry pocket", "[plut][pocket][armor]" )
{
    clear_all_state();
    for( const char *id : { "phase_immersion_suit", "phase_immersion_suit_on" } ) {
        CAPTURE( id );
        detached_ptr<item> suit = item::spawn( itype_id( id ) );
        // CDF: p3_golden.json phase_immersion_suit CONTAINER carry 8 L
        CHECK( suit->get_storage() == 8000_ml );
        // CDF: explicit pocket list (no synthesized garment pocket): MAGAZINE_WELL, MOD, CONTAINER, as rm13_armor
        const std::vector<pocket_data> &pockets = suit->type->pockets;
        REQUIRE( pockets.size() == 3 );
        CHECK( pockets[0].type == pocket_type::MAGAZINE_WELL );
        CHECK( pockets[1].type == pocket_type::MOD );
        CHECK( pockets[2].type == pocket_type::CONTAINER );
    }
}
