#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "game_inventory.h"
#include "inventory_ui.h"
#include "item.h"
#include "item_pocket.h"
#include "itype.h"
#include "json.h"
#include "recipe.h"
#include "state_helpers.h"
#include "type_id.h"
#include "uistate.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// PL-64 IN4 (G40, G21). G40: DDA's apply menu preset (game_inventory.cpp:1117-1140 at
// b4b62254ae2) walks only CONTAINER and MOD pockets (1123), lists a craft always, hides a
// comestible that has a use unless it is PETFOOD, and drops an installed gunmod that is invisible
// when installed (1131-1135). G21: hide_entries_override starts every load at nullopt, also when
// the world has no uistate.json. Expected item data comes from the JSON (lighter has a firestarter
// use, bandages is a comestible MED with a heal use, dogfood is a comestible with PETFOOD, jerky has
// no use, glock_19 takes glockmag, ar15 takes u_shotgun), not from the code under test.
// PL-64 IN5: the rest of DDA's get_denial (1137-1212) and _collate_entries (1122): comestible
// tool, consume_drug tools_needed, the single-use can_call, the charge check, frozen liquids, and
// collation. DDA's relic, SMOKING and is_broken checks have no CDF equivalent and are skipped.
// ---------------------------------------------------------------------------

namespace game_menus::inv
{
// CDF: PL-64 IN4. Defined in src/game_inventory.cpp, which declares them itself; they are not in
// game_inventory.h (a hook there would rebuild its includers). Keep the signatures identical.
bool use_preset_shows( const player &p, const item &it );
std::vector<pocket_type> use_preset_pocket_types();
std::string use_preset_denial( const player &p, const item &it );
bool use_preset_collates();
} // namespace game_menus::inv

// CDF: PL-64 G21. Defined in src/game.cpp; the reset game::setup() and game::load() run.
void reset_uistate_hide_entries_override();

namespace
{

// The real apply preset is file-local. This copy takes its pocket-type list and its is_shown
// through the two hooks, so the rows of an actual selector are built with the ported rules.
class apply_menu_probe_preset : public inventory_selector_preset
{
    public:
        apply_menu_probe_preset() {
            _pk_type = game_menus::inv::use_preset_pocket_types();
        }

        bool is_shown( const item *loc ) const override {
            return game_menus::inv::use_preset_shows( get_avatar(), *loc );
        }
};

// Same pocket-type list, every item shown: isolates the pocket filter from is_shown.
class apply_pocket_probe_preset : public inventory_selector_preset
{
    public:
        apply_pocket_probe_preset() {
            _pk_type = game_menus::inv::use_preset_pocket_types();
        }

        bool is_shown( const item * ) const override {
            return true;
        }
};

class apply_selector_probe : public inventory_pick_selector
{
    public:
        using inventory_pick_selector::inventory_pick_selector;

        // The item of every row of the three columns, in column order.
        auto listed_items() const -> std::vector<const item *> {
            std::vector<const item *> rows;
            for( const inventory_column *col : { &own_gear_column, &own_inv_column, &map_column } ) {
                for( const inventory_entry *entry : col->get_all_entries() ) {
                    if( entry->is_item() ) {
                        rows.push_back( entry->any_item() );
                    }
                }
            }
            return rows;
        }
};

auto listed( avatar &you, const inventory_selector_preset &preset ) -> std::vector<const item *>
{
    apply_selector_probe sel( you, preset );
    sel.clear_items();
    sel.add_character_items( you );
    return sel.listed_items();
}

auto contains( const std::vector<const item *> &rows, const item *it ) -> bool
{
    return std::ranges::find( rows, it ) != rows.end();
}

} // namespace

// DDA game_inventory.cpp:1123: _pk_type = { CONTAINER, MOD }. Magazine wells, magazines and cable
// pockets are not walked.
TEST_CASE( "apply_menu_walks_the_dda_pocket_types", "[inventory][apply]" )
{
    const std::vector<pocket_type> types = game_menus::inv::use_preset_pocket_types();
    CHECK( types == std::vector<pocket_type>{ pocket_type::CONTAINER, pocket_type::MOD } );
    CHECK( std::ranges::find( types, pocket_type::MAGAZINE_WELL ) == types.end() );
    CHECK( std::ranges::find( types, pocket_type::MAGAZINE ) == types.end() );
    CHECK( std::ranges::find( types, pocket_type::CABLE ) == types.end() );
    CHECK( std::ranges::find( types, pocket_type::MIGRATION ) == types.end() );
}

// DDA inventory_ui.cpp:2109-2141 via _pk_type: a gun's magazine well and its magazine are not
// child rows, an installed gunmod is. Every item is shown here so only the pocket filter decides.
TEST_CASE( "apply_menu_lists_a_mod_but_not_a_seated_magazine", "[inventory][apply]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    const time_point bday = calendar::start_of_cataclysm;

    detached_ptr<item> mag = item::spawn( "glockmag", bday, 0 );
    REQUIRE( mag->put_in( item::spawn( "9mm", bday, 15 ), pocket_type::MAGAZINE ).success() );
    item *mag_ptr = mag.get();
    // CDF: glock_19 has a glockmag well (data/json/items/gun/9mm.json)
    detached_ptr<item> gun = item::spawn( "glock_19", bday, 0 );
    gun->put_in( std::move( mag ) );
    REQUIRE( gun->magazine_current() == mag_ptr );
    item *gun_ptr = gun.get();
    // CDF: ar15 takes the underslung shotgun u_shotgun in its MOD pocket (gun/223.json, gunmod/underbarrel.json)
    REQUIRE( gun->put_in( item::spawn( "u_shotgun", bday, 0 ), pocket_type::MOD ).success() );
    REQUIRE_FALSE( gun->gunmods().empty() );
    item *mod_ptr = gun->gunmods().front();

    you.i_add( std::move( gun ) );
    const std::vector<const item *> rows = listed( you, apply_pocket_probe_preset() );

    CHECK( contains( rows, gun_ptr ) );
    CHECK( contains( rows, mod_ptr ) );
    CHECK_FALSE( contains( rows, mag_ptr ) );
}

// DDA game_inventory.cpp:1134: an item in a CONTAINER pocket is still a row when it is usable.
TEST_CASE( "apply_menu_lists_a_usable_item_nested_in_a_container", "[inventory][apply]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    const time_point bday = calendar::start_of_cataclysm;

    detached_ptr<item> pack = item::spawn( "backpack", bday );
    detached_ptr<item> lighter = item::spawn( "lighter", bday );
    item *lighter_ptr = lighter.get();
    REQUIRE( pack->put_in( std::move( lighter ), pocket_type::CONTAINER ).success() );
    you.i_add( std::move( pack ) );

    REQUIRE( lighter_ptr->parent_item() != nullptr );
    CHECK( game_menus::inv::use_preset_shows( you, *lighter_ptr ) );
    CHECK( contains( listed( you, apply_menu_probe_preset() ), lighter_ptr ) );
}

// DDA game_inventory.cpp:1131-1135: a comestible is listed only when it has a use and that use is
// PETFOOD. bandages is a comestible MED with a heal use (med.json), dogfood has PETFOOD
// (comestibles/other.json), jerky has no use.
TEST_CASE( "apply_menu_hides_comestibles_with_a_use_except_petfood", "[inventory][apply]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    const time_point bday = calendar::start_of_cataclysm;

    detached_ptr<item> bandages = item::spawn( "bandages", bday );
    detached_ptr<item> dogfood = item::spawn( "dogfood", bday );
    detached_ptr<item> jerky = item::spawn( "jerky", bday );
    detached_ptr<item> water = item::spawn( "water_clean", bday );

    REQUIRE( bandages->is_comestible() );
    REQUIRE( bandages->type->has_use() );
    REQUIRE( bandages->type->can_use( "PETFOOD" ) == false );
    CHECK_FALSE( game_menus::inv::use_preset_shows( you, *bandages ) );

    REQUIRE( dogfood->is_comestible() );
    REQUIRE( dogfood->type->can_use( "PETFOOD" ) );
    CHECK( game_menus::inv::use_preset_shows( you, *dogfood ) );

    REQUIRE( jerky->is_comestible() );
    REQUIRE_FALSE( jerky->type->has_use() );
    CHECK_FALSE( game_menus::inv::use_preset_shows( you, *jerky ) );
    CHECK_FALSE( game_menus::inv::use_preset_shows( you, *water ) );
}

// DDA game_inventory.cpp:1131-1135 drops the old CDF shortcut that listed a container because a
// medication with a use sat inside it: the medication is its own row, the bottle is not.
TEST_CASE( "apply_menu_does_not_list_a_container_holding_a_medication", "[inventory][apply]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    const time_point bday = calendar::start_of_cataclysm;

    detached_ptr<item> bottle = item::spawn( "bottle_plastic", bday );
    detached_ptr<item> bandages = item::spawn( "bandages", bday );
    REQUIRE( bottle->put_in( std::move( bandages ), pocket_type::CONTAINER ).success() );
    const item &inside = bottle->get_contained();
    REQUIRE( inside.is_medication() );
    REQUIRE( inside.type->has_use() );

    CHECK_FALSE( game_menus::inv::use_preset_shows( you, *bottle ) );
    CHECK_FALSE( game_menus::inv::use_preset_shows( you, inside ) );
}

// DDA game_inventory.cpp:1131: loc->is_craft() is listed before the use test, so an in-progress
// craft is a row. The "craft" item type carries use_action CRAFT (data/json/items/generic.json, as
// in DDA b4b62254ae2:3227), so the old premise that it has no use does not hold.
TEST_CASE( "apply_menu_lists_an_in_progress_craft", "[inventory][apply]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    const recipe &rec = recipe_id( "cudgel" ).obj();
    item craft( &rec, 1, {}, {} );
    REQUIRE( craft.is_craft() );
    CHECK( game_menus::inv::use_preset_shows( you, craft ) );
}

// DDA game_inventory.cpp:1122: the apply preset collates its entries (_collate_entries = true).
// CDF spells that as the collate_entries() override.
TEST_CASE( "apply_menu_collates_entries", "[inventory][apply]" )
{
    CHECK( game_menus::inv::use_preset_collates() );
}

// DDA game_inventory.cpp:1145-1153: a comestible whose comestible tool is missing is denied.
// iv_mutagen carries "tool": "syringe" (data/json/items/comestibles/mutagen.json). It is also a
// comestible with a use, so is_shown keeps it out of the list (1131-1135); the denial is checked
// on its own because that is how DDA writes the check.
TEST_CASE( "apply_menu_denies_a_comestible_without_its_tool", "[inventory][apply]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    const time_point bday = calendar::start_of_cataclysm;

    detached_ptr<item> iv = item::spawn( "iv_mutagen", bday );
    REQUIRE( iv->is_comestible() );
    REQUIRE( iv->get_comestible()->tool == itype_id( "syringe" ) );
    REQUIRE_FALSE( you.has_amount( itype_id( "syringe" ), 1 ) );
    CHECK_FALSE( game_menus::inv::use_preset_shows( you, *iv ) );
    CHECK( game_menus::inv::use_preset_denial( you, *iv ) ==
           "You need a " + item::nname( itype_id( "syringe" ) ) + " to consume that!" );
}

// DDA game_inventory.cpp:1154-1166: consume_drug's tools_needed are checked before anything else.
// dab_pen is a TOOL with one use and "tools_needed": { "thc_liquid": 1 }
// (data/json/items/tool/smoking.json), so it is listed and denied until the tool is carried.
TEST_CASE( "apply_menu_denies_a_drug_without_its_tool", "[inventory][apply]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    const time_point bday = calendar::start_of_cataclysm;

    detached_ptr<item> pen = item::spawn( "dab_pen", bday );
    REQUIRE( pen->type->get_use( "consume_drug" ) != nullptr );
    REQUIRE( pen->type->use_methods.size() == 1 );
    CHECK( game_menus::inv::use_preset_shows( you, *pen ) );
    CHECK( game_menus::inv::use_preset_denial( you, *pen ) ==
           "You need a " + item::nname( itype_id( "thc_liquid" ) ) + " to consume that!" );

    // With the tool carried the drug check passes and DDA's charge check (1191-1202) is next: the
    // pen has no battery and its single use costs 1 charge.
    you.i_add( item::spawn( "thc_liquid", bday, 5 ) );
    REQUIRE( you.has_charges( itype_id( "thc_liquid" ), 1 ) );
    REQUIRE_FALSE( pen->ammo_sufficient( &you, "consume_drug" ) );
    REQUIRE( pen->ammo_required() == 1 );
    CHECK( game_menus::inv::use_preset_denial( you, *pen ) == "Needs at least 1 charge." );
}

// DDA game_inventory.cpp:1137-1212 returns an empty string for a usable item that passes every
// check: a charged drug with no tools_needed has no denial.
TEST_CASE( "apply_menu_allows_a_charged_drug", "[inventory][apply]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    const time_point bday = calendar::start_of_cataclysm;

    detached_ptr<item> pen = item::spawn( "dab_pen_disposable", bday );
    REQUIRE( pen->type->use_methods.size() == 1 );
    REQUIRE( pen->ammo_sufficient( &you, "consume_drug" ) );
    CHECK( game_menus::inv::use_preset_shows( you, *pen ) );
    CHECK( game_menus::inv::use_preset_denial( you, *pen ).empty() );
}

// PL-64 G21. The reset must not depend on a uistate.json existing: game::setup() and game::load()
// call it directly (a new world never reaches uistatedata::deserialize).
TEST_CASE( "apply_menu_hide_override_resets_without_a_uistate_file", "[inventory][apply]" )
{
    uistate.hide_entries_override = true;
    reset_uistate_hide_entries_override();
    CHECK_FALSE( uistate.hide_entries_override.has_value() );

    uistate.hide_entries_override = false;
    reset_uistate_hide_entries_override();
    CHECK_FALSE( uistate.hide_entries_override.has_value() );

    // an existing uistate.json without the key gives the same default (savegame_json.cpp)
    std::istringstream in( "{}" );
    JsonIn jsin( in );
    uistatedata loaded;
    loaded.hide_entries_override = true;
    loaded.deserialize( jsin.get_object() );
    CHECK_FALSE( loaded.hide_entries_override.has_value() );
}
