// CDF: RM13PACK1 (PL-51 PART1 section H, AUTO-234 item 8). The RM13 combat armor keeps its
// plutonium fuel pack and gains a battery buffer well: a relic recharge entry of the new CDF type
// `transfer` moves charges from the pack into the battery every second, so the suit draws on the
// pack while it has fuel and on the battery afterwards. CDF numbers stay: pack 5000, battery 5000,
// a plutonium cell is 500 charges. DDA shape reference: data/json/items/armor/rm13_combat_armor.json
// (two named wells), but DDA has no transfer type and no generator; the mechanism is CDF's
// (src/relic.cpp, PL-51 PART1 section B).
#include <sstream>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "debug.h"
#include "item.h"
#include "item_group.h"
#include "itype.h"
#include "json.h"
#include "pocket_data.h"
#include "pocket_type.h"
#include "player_helpers.h"
#include "relic.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"

namespace
{
const itype_id itype_rm13_armor( "rm13_armor" );
const itype_id itype_rm13_armor_on( "rm13_armor_on" );
const itype_id itype_rm13_battery( "rm13_battery" );
const itype_id itype_plut_pack_large( "plut_pack_large" );
const itype_id itype_plut_cell_charge( "plut_cell_charge" );
const itype_id itype_battery( "battery" );

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

auto loaded_mag( item &host, const itype_id &id ) -> item *
{
    for( item *mag : host.magazines_current() ) {
        if( mag->typeId() == id ) {
            return mag;
        }
    }
    return nullptr;
}

// CDF: a bare spawn loads only the first well through initial_charges (item::spawn); the battery
// well stays empty. Dress every well the way the spawn sites do, then set the two charge levels a
// case needs (0 empties the magazine).
auto load_suit( avatar &you, const itype_id &id, int pack, int battery ) -> item &
{
    item &suit = you.i_add( item::spawn( id ) );
    suit.dress_magazine_wells( true, true );
    item *pack_mag = loaded_mag( suit, itype_plut_pack_large );
    REQUIRE( pack_mag != nullptr );
    pack_mag->ammo_set( itype_plut_cell_charge, pack );
    item *batt_mag = loaded_mag( suit, itype_rm13_battery );
    REQUIRE( batt_mag != nullptr );
    batt_mag->ammo_set( itype_battery, battery );
    return suit;
}

auto transfer_entry( item &host ) -> relic_recharge
{
    const std::vector<relic_recharge> scheme = host.get_relic_recharge_scheme();
    for( const relic_recharge &rech : scheme ) {
        if( rech.type == relic_recharge_type::transfer ) {
            return rech;
        }
    }
    FAIL( "no transfer recharge entry on " << host.typeId().str() );
    return relic_recharge();
}
} // namespace

// B1
TEST_CASE( "rm13_pack_b1_two_wells_and_the_transfer_entry", "[rm13pack1][p3][relic][plut][pocket]" )
{
    clear_all_state();
    for( const itype_id &id : { itype_rm13_armor, itype_rm13_armor_on } ) {
        CAPTURE( id.str() );
        const itype &type = id.obj();
        const std::vector<const pocket_data *> wells = wells_of( type );
        REQUIRE( wells.size() == 2 );
        CHECK( wells[0]->item_id_restriction.contains( itype_plut_pack_large ) );
        CHECK( wells[0]->default_magazine == itype_plut_pack_large );
        CHECK( wells[1]->item_id_restriction.contains( itype_rm13_battery ) );
        CHECK( wells[1]->default_magazine == itype_rm13_battery );
        CHECK( wells[1]->rigid );
        // CDF: AMENDMENT-v3 pocket order with the battery well after the pack well
        REQUIRE( type.pockets.size() == 4 );
        CHECK( type.pockets[0].type == pocket_type::MAGAZINE_WELL );
        CHECK( type.pockets[1].type == pocket_type::MAGAZINE_WELL );
        CHECK( type.pockets[2].type == pocket_type::MOD );
        CHECK( type.pockets[3].type == pocket_type::CONTAINER );
        // CDF: tool_armor.json rm13_armor storage 8 L
        CHECK( type.pockets[3].raw_volume_capacity == 8000_ml );
        REQUIRE( type.tool );
        CHECK( type.tool->ammo_id.contains( ammotype( "plutonium" ) ) );
        CHECK( type.tool->ammo_id.contains( ammotype( "battery" ) ) );

        REQUIRE( type.relic_data );
        const std::vector<relic_recharge> scheme = type.relic_data->get_recharge_scheme();
        int transfers = 0;
        for( const relic_recharge &rech : scheme ) {
            if( rech.type != relic_recharge_type::transfer ) {
                continue;
            }
            ++transfers;
            CHECK( rech.from_ammo == ammotype( "plutonium" ) );
            CHECK( rech.to_ammo == ammotype( "battery" ) );
            CHECK( rech.rate == 1 );
            CHECK( rech.cost == 1 );
            CHECK( rech.interval == 1_seconds );
        }
        CHECK( transfers == 1 );
    }
}

// B2
TEST_CASE( "rm13_pack_b2_spawn_fills_both_wells", "[rm13pack1][p3][relic][plut][pocket]" )
{
    clear_all_state();
    Item_modifier modifier;
    // CDF: the item group knobs the spawn sites use: a certain default magazine and its ammo
    modifier.with_magazine = 100;
    modifier.with_ammo = 100;
    detached_ptr<item> suit;
    const std::string dmsg = capture_debugmsg_during( [&]() {
        suit = modifier.modify( item::spawn( itype_rm13_armor ) );
    } );
    REQUIRE( suit );
    CHECK( dmsg.empty() );
    const std::vector<item *> mags = suit->magazines_current();
    REQUIRE( mags.size() == 2 );
    CHECK( mags[0]->typeId() == itype_plut_pack_large );
    CHECK( mags[0]->ammo_current() == itype_plut_cell_charge );
    // CDF: plut_pack_large capacity 5000 (plutonium_packs.json)
    CHECK( mags[0]->ammo_remaining() == 5000 );
    CHECK( mags[1]->typeId() == itype_rm13_battery );
    CHECK( mags[1]->ammo_current() == itype_battery );
    // CDF: rm13_battery capacity 5000 (tool_armor.json)
    CHECK( mags[1]->ammo_remaining() == 5000 );
}

// B3
TEST_CASE( "rm13_pack_b3_transfer_moves_one_charge_per_second", "[rm13pack1][p3][relic][plut][pocket]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_avatar();
    item &suit = load_suit( you, itype_rm13_armor, 5000, 100 );
    const relic_recharge &rech = transfer_entry( suit );
    item *pack = loaded_mag( suit, itype_plut_pack_large );
    item *batt = loaded_mag( suit, itype_rm13_battery );
    REQUIRE( pack != nullptr );
    REQUIRE( batt != nullptr );

    calendar::turn += 1_turns;
    // CDF: the path item::process takes: process_recharge must not stop at the full check, because
    // with two wells ammo_remaining() (5100) is the sum and ammo_capacity() (5000) is the pack's.
    relic_funcs::process_recharge( suit, &you );
    CHECK( pack->ammo_remaining() == 4999 );
    CHECK( batt->ammo_remaining() == 101 );

    // CDF: 4899 more entry calls reach 4900 transfers: the battery is full, the pack is down 4900
    for( int i = 0; i < 4899; ++i ) {
        calendar::turn += 1_turns;
        relic_funcs::process_recharge_entry( suit, rech, &you );
    }
    CHECK( batt->ammo_remaining() == 5000 );
    CHECK( pack->ammo_remaining() == 100 );

    // a full battery has no room: nothing moves any more
    calendar::turn += 1_turns;
    CHECK_FALSE( relic_funcs::process_recharge_entry( suit, rech, &you ) );
    CHECK( pack->ammo_remaining() == 100 );
    CHECK( batt->ammo_remaining() == 5000 );
}

// B4
TEST_CASE( "rm13_pack_b4_no_transfer_without_fuel_and_the_battery_drains", "[rm13pack1][p3][relic][plut][pocket]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_avatar();
    item &suit = load_suit( you, itype_rm13_armor, 0, 100 );
    const relic_recharge &rech = transfer_entry( suit );
    item *pack = loaded_mag( suit, itype_plut_pack_large );
    item *batt = loaded_mag( suit, itype_rm13_battery );
    REQUIRE( pack != nullptr );
    REQUIRE( batt != nullptr );

    calendar::turn += 1_turns;
    CHECK_FALSE( relic_funcs::process_recharge_entry( suit, rech, &you ) );
    CHECK( pack->ammo_remaining() == 0 );
    CHECK( batt->ammo_remaining() == 100 );

    // the suit draws on the battery once the pack is dry
    suit.ammo_consume( 5, you.bub_pos(), &you );
    CHECK( batt->ammo_remaining() == 95 );
    CHECK( suit.ammo_remaining() == 95 );
}

// B5
TEST_CASE( "rm13_pack_b5_the_pack_drains_first", "[rm13pack1][p3][relic][plut][pocket]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_avatar();
    item &suit = load_suit( you, itype_rm13_armor, 1000, 500 );
    item *pack = loaded_mag( suit, itype_plut_pack_large );
    item *batt = loaded_mag( suit, itype_rm13_battery );
    REQUIRE( pack != nullptr );
    REQUIRE( batt != nullptr );

    suit.ammo_consume( 5, you.bub_pos(), &you );
    CHECK( pack->ammo_remaining() == 995 );
    CHECK( batt->ammo_remaining() == 500 );

    pack->ammo_set( itype_plut_cell_charge, 0 );
    REQUIRE( pack->ammo_remaining() == 0 );
    suit.ammo_consume( 5, you.bub_pos(), &you );
    CHECK( pack->ammo_remaining() == 0 );
    CHECK( batt->ammo_remaining() == 495 );
}

// B6
TEST_CASE( "rm13_pack_b6_the_transform_keeps_both_wells", "[rm13pack1][p3][relic][plut][pocket]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_avatar();
    item &suit = load_suit( you, itype_rm13_armor, 4995, 1000 );

    suit.convert( itype_rm13_armor_on );
    CHECK( suit.typeId() == itype_rm13_armor_on );
    REQUIRE( suit.magazines_current().size() == 2 );
    item *pack = loaded_mag( suit, itype_plut_pack_large );
    item *batt = loaded_mag( suit, itype_rm13_battery );
    REQUIRE( pack != nullptr );
    REQUIRE( batt != nullptr );
    CHECK( pack->ammo_remaining() == 4995 );
    CHECK( batt->ammo_remaining() == 1000 );
    CHECK( suit.ammo_remaining() == 5995 );

    suit.convert( itype_rm13_armor );
    CHECK( suit.typeId() == itype_rm13_armor );
    REQUIRE( suit.magazines_current().size() == 2 );
    pack = loaded_mag( suit, itype_plut_pack_large );
    batt = loaded_mag( suit, itype_rm13_battery );
    REQUIRE( pack != nullptr );
    REQUIRE( batt != nullptr );
    CHECK( pack->ammo_remaining() == 4995 );
    CHECK( batt->ammo_remaining() == 1000 );
    CHECK( suit.ammo_remaining() == 5995 );
}

// B7
TEST_CASE( "rm13_pack_b7_worn_suit_burns_the_pack_first", "[rm13pack1][p3][relic][plut][pocket]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_avatar();
    item &spawned = load_suit( you, itype_rm13_armor_on, 500, 5000 );
    // CDF: turns_active lives on the shared itype (BN model); reset it the way deactivate() does.
    // item::process stops at "inactive item" before process_tool, so the suit must be active, as the
    // transform's "active": true leaves it in play.
    spawned.type->tool->turns_active = 0;
    spawned.activate();
    you.add_worn( spawned.detach() );
    item *suit = nullptr;
    for( item *w : you.worn ) {
        if( w->typeId() == itype_rm13_armor_on ) {
            suit = w;
        }
    }
    REQUIRE( suit != nullptr );
    item *pack = loaded_mag( *suit, itype_plut_pack_large );
    item *batt = loaded_mag( *suit, itype_rm13_battery );
    REQUIRE( pack != nullptr );
    REQUIRE( batt != nullptr );

    // CDF: rm13_armor_on turns_per_charge 18, charges_per_use 5: 20 x 5 charges.
    // CDF: BN arms turns_active on the first tick and drains at the start of the next one, so the
    // 20th draw lands on tick 361 (18 x 20 + 1), not 360.
    INFO( "turns_per_charge " << suit->type->tool->turns_per_charge
          << " required " << suit->ammo_required()
          << " needs_processing " << suit->needs_processing()
          << " worn " << you.worn.size() );
    for( int i = 0; i < 361; ++i ) {
        calendar::turn += 1_turns;
        you.process_items();
    }
    INFO( "pack " << pack->ammo_remaining() << " battery " << batt->ammo_remaining()
          << " turns_active " << suit->type->tool->turns_active );
    CHECK( pack->ammo_remaining() == 400 );
    CHECK( batt->ammo_remaining() == 5000 );
    CHECK( suit->typeId() == itype_rm13_armor_on );
}

// B8
TEST_CASE( "rm13_pack_b8_the_name_shows_both_wells", "[rm13pack1][p3][relic][plut][pocket]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_avatar();
    item &suit = load_suit( you, itype_rm13_armor_on, 5000, 1000 );
    const std::string shown = suit.display_name();
    // CDF: the two well segments of item::display_name (multi_well_name_segments)
    CHECK( shown.find( "5000/5000" ) != std::string::npos );
    CHECK( shown.find( "1000/5000" ) != std::string::npos );
    CHECK( shown.find( "plutonium charge" ) != std::string::npos );
    CHECK( shown.find( "battery" ) != std::string::npos );
}

// B9
TEST_CASE( "rm13_pack_b9_transfer_entry_round_trips", "[rm13pack1][p3][relic][plut][pocket]" )
{
    clear_all_state();
    relic_recharge rech;
    rech.type = relic_recharge_type::transfer;
    rech.from_ammo = ammotype( "plutonium" );
    rech.to_ammo = ammotype( "battery" );
    rech.interval = 2_seconds;
    rech.rate = 3;
    rech.cost = 2;
    rech.message = "The pack hums.";

    std::ostringstream out;
    {
        JsonOut jsout( out );
        rech.serialize( jsout );
    }
    const std::string text = out.str();
    // CDF: JsonOut writes enums as their underlying number (json.h write(T) requires enum), so the
    // type is checked on the loaded object; the string members are checked in the text too.
    CHECK( text.find( "\"from_ammo\":\"plutonium\"" ) != std::string::npos );
    CHECK( text.find( "\"to_ammo\":\"battery\"" ) != std::string::npos );
    CHECK( text.find( "\"cost\":2" ) != std::string::npos );

    std::istringstream in( text );
    JsonIn jsin( in );
    relic_recharge loaded;
    loaded.deserialize( jsin );
    CHECK( loaded.type == relic_recharge_type::transfer );
    CHECK( loaded.from_ammo == ammotype( "plutonium" ) );
    CHECK( loaded.to_ammo == ammotype( "battery" ) );
    CHECK( loaded.rate == 3 );
    CHECK( loaded.cost == 2 );
    CHECK( loaded == rech );
}
