// FILT1: DDA filter / rebreather cartridges, fillers, rm13_battery, and the loose filters they replace.
// DDA source: data/json/items/magazine/filter.json, items/ammo.json:1225-1262, items/ammo_types.json:654-672,
// items/armor/rm13_combat_armor.json:250-272 (DDA commit b4b62254ae2).
// CDF-authored (the DDA tree has no test for this data).
#include <algorithm>
#include <fstream>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "ammo.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "item.h"
#include "item_factory.h"
#include "item_group.h"
#include "itype.h"
#include "pocket_data.h"
#include "pocket_type.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "requirements.h"
#include "state_helpers.h"
#include "type_id.h"

namespace
{
auto has_itype( const itype_id &id ) -> bool
{
    return id.is_valid();
}

auto read_text( const std::string &path ) -> std::string
{
    std::ifstream f( path, std::ios::binary );
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

auto magazine_restriction( const itype &type, const std::string &ammo ) -> int
{
    for( const pocket_data &p : type.pockets ) {
        if( p.type == pocket_type::MAGAZINE ) {
            const auto it = p.ammo_restriction.find( ammotype( ammo ) );
            return it == p.ammo_restriction.end() ? -1 : it->second;
        }
    }
    return -2;
}

struct cartridge_expect {
    std::string id;
    std::string ammo;
    int capacity;
    std::vector<std::string> flags;
};
} // namespace

TEST_CASE( "filt1_cartridges_load_as_magazines", "[filt1][magazine]" )
{
    clear_all_state();
    // DDA: magazine/filter.json (capacities from the ammo_restriction of each MAGAZINE pocket)
    const std::vector<cartridge_expect> expected = {
        { "gasfilter_sm", "gasfilter_s", 100, { "GASFILTER_SM", "NO_UNLOAD", "NO_RELOAD", "NO_SALVAGE" } },
        { "gasfilter_med", "gasfilter_m", 100, { "GASFILTER_MED", "NO_UNLOAD", "NO_RELOAD", "NO_SALVAGE" } },
        { "rebreather_cartridge_air", "rebreather_filter", 90, { "REBREATHER_CART", "NO_RELOAD", "NO_SALVAGE" } },
        { "rebreather_cartridge_o2", "rebreather_filter", 180, { "REBREATHER_CART", "NO_RELOAD", "NO_SALVAGE" } },
        { "rebreather_cartridge_regen", "rebreather_o2_regenerator", 360, { "REBREATHER_CART", "NO_RELOAD", "NO_SALVAGE" } },
    };
    for( const cartridge_expect &e : expected ) {
        CAPTURE( e.id );
        REQUIRE( has_itype( itype_id( e.id ) ) );
        const item *cart = item::spawn_temporary( itype_id( e.id ) );
        CHECK( cart->is_magazine() );
        CHECK( magazine_restriction( *cart->type, e.ammo ) == e.capacity );
        for( const std::string &f : e.flags ) {
            CAPTURE( f );
            CHECK( cart->has_flag( flag_id( f ) ) );
        }
    }
    // the two gas filters can not be unloaded (DDA NO_UNLOAD); the rebreather cartridges can
    CHECK_FALSE( item::spawn_temporary( itype_id( "rebreather_cartridge_air" ) )->has_flag( flag_id( "NO_UNLOAD" ) ) );
}

TEST_CASE( "filt1_fillers_and_ammo_types", "[filt1][magazine]" )
{
    clear_all_state();
    REQUIRE( has_itype( itype_id( "rebreather_filler_soda_lime" ) ) );
    REQUIRE( has_itype( itype_id( "rebreather_filler_ko2" ) ) );
    const item *soda = item::spawn_temporary( itype_id( "rebreather_filler_soda_lime" ) );
    const item *ko2 = item::spawn_temporary( itype_id( "rebreather_filler_ko2" ) );
    CHECK( soda->is_ammo() );
    CHECK( ko2->is_ammo() );
    CHECK( soda->ammo_type() == ammotype( "rebreather_filter" ) ); // DDA ammo.json:1236
    CHECK( ko2->ammo_type() == ammotype( "rebreather_o2_regenerator" ) ); // DDA ammo.json:1255
    CHECK( ammotype( "rebreather_o2_regenerator" ).is_valid() );

    // an empty cartridge (a spawned one is full)
    item *regen = item::spawn_temporary( itype_id( "rebreather_cartridge_regen" ) );
    regen->ammo_unset();
    REQUIRE( regen->ammo_remaining() == 0 );
    CHECK( regen->can_contain( *ko2 ).success() );
    CHECK_FALSE( regen->can_contain( *soda ).success() );
    CHECK_FALSE( regen->can_contain( *item::spawn_temporary( itype_id( "battery" ) ) ).success() );
}

TEST_CASE( "filt1_rm13_battery", "[filt1][magazine]" )
{
    clear_all_state();
    REQUIRE( has_itype( itype_id( "rm13_battery" ) ) );
    const item *bat = item::spawn_temporary( itype_id( "rm13_battery" ) );
    CHECK( bat->is_magazine() );
    CHECK( magazine_restriction( *bat->type, "battery" ) == 5000 ); // DDA rm13_combat_armor.json:268
    CHECK( to_gram( bat->type->weight ) == 14000 ); // DDA :258
    CHECK( to_milliliter( bat->type->volume ) == 10000 ); // DDA :259
    CHECK( bat->has_flag( flag_id( "RECHARGE" ) ) );
}

TEST_CASE( "filt1_no_loose_filters_in_groups", "[filt1][magazine]" )
{
    clear_all_state();
    // group id -> cartridge that replaces the loose filter it held at the base (see FILT1 anchors)
    const std::map<std::string, std::string> groups = {
        { "portal_room_gear", "gasfilter_med" },
        { "SUS_scuba_gear", "rebreather_cartridge_air" },
        { "fishing_items", "rebreather_cartridge_air" },
        { "pawn", "rebreather_cartridge_air" },
        { "sports", "rebreather_cartridge_air" },
        { "camping", "rebreather_cartridge_air" },
        { "tools_science", "gasfilter_sm" },
        { "npc_misc", "rebreather_cartridge_air" },
    };
    const std::set<std::string> loose = { "gasfilter_s", "gasfilter_m", "rebreather_filter" };
    for( const auto &g : groups ) {
        CAPTURE( g.first );
        const item_group_id gid( g.first );
        Item_spawn_data *data = item_controller->get_group( gid );
        REQUIRE( data != nullptr );
        // the group can hand out the cartridge and no loose filter
        CHECK( data->has_item( itype_id( g.second ) ) );
        for( const std::string &l : loose ) {
            CHECK_FALSE( data->has_item( itype_id( l ) ) );
        }
        for( int i = 0; i < 300; ++i ) {
            for( const detached_ptr<item> &it : item_group::items_from( gid ) ) {
                CHECK( loose.count( it->typeId().str() ) == 0 );
                if( it->is_magazine() && ( it->typeId().str().find( "gasfilter_s" ) == 0 ||
                                           it->typeId().str().find( "rebreather_cartridge" ) == 0 ) ) {
                    CHECK( it->ammo_remaining() >= 0 );
                    CHECK( it->ammo_remaining() <= it->ammo_capacity() );
                }
            }
        }
    }
    // vehicle part item lists are not item groups: read the data files (CDF: vehicles/boats.json,
    // vehicles/helicopters.json held loose filters)
    for( const std::string &file : { "data/json/vehicles/boats.json", "data/json/vehicles/helicopters.json",
                                      "data/json/mapgen/nested/residential_personal_items.json" } ) {
        CAPTURE( file );
        const std::string text = read_text( file );
        REQUIRE_FALSE( text.empty() );
        CHECK( text.find( "\"rebreather_filter\"" ) == std::string::npos );
        CHECK( text.find( "\"gasfilter_m\"" ) == std::string::npos );
    }
    // charges survive: gear.json portal_room_gear spawns the canister with a [0, -1] charges range
    int max_seen = -1;
    for( int i = 0; i < 300; ++i ) {
        for( const detached_ptr<item> &it : item_group::items_from( item_group_id( "portal_room_gear" ) ) ) {
            if( it->typeId() == itype_id( "gasfilter_med" ) ) {
                max_seen = std::max( max_seen, it->ammo_remaining() );
            }
        }
    }
    CHECK( max_seen >= 0 );
}

TEST_CASE( "filt1_migration_and_recipe", "[filt1][magazine]" )
{
    clear_all_state();
    detached_ptr<item> migrated = item_controller->spawn_migrated( itype_id( "rebreather_filter" ),
                                  calendar::turn );
    REQUIRE( migrated );
    CHECK( migrated->typeId() == itype_id( "rebreather_cartridge_air" ) );

    int with_cartridge = 0;
    int with_loose = 0;
    for( const auto &r : recipe_dict ) {
        const recipe &rec = r.first.obj();
        if( rec.result() == itype_id( "rebreather_filter" ) ) {
            ++with_loose;
        }
        if( rec.result() == itype_id( "rebreather_cartridge_air" ) ) {
            ++with_cartridge;
            for( const auto &group : rec.simple_requirements().get_components() ) {
                for( const item_comp &c : group ) {
                    CAPTURE( c.type.str() );
                    CHECK( has_itype( c.type ) );
                }
            }
        }
    }
    CHECK( with_loose == 0 );
    CHECK( with_cartridge == 1 );
}

TEST_CASE( "filt1_npc_and_trade_ids_resolve", "[filt1][magazine]" )
{
    clear_all_state();
    const std::string talk = read_text( "data/json/npcs/robofac/NPC_ROBOFAC_INTERCOM.json" );
    REQUIRE_FALSE( talk.empty() );
    const std::regex buy( R"re("u_buy_item"\s*:\s*"(\w+)"(?:\s*,\s*"count"\s*:\s*(\d+))?)re" );
    std::map<std::string, int> bought; // id -> count (0 if no count)
    for( auto it = std::sregex_iterator( talk.begin(), talk.end(), buy ); it != std::sregex_iterator(); ++it ) {
        const std::string id = ( *it )[1];
        CAPTURE( id );
        CHECK( has_itype( itype_id( id ) ) );
        bought[id] = ( *it )[2].matched ? std::stoi( ( *it )[2] ) : 0;
    }
    CHECK( bought.count( "gasfilter_m" ) == 0 );
    REQUIRE( bought.count( "gasfilter_med" ) == 1 );
    CHECK( bought["gasfilter_med"] == 1 ); // 100 charges of carbon = one full canister

    Item_spawn_data *npc_misc = item_controller->get_group( item_group_id( "npc_misc" ) );
    REQUIRE( npc_misc != nullptr );
    CHECK( npc_misc->has_item( itype_id( "rebreather_cartridge_air" ) ) );
    CHECK_FALSE( npc_misc->has_item( itype_id( "rebreather_filter" ) ) );
}
