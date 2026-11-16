#include "catch/catch.hpp"
#include "item_category.h"
#include "item_factory.h"
#include "itype.h"
#include "options.h"

#include <set>
#include <string>

// Every item category a real item can resolve to must have a SPAWN_RATE_<id>
// option.  map::item_category_spawn_rate (src/map/map.cpp:5166),
// mapgen_constructor::item_category_spawn_rate (src/mapgen/mapgen_constructor.cpp:638)
// and get_item_category_spawn_rate (src/monster.cpp:4011) build the option name
// from item::get_category().id and call get_option(), which errors when the
// option does not exist: "requested non-existing option SPAWN_RATE_<id>".
TEST_CASE("every item category has a spawn rate option", "[options][spawn_rate]")
{
    options_manager &opts = get_options();
    std::set<std::string> used_categories;

    for( const itype *type : item_controller->all() ) {
        REQUIRE( type != nullptr );
        // Item_factory::load sets category_force for every type (src/item_factory.cpp:530);
        // item::get_category() returns it unless the item is a non-empty container, and
        // container contents are item types themselves, so this covers every reachable id.
        if( !type->category_force.is_valid() ) {
            continue;
        }
        const std::string cat = type->category_force.str();
        used_categories.insert( cat );
        INFO("itype " << type->get_id().str() << " has category " << cat);
        CHECK( item_category_id( cat ).is_valid() );
    }

    // The DDA armor/clothing split (data/json/item_category.json "armor") is used by
    // real items, so its option must exist.
    REQUIRE( used_categories.count( "armor" ) == 1 );

    for( const std::string &cat : used_categories ) {
        INFO("item category " << cat);
        CHECK( opts.has_option( "SPAWN_RATE_" + cat ) );
    }
    CHECK( opts.has_option( "SPAWN_RATE_armor" ) );

    // ITEMS_WORN, WEAPON_HELD and MISC_USABLES are UI-only pseudo categories
    // (src/inventory_ui.cpp:1928-2149); no item type may resolve to them, which is
    // why they need no spawn rate option.
    CHECK( used_categories.count( "ITEMS_WORN" ) == 0 );
    CHECK( used_categories.count( "WEAPON_HELD" ) == 0 );
    CHECK( used_categories.count( "MISC_USABLES" ) == 0 );
}
