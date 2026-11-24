// CDF: PC-E1 step E1-8 (reading with an e-reader, NPC reading, recipes from e-books). Tests for DDA
// b4b62254ae2 avatar.cpp:496-535, 761 (avatar::read( book, ereader )), activity_actor.cpp:2122-2205
// (read_activity_actor: one battery use at the start, a cancel when the e-reader is empty or gone,
// one use per five minutes), npc.cpp:1486-1550 (do_npc_read( ebook )), character.cpp:8157
// (Character::use: a book in an e-device is read with that device), character_crafting.cpp:127-179
// (get_recipes_from_ebooks). Written before the code.
// DEVIATION: DDA has no test of the NPC read, of the recipes from e-books or of the stored book
// read through the use function (its reading_test.cpp has only reading_a_book_with_an_ebook_reader,
// transplanted into tests/reading_test.cpp), so every case here is CDF-new. ACT_READ is an
// old-style activity in CDF: the book is targets[0] and the e-reader targets[1] (DDA keeps both in
// read_activity_actor), so the cases drive activity_handlers::read_do_turn directly, as the digital
// study test did. The NPC menus (ereader_to_use, ebookread) cannot be driven headless: do_npc_read
// calls the hook efile_read_hooks::npc_start_reading, which the cases call directly.
#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "activity_handlers.h"
#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "character_functions.h"
#include "detached_ptr.h"
#include "effect.h"
#include "flag.h"
#include "item.h"
#include "itype.h"
#include "map/map.h"
#include "map_helpers.h"
#include "npc.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "skill.h"
#include "state_helpers.h"
#include "temp_crafting_inventory.h"
#include "type_id.h"

namespace efile_read_hooks
{
// src/npc.cpp: what do_npc_read does after the menus picked the book (and the e-reader): the NPC's
// own reading check, then the ACT_READ activity (DDA npc.cpp:1520-1550). True when it started.
bool npc_start_reading( npc &guy, item &book, item *ereader );
} // namespace efile_read_hooks

static const activity_id ACT_READ( "ACT_READ" );

static const efftype_id effect_darkness( "darkness" );

static const flag_id flag_CAN_USE_IN_DARK( "CAN_USE_IN_DARK" );

static const itype_id itype_battery( "battery" );
static const itype_id itype_novel_western( "novel_western" );
static const itype_id itype_recipe_alpha( "recipe_alpha" );
static const itype_id itype_efile_recipes( "efile_recipes" );
static const itype_id itype_test_book( "test_book" );
static const itype_id itype_test_ebook_reader( "test_ebook_reader" );
static const itype_id itype_test_eink_tablet_pc( "test_eink_tablet_pc" );

static const recipe_id recipe_sushi_rice( "sushi_rice" );

namespace
{

// 12:05, a five-minute boundary: the one-use-per-five-minutes rule fires exactly when a handler
// call happens on such a boundary (action_time_scale::once_every_this_tick( 5_minutes ))
const time_point five_minute_boundary = calendar::turn_zero + 12_hours + 5_minutes;

auto start_reading_day() -> void
{
    clear_all_state();
    set_time( five_minute_boundary );
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

auto powered_device( Character &who, const itype_id &type, int charges ) -> item &
{
    item &device = who.i_add( item::spawn( type ) );
    device.ammo_set( itype_battery, charges );
    REQUIRE( device.ammo_remaining() == charges );
    return device;
}

auto carrying_avatar() -> avatar &
{
    avatar &who = get_avatar();
    clear_avatar();
    who.wear_item( item::spawn( "backpack" ), false );
    return who;
}

auto reading_ereader( const avatar &who ) -> item *
{
    if( who.activity->targets.size() < 2 || !who.activity->targets[1] ) {
        return nullptr;
    }
    return &*who.activity->targets[1];
}

// the recipes the device and its stored books give, as the crafting code asks for them
auto ebook_recipes( avatar &who ) -> recipe_subset
{
    who.invalidate_crafting_inventory();
    return who.get_recipes_from_ebooks( who.crafting_inventory() );
}

} // namespace

TEST_CASE( "efile_read_with_an_ereader_charges_the_device", "[efile][read]" )
{
    start_reading_day();
    avatar &who = carrying_avatar();
    item &reader = powered_device( who, itype_test_ebook_reader, 100 );
    item &book = store_file( reader, itype_test_book );
    REQUIRE( reader.has_flag( flag_CAN_USE_IN_DARK ) );
    REQUIRE( book.has_flag( flag_CAN_USE_IN_DARK ) );

    SECTION( "starting costs one use, the e-reader is the second target" ) {
        REQUIRE( who.read( &book, &reader, true ) );
        REQUIRE( who.activity->id() == ACT_READ );
        CHECK( reader.ammo_remaining() == 99 );
        CHECK( &*who.activity->targets.front() == &book );
        CHECK( reading_ereader( who ) == &reader );
    }

    SECTION( "one use more at a five-minute mark, none in between" ) {
        REQUIRE( who.read( &book, &reader, true ) );
        REQUIRE( reader.ammo_remaining() == 99 );
        activity_handlers::read_do_turn( &*who.activity, &who );
        CHECK( who.activity->id() == ACT_READ );
        CHECK( reader.ammo_remaining() == 98 );

        set_time( five_minute_boundary + 30_seconds );
        activity_handlers::read_do_turn( &*who.activity, &who );
        CHECK( who.activity->id() == ACT_READ );
        CHECK( reader.ammo_remaining() == 98 );

        set_time( five_minute_boundary + 5_minutes );
        activity_handlers::read_do_turn( &*who.activity, &who );
        CHECK( who.activity->id() == ACT_READ );
        CHECK( reader.ammo_remaining() == 97 );
    }

    SECTION( "an empty e-reader stops the reading" ) {
        REQUIRE( who.read( &book, &reader, true ) );
        reader.ammo_consume( reader.ammo_remaining(), who.bub_pos(), &who );
        REQUIRE( reader.ammo_remaining() == 0 );
        activity_handlers::read_do_turn( &*who.activity, &who );
        CHECK( who.activity->id() != ACT_READ );
        CHECK( who.activity->is_null() );
    }

    SECTION( "a device that is gone stops the reading" ) {
        REQUIRE( who.read( &book, &reader, true ) );
        {
            detached_ptr<item> gone = reader.detach();
            activity_handlers::read_do_turn( &*who.activity, &who );
            CHECK( who.activity->is_null() );
        }
    }

    SECTION( "a book that left the device stops the reading" ) {
        REQUIRE( who.read( &book, &reader, true ) );
        {
            detached_ptr<item> gone = book.detach();
            activity_handlers::read_do_turn( &*who.activity, &who );
            CHECK( who.activity->is_null() );
        }
    }

    SECTION( "a paper book does not use an e-reader" ) {
        item &paper = who.i_add( item::spawn( itype_test_book ) );
        REQUIRE( who.read( &paper, true ) );
        REQUIRE( who.activity->id() == ACT_READ );
        CHECK( reading_ereader( who ) == nullptr );
        CHECK( reader.ammo_remaining() == 100 );
        activity_handlers::read_do_turn( &*who.activity, &who );
        CHECK( who.activity->id() == ACT_READ );
        CHECK( reader.ammo_remaining() == 100 );
    }

    SECTION( "reading with a null e-reader is the plain read" ) {
        item &paper = who.i_add( item::spawn( itype_test_book ) );
        REQUIRE( who.read( &paper, nullptr, true ) );
        REQUIRE( who.activity->id() == ACT_READ );
        CHECK( reading_ereader( who ) == nullptr );
    }
}

TEST_CASE( "efile_read_an_ereader_on_the_ground_works", "[efile][read]" )
{
    start_reading_day();
    avatar &who = carrying_avatar();
    detached_ptr<item> det = item::spawn( itype_test_ebook_reader );
    item &reader = *det;
    reader.ammo_set( itype_battery, 100 );
    item &book = store_file( reader, itype_test_book );
    get_map().add_item( who.bub_pos(), std::move( det ) );

    REQUIRE( who.read( &book, &reader, true ) );
    REQUIRE( who.activity->id() == ACT_READ );
    CHECK( reader.ammo_remaining() == 99 );
    activity_handlers::read_do_turn( &*who.activity, &who );
    CHECK( who.activity->id() == ACT_READ );
    CHECK( reader.ammo_remaining() == 98 );
    reader.ammo_consume( reader.ammo_remaining(), who.bub_pos(), &who );
    activity_handlers::read_do_turn( &*who.activity, &who );
    CHECK( who.activity->is_null() );
}

TEST_CASE( "efile_read_in_the_dark_needs_a_book_that_can_be_used_in_the_dark", "[efile][read]" )
{
    start_reading_day();
    avatar &who = carrying_avatar();
    item &reader = powered_device( who, itype_test_ebook_reader, 100 );
    item &stored = store_file( reader, itype_test_book );
    item &paper = who.i_add( item::spawn( itype_test_book ) );
    who.add_env_effect( effect_darkness, body_part_eyes, 3, 1_hours );
    REQUIRE_FALSE( character_funcs::can_see_fine_details( who ) );
    REQUIRE( stored.has_flag( flag_CAN_USE_IN_DARK ) );
    REQUIRE_FALSE( paper.has_flag( flag_CAN_USE_IN_DARK ) );

    SECTION( "the stored book can be read, as DDA check_read_condition lets it" ) {
        std::vector<std::string> reasons;
        CHECK( who.get_book_reader( stored, reasons ) == &who );
        CHECK( reasons.empty() );
        REQUIRE( who.read( &stored, &reader, true ) );
        CHECK( who.activity->id() == ACT_READ );
        CHECK( reader.ammo_remaining() == 99 );
    }

    SECTION( "the paper book cannot" ) {
        std::vector<std::string> reasons;
        CHECK( who.get_book_reader( paper, reasons ) == nullptr );
        REQUIRE( reasons.size() == 1 );
        CHECK( reasons.front() == "It's too dark to read!" );
    }

    SECTION( "an NPC reads the stored book in the dark" ) {
        standard_npc guy( "Reader", who.bub_pos() + tripoint_rel_ms( 1, 0, 0 ) );
        guy.add_env_effect( effect_darkness, body_part_eyes, 3, 1_hours );
        REQUIRE_FALSE( character_funcs::can_see_fine_details( guy ) );
        std::vector<std::string> reasons;
        CHECK( guy.can_read( stored, reasons ) );
        reasons.clear();
        CHECK_FALSE( guy.can_read( paper, reasons ) );
        REQUIRE_FALSE( reasons.empty() );
        CHECK( reasons.back() == "It's too dark to read!" );
    }
}

TEST_CASE( "efile_read_a_stored_book_through_its_use", "[efile][read]" )
{
    start_reading_day();
    avatar &who = carrying_avatar();
    item &reader = powered_device( who, itype_test_ebook_reader, 100 );
    // no skill, so the "read until you gain a level" menu is not shown (not testable headless)
    item &novel = store_file( reader, itype_novel_western );

    // DDA character.cpp:8157: using a book that is a file of an e-device reads it with that device
    REQUIRE( who.invoke_item( &novel ) );
    REQUIRE( who.activity->id() == ACT_READ );
    CHECK( &*who.activity->targets.front() == &novel );
    CHECK( reading_ereader( who ) == &reader );
    CHECK( reader.ammo_remaining() == 99 );
}

TEST_CASE( "efile_read_an_npc_reads_with_a_tablet", "[efile][read]" )
{
    start_reading_day();
    avatar &who = carrying_avatar();
    get_map().invalidate_map_cache( 0 );
    get_map().build_map_cache( 0 );
    standard_npc guy( "Reader", who.bub_pos() + tripoint_rel_ms( 1, 0, 0 ) );
    guy.wear_item( item::spawn( "backpack" ), false );
    REQUIRE( character_funcs::can_see_fine_details( guy ) );
    item &tablet = powered_device( guy, itype_test_eink_tablet_pc, 50 );
    item &stored = store_file( tablet, itype_test_book );
    guy.set_skill_level( skill_id( "computer" ), 1 );

    SECTION( "a stored book is read with the tablet" ) {
        REQUIRE( efile_read_hooks::npc_start_reading( guy, stored, &tablet ) );
        REQUIRE( guy.activity->id() == ACT_READ );
        CHECK( &*guy.activity->targets.front() == &stored );
        REQUIRE( guy.activity->targets.size() == 2 );
        CHECK( &*guy.activity->targets[1] == &tablet );
        CHECK( tablet.ammo_remaining() == 49 );

        activity_handlers::read_do_turn( &*guy.activity, &guy );
        CHECK( guy.activity->id() == ACT_READ );
        CHECK( tablet.ammo_remaining() == 48 );

        tablet.ammo_consume( tablet.ammo_remaining(), guy.bub_pos(), &guy );
        activity_handlers::read_do_turn( &*guy.activity, &guy );
        CHECK( guy.activity->is_null() );
    }

    SECTION( "a paper book is read as before, the tablet is not used" ) {
        item &paper = guy.i_add( item::spawn( itype_test_book ) );
        REQUIRE( efile_read_hooks::npc_start_reading( guy, paper, nullptr ) );
        REQUIRE( guy.activity->id() == ACT_READ );
        CHECK( guy.activity->targets.size() == 1 );
        CHECK( tablet.ammo_remaining() == 50 );
    }

    SECTION( "a book the NPC cannot learn from is refused" ) {
        guy.set_skill_level( skill_id( "computer" ), 9 );
        CHECK_FALSE( efile_read_hooks::npc_start_reading( guy, stored, &tablet ) );
        CHECK( guy.activity->is_null() );
        CHECK( tablet.ammo_remaining() == 50 );
    }
}

TEST_CASE( "efile_read_recipes_from_the_ebooks_of_powered_devices", "[efile][read][recipe]" )
{
    start_reading_day();
    avatar &who = carrying_avatar();
    const recipe &sushi = recipe_sushi_rice.obj();
    who.set_skill_level( sushi.skill_used, 10 );
    REQUIRE_FALSE( who.knows_recipe( &sushi ) );
    item &tablet = powered_device( who, itype_test_eink_tablet_pc, 50 );

    SECTION( "a recipe catalog counts while the device is powered" ) {
        item &catalog = store_file( tablet, itype_efile_recipes );
        catalog.set_saved_recipes( { recipe_sushi_rice } );
        CHECK( ebook_recipes( who ).contains( sushi ) );
        who.invalidate_crafting_inventory();
        CHECK( who.get_available_recipes( who.crafting_inventory() ).contains( sushi ) );
        CHECK_FALSE( who.knows_recipe( &sushi ) );

        tablet.ammo_consume( tablet.ammo_remaining(), who.bub_pos(), &who );
        REQUIRE( tablet.ammo_remaining() == 0 );
        CHECK_FALSE( ebook_recipes( who ).contains( sushi ) );
        who.invalidate_crafting_inventory();
        CHECK_FALSE( who.get_available_recipes( who.crafting_inventory() ).contains( sushi ) );
    }

    SECTION( "a catalog does not count once its device is gone" ) {
        item &catalog = store_file( tablet, itype_efile_recipes );
        catalog.set_saved_recipes( { recipe_sushi_rice } );
        REQUIRE( ebook_recipes( who ).contains( sushi ) );
        {
            detached_ptr<item> gone = tablet.detach();
            CHECK_FALSE( ebook_recipes( who ).contains( sushi ) );
        }
    }

    SECTION( "a catalog entry above the skill does not count" ) {
        who.set_skill_level( sushi.skill_used, 0 );
        REQUIRE( sushi.difficulty > 0 );
        item &catalog = store_file( tablet, itype_efile_recipes );
        catalog.set_saved_recipes( { recipe_sushi_rice } );
        CHECK_FALSE( ebook_recipes( who ).contains( sushi ) );
    }

    SECTION( "the recipes of a book stored on a powered device count" ) {
        const recipe *mutagen = nullptr;
        for( const auto &entry : recipe_dict ) {
            if( entry.second.result() == itype_id( "mutagen_alpha" ) ) {
                mutagen = &entry.second;
                break;
            }
        }
        REQUIRE( mutagen != nullptr );
        for( const Skill &s : Skill::skills ) {
            who.set_skill_level( s.ident(), 10 );
        }
        store_file( tablet, itype_recipe_alpha );
        CHECK( ebook_recipes( who ).contains( *mutagen ) );
        // the plain book walk does not see the stored book (the default walks skip E_FILE)
        who.invalidate_crafting_inventory();
        CHECK_FALSE( who.get_recipes_from_books( who.crafting_inventory() ).contains( *mutagen ) );

        tablet.ammo_consume( tablet.ammo_remaining(), who.bub_pos(), &who );
        CHECK_FALSE( ebook_recipes( who ).contains( *mutagen ) );
    }
}
