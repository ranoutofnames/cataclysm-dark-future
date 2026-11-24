// CDF: PC-E1 step E1-12 (the 15 recipe and blueprint loot files, AUTO-376, Option A). The BN files
// carried the item vars digital_recipes / digital_random_recipes (a fixed list, or a random pick by
// category, subcategory, candidate list and difficulty window). DDA books have no recipe list: a
// recipe is learned from a book through `book_learn` on the RECIPE (DDA recipe.cpp:473-480,
// recipe_dictionary.cpp:790; CDF src/recipe.cpp:201-210, src/craft/recipe_dictionary.cpp:607-616).
// So each file is a BOOK e-file (like digital_martial_arts.json) and the representative recipes
// carry `book_learn` for it. Written before the data.
// The pick rule (reports/E1-12-loot-picks.py): the recipes of the old selection that can be learned
// (not obsolete, not never_learn), in the old window; category or subcategory files take the first N
// by recipe id, candidate-list files the first N candidates in list order. The expected picks are
// worked out here from the loaded recipe dictionary, independently of the script.
// DDA has no such test, so every case is CDF-new.
#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "detached_ptr.h"
#include "flag.h"
#include "item.h"
#include "item_contents.h"
#include "itype.h"
#include "itype_slots/islot_book.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "skill.h"
#include "state_helpers.h"
#include "temp_crafting_inventory.h"
#include "type_id.h"
#include "units.h"

static const itype_id itype_battery( "battery" );
static const itype_id itype_test_laptop( "test_laptop" );

namespace
{

// the old selection of one file (the item vars of the BN item definitions)
struct loot_rule {
    const char *file;
    // the fixed file lists one recipe and its level, everything else is a random selection
    const char *fixed_recipe;
    int fixed_level;
    int count;
    const char *category;
    const char *subcategory;
    int min_difficulty;
    int max_difficulty;
    std::vector<std::string> candidates;
};

constexpr int any_difficulty = 1000;

const std::vector<loot_rule> &loot_rules()
{
    static const std::vector<loot_rule> rules = {
        { "digital_3d_printing_guide", "3d_printer_basic", 7, 0, "", "", 0, any_difficulty, {} },
        { "digital_cooking_recipe_archive", nullptr, 0, 1, "CC_FOOD", "", 0, 3, {} },
        { "digital_science_recipe_archive", nullptr, 0, 1, "", "", 3, any_difficulty, {} },
        { "digital_tailoring_recipe_archive", nullptr, 0, 3, "CC_ARMOR", "", 0, 3, {} },
        { "digital_bartender_drinks_list", nullptr, 0, 3, "", "CSC_FOOD_DRINKS", 0, 3, {} },
        { "digital_electronic_blueprints", nullptr, 0, 2, "CC_ELECTRONIC", "", 2, 6, {} },
        { "digital_chemical_synthesis_sheet", nullptr, 0, 1, "", "CSC_CHEM_CHEMICALS", 2, 6, {} },
        { "digital_workshop_tool_plans", nullptr, 0, 2, "", "CSC_OTHER_TOOLS", 2, 5, {} },
        { "digital_basic_trap_blueprints", nullptr, 0, 2, "", "CSC_OTHER_TRAPS", 0, 2, {} },
        {
            "digital_mechanical_trap_blueprints", nullptr, 0, 2, "", "", 0, any_difficulty,
            { "beartrap", "crossbow_trap", "shotgun_trap_single", "shotgun_trap", "blade_trap" }
        },
        { "digital_advanced_trap_blueprints", nullptr, 0, 1, "", "CSC_OTHER_TRAPS", 5, 8, {} },
        { "digital_blade_blueprints", nullptr, 0, 2, "", "CSC_WEAPON_CUTTING", 2, 6, {} },
        { "digital_piercing_weapon_blueprints", nullptr, 0, 2, "", "CSC_WEAPON_PIERCING", 2, 6, {} },
        {
            "digital_archery_blueprints", nullptr, 0, 2, "", "", 0, any_difficulty,
            { "shortbow", "longbow", "compositebow_short", "compositebow", "crossbow",
              "compositecrossbow", "rep_crossbow"
            }
        },
        {
            "digital_firearm_blueprints", nullptr, 0, 2, "", "", 0, any_difficulty,
            { "pipe_rifle", "pipe_shotgun", "pipe_double_shotgun", "revolver_shotgun",
              "handmade_revolver", "smg_luty", "handmade_carbine"
            }
        },
    };
    return rules;
}

struct expected_pick {
    const recipe *rec;
    int level;
};

// what the rule selects from the loaded recipe dictionary
auto expected_picks( const loot_rule &rule ) -> std::vector<expected_pick>
{
    std::vector<expected_pick> picks;
    if( rule.fixed_recipe != nullptr ) {
        const recipe &r = recipe_id( rule.fixed_recipe ).obj();
        picks.push_back( { &r, rule.fixed_level } );
        return picks;
    }
    std::vector<const recipe *> pool;
    for( const auto &entry : recipe_dict ) {
        const recipe &r = entry.second;
        if( r.obsolete || r.never_learn ) {
            continue;
        }
        if( rule.category[0] != '\0' && r.category != rule.category ) {
            continue;
        }
        if( rule.subcategory[0] != '\0' && r.subcategory != rule.subcategory ) {
            continue;
        }
        if( r.difficulty < rule.min_difficulty || r.difficulty > rule.max_difficulty ) {
            continue;
        }
        if( !rule.candidates.empty() &&
            std::ranges::find( rule.candidates, r.ident().str() ) == rule.candidates.end() ) {
            continue;
        }
        pool.push_back( &r );
    }
    if( rule.candidates.empty() ) {
        std::ranges::sort( pool, []( const recipe * a, const recipe * b ) {
            return a->ident().str() < b->ident().str();
        } );
    } else {
        std::ranges::sort( pool, [&rule]( const recipe * a, const recipe * b ) {
            return std::ranges::find( rule.candidates, a->ident().str() ) <
                   std::ranges::find( rule.candidates, b->ident().str() );
        } );
    }
    for( const recipe *r : pool ) {
        if( static_cast<int>( picks.size() ) >= rule.count ) {
            break;
        }
        picks.push_back( { r, r->difficulty } );
    }
    return picks;
}

auto store_file( item &device, const itype_id &file ) -> item &
{
    const std::vector<item *> before = device.efiles();
    REQUIRE( device.put_in( item::spawn( file ), pocket_type::E_FILE_STORAGE, false, nullptr,
                            true ).success() );
    for( item *stored : device.efiles() ) {
        if( std::find( before.begin(), before.end(), stored ) == before.end() ) {
            stored->set_browsed( true );
            return *stored;
        }
    }
    FAIL( "the stored file was not found" );
    return device;
}

} // namespace

// the file is a BOOK e-file like the martial arts videos, and nothing of the old item vars is left
TEST_CASE( "efile_loot_the_files_are_book_efiles", "[efile][loot]" )
{
    clear_all_state();
    REQUIRE( loot_rules().size() == 15 );
    for( const loot_rule &rule : loot_rules() ) {
        const itype_id id( rule.file );
        INFO( rule.file );
        REQUIRE( id.is_valid() );
        const itype &type = id.obj();
        CHECK( static_cast<bool>( type.book ) );
        const item file( id );
        CHECK( file.is_book() );
        CHECK( file.is_estorable() );
        CHECK( file.is_estorable_exclusive() );
        CHECK( file.is_ecopiable() );
        CHECK( type.ememory_size > 0_KB );
        // NOTE: CDF gives every item at least 1 ml (item_factory.cpp:559), so a BOOK file gets an
        // uncraft_book recipe (recipe_dictionary.cpp:630 tests the volume, DDA :818 the weight, and a
        // file is ZERO_WEIGHT). Pre-existing for every BOOK file (the martial arts videos); a stored
        // file is not offered for disassembly. Reported, not asserted here.
        // the generator input of the BN system is gone
        CHECK_FALSE( type.item_vars.contains( "digital_recipes" ) );
        CHECK_FALSE( type.item_vars.contains( "digital_random_recipes" ) );
    }
}

// the stable link: the book of the file lists exactly the picks of the rule, at the old level
TEST_CASE( "efile_loot_each_file_links_its_representative_recipes", "[efile][loot]" )
{
    clear_all_state();
    for( const loot_rule &rule : loot_rules() ) {
        const itype_id id( rule.file );
        INFO( rule.file );
        REQUIRE( id.is_valid() );
        REQUIRE( static_cast<bool>( id.obj().book ) );
        const std::vector<expected_pick> picks = expected_picks( rule );
        const int wanted = rule.fixed_recipe != nullptr ? 1 : rule.count;
        // the pool is large enough: the file does not give fewer recipes than the old one
        REQUIRE( static_cast<int>( picks.size() ) == wanted );

        const std::set<book_recipe> &listed = id.obj().book->recipes;
        CHECK( static_cast<int>( listed.size() ) == wanted );
        for( const expected_pick &pick : picks ) {
            CAPTURE( pick.rec->ident().str(), pick.level );
            const auto found = std::ranges::find_if( listed, [&pick]( const book_recipe & entry ) {
                return entry.recipe == pick.rec;
            } );
            REQUIRE( found != listed.end() );
            CHECK( found->skill_level == pick.level );
            // the recipe names the file in its own book_learn
            CHECK( pick.rec->booksets.contains( id ) );
        }
    }
}

// the learn path of E1-8: a powered device with the file gives the recipes (get_recipes_from_ebooks
// reads the books of the device), an unpowered one gives nothing
TEST_CASE( "efile_loot_a_powered_device_with_the_files_teaches_the_recipes", "[efile][loot]" )
{
    clear_all_state();
    set_time( calendar::turn_zero + 12_hours );
    clear_map();
    clear_avatar();
    avatar &who = get_avatar();
    who.wear_item( item::spawn( "backpack" ), false );
    item &laptop = who.i_add( item::spawn( itype_test_laptop ) );
    laptop.ammo_set( itype_battery, 10 );
    REQUIRE( laptop.ammo_remaining() == 10 );

    std::vector<const item *> files;
    for( const loot_rule &rule : loot_rules() ) {
        files.push_back( &store_file( laptop, itype_id( rule.file ) ) );
    }
    // a character who knows enough of every skill
    for( const loot_rule &rule : loot_rules() ) {
        for( const expected_pick &pick : expected_picks( rule ) ) {
            if( pick.rec->skill_used ) {
                who.set_skill_level( pick.rec->skill_used, 10 );
            }
        }
    }

    // each file by itself
    for( size_t i = 0; i < files.size(); ++i ) {
        const loot_rule &rule = loot_rules()[i];
        INFO( rule.file );
        const std::vector<std::pair<const recipe *, int>> given = files[i]->get_available_recipes( who );
        for( const expected_pick &pick : expected_picks( rule ) ) {
            CAPTURE( pick.rec->ident().str() );
            CHECK( std::ranges::any_of( given, [&pick]( const std::pair<const recipe *, int> &entry ) {
                return entry.first == pick.rec;
            } ) );
        }
    }

    // the crafting side: the device counts
    who.invalidate_crafting_inventory();
    const recipe_subset powered = who.get_recipes_from_ebooks( who.crafting_inventory() );
    for( const loot_rule &rule : loot_rules() ) {
        for( const expected_pick &pick : expected_picks( rule ) ) {
            CAPTURE( rule.file, pick.rec->ident().str() );
            CHECK( powered.contains( *pick.rec ) );
        }
    }

    SECTION( "a device without power gives nothing" ) {
        laptop.ammo_consume( laptop.ammo_remaining(), who.bub_pos(), &who );
        REQUIRE( laptop.ammo_remaining() == 0 );
        who.invalidate_crafting_inventory();
        const recipe_subset unpowered = who.get_recipes_from_ebooks( who.crafting_inventory() );
        for( const loot_rule &rule : loot_rules() ) {
            for( const expected_pick &pick : expected_picks( rule ) ) {
                CAPTURE( rule.file, pick.rec->ident().str() );
                CHECK_FALSE( unpowered.contains( *pick.rec ) );
            }
        }
    }
}

// DDA gates the pseudo uncraft_book recipe on weight (recipe_dictionary.cpp:818), so a zero-weight
// loot file gets no disassembly recipe, while a paper book keeps its own
TEST_CASE( "efile_loot_files_get_no_uncraft_book_recipe", "[efile][loot][uncraft]" )
{
    clear_all_state();
    REQUIRE( loot_rules().size() == 15 );
    for( const loot_rule &rule : loot_rules() ) {
        const itype_id id( rule.file );
        INFO( rule.file );
        REQUIRE( id.is_valid() );
        // precondition of the weight gate: the loaded file weighs nothing
        const item file( id );
        CHECK( file.weight() == 0_gram );
        CHECK( recipe_dictionary::get_uncraft( id ).ident().str().empty() );
    }

    const itype_id paper_book( "textbook_carpentry" );
    REQUIRE( paper_book.is_valid() );
    CHECK( recipe_dictionary::get_uncraft( paper_book ).ident().str() == paper_book.str() );
}
