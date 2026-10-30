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
