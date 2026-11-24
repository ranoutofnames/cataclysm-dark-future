// CDF: PC-E1 step E1-7 (the e-file iuse functions: Manage files, Store books, camera, view photos
// and recipes, book binder). DDA b4b62254ae2 has no test of these functions (its tests only run
// the activities), so every case here is CDF-new. A menu, a popup or look_around cannot be
// answered headless: each iuse function calls the small hooks of namespace efile_iuse_hooks
// (declared below and in src/iuse.cpp, defined there) and the cases test the data and state the
// menus are built from, not the drawing. Messages are stubbed out in the test binary
// (tests/fake_messages.cpp), so a refusal hook returns the wording instead of printing it.
// Written before the code.
#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "detached_ptr.h"
#include "effect.h"
#include "item.h"
#include "item_contents.h"
#include "item_handling_util.h"
#include "itype.h"
#include "map/map.h"
#include "map_helpers.h"
#include "monster.h"
#include "morale_types.h"
#include "npc.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "state_helpers.h"
#include "translations.h"
#include "type_id.h"
#include "units.h"

static const efftype_id effect_blind( "blind" );
static const efftype_id effect_contacts( "contacts" );

static const itype_id itype_battery( "battery" );
static const itype_id itype_efile_map( "efile_map" );
static const itype_id itype_efile_photos( "efile_photos" );
static const itype_id itype_efile_recipes( "efile_recipes" );
static const itype_id itype_paper( "paper" );
static const itype_id itype_pen( "pen" );
static const itype_id itype_test_efile_binder( "test_efile_binder" );
static const itype_id itype_test_efile_camera( "test_efile_camera" );
static const itype_id itype_test_efile_camera_tiny( "test_efile_camera_tiny" );
static const itype_id itype_test_efile_medium( "test_efile_medium" );
static const itype_id itype_test_efile_scan_book_120( "test_efile_scan_book_120" );
static const itype_id itype_test_efile_small( "test_efile_small" );
static const itype_id itype_test_laptop( "test_laptop" );

static const recipe_id recipe_2byarm_guard( "2byarm_guard" );
static const recipe_id recipe_balclava( "balclava" );

static const trait_id trait_HYPEROPIC( "HYPEROPIC" );
static const trait_id trait_ILLITERATE( "ILLITERATE" );
static const trait_id trait_test_efile_no_computers( "test_efile_no_computers" );

// Test hooks of src/iuse.cpp (precedent: tests/efile_menu_test.cpp). Keep the declarations
// identical to the ones in iuse.cpp.
namespace efile_iuse_hooks
{
struct menu_row {
    int id = 0;
    bool enabled = true;
    // 0: no key
    int key = 0;
    std::string text;
    // second column of a table menu (the binder: pages)
    std::string column;
    // description of the entry, or why it is greyed
    std::string desc;
};

std::optional<std::string> efiledevice_refusal( Character &p );
std::optional<std::string> ebooksave_refusal( Character &p );
std::vector<menu_row> efiledevice_rows( item &device );
std::vector<menu_row> camera_rows( item &camera );
item *camera_prepare_gallery( Character &p, item &camera, std::string &refusal );
bool camera_shoot_at( player &p, item &camera, item &gallery, const tripoint_bub_ms &aim_point );
std::vector<menu_row> photo_rows( Character &p, item &gallery );
std::vector<menu_row> monster_photo_rows( item &gallery );
void photo_viewed( Character &p );
std::vector<std::string> view_recipes_names( const item &catalog );
void ebooksave_assign( player &p, item &reader, const drop_locations &to_scan );
std::optional<std::string> binder_add_recipe_refusal( Character &p );
std::vector<menu_row> binder_add_recipe_rows( player &p, item &binder,
        const std::vector<const recipe *> &recipes );
bool binder_start_copy( player &p, item &binder, const recipe_id &rec );
std::vector<menu_row> binder_manage_rows( player &p, const item &binder );
void binder_remove_recipe( player &p, item &binder, const recipe_id &rec,
                           const tripoint_bub_ms &pos );
} // namespace efile_iuse_hooks

namespace
{

using efile_iuse_hooks::menu_row;

auto start_clean() -> void
{
    clear_all_state();
    set_time( calendar::turn_zero + 12_hours );
    clear_map();
    clear_avatar();
}

auto charged_device( avatar &who, const itype_id &type, int charges ) -> item &
{
    item &device = who.i_add( item::spawn( type ) );
    device.ammo_set( itype_battery, charges );
    REQUIRE( device.ammo_remaining() == charges );
    return device;
}

// stores a file in the device's e-file pocket (no memory check) and returns it
auto store_file( item &device, const itype_id &file, bool browsed = true ) -> item &
{
    const std::vector<item *> before = device.efiles();
    REQUIRE( device.put_in( item::spawn( file ), pocket_type::E_FILE_STORAGE, false, nullptr,
                            true ).success() );
    for( item *stored : device.efiles() ) {
        if( std::find( before.begin(), before.end(), stored ) == before.end() ) {
            stored->set_browsed( browsed );
            return *stored;
        }
    }
    FAIL( "the stored file was not found" );
    return device;
}

auto row_with_key( const std::vector<menu_row> &rows, char key ) -> const menu_row &
{
    for( const menu_row &row : rows ) {
        if( row.key == key ) {
            return row;
        }
    }
    FAIL( "no menu row with the key " << key );
    return rows.front();
}

auto keys_of( const std::vector<menu_row> &rows ) -> std::string
{
    std::string keys;
    for( const menu_row &row : rows ) {
        keys += static_cast<char>( row.key );
    }
    return keys;
}

auto make_photo_gallery( avatar &who, int n_photos ) -> detached_ptr<item>
{
    detached_ptr<item> gallery = item::spawn( itype_efile_photos );
    std::vector<item::extended_photo_def> photos;
    for( int i = 0; i < n_photos; ++i ) {
        item::extended_photo_def photo;
        photo.quality = 3;
        photo.name = who.name + " and a zombie";
        photo.description = "Photo number " + std::to_string( i );
        photos.push_back( photo );
    }
    gallery->write_extended_photos( photos, "CAMERA_EXTENDED_PHOTOS" );
    return gallery;
}

} // namespace

// DDA iuse.cpp:5320-5347 (efiledevice) and :8847-8884 (ebooksave): the restrictions and their wording.
TEST_CASE( "efile_iuse_efiledevice_refuses_with_the_dda_wording", "[efile][iuse]" )
{
    start_clean();
    avatar &who = get_avatar();
    CHECK_FALSE( efile_iuse_hooks::efiledevice_refusal( who ).has_value() );

    SECTION( "an NPC is refused without a message" ) {
        npc &guy = spawn_npc( who.bub_pos() + point( 5, 0 ), "test_talker" );
        const std::optional<std::string> refusal = efile_iuse_hooks::efiledevice_refusal( guy );
        REQUIRE( refusal.has_value() );
        CHECK( refusal->empty() );
    }
    SECTION( "underwater" ) {
        who.set_underwater( true );
        const std::optional<std::string> refusal = efile_iuse_hooks::efiledevice_refusal( who );
        who.set_underwater( false );
        REQUIRE( refusal.has_value() );
        CHECK( *refusal == "You can't do that while underwater." );
    }
    SECTION( "CANNOT_USE_COMPUTERS" ) {
        who.set_mutation( trait_test_efile_no_computers );
        const std::optional<std::string> refusal = efile_iuse_hooks::efiledevice_refusal( who );
        who.unset_mutation( trait_test_efile_no_computers );
        REQUIRE( refusal.has_value() );
        CHECK( *refusal == "You push some buttons but have no idea how to get it to work." );
    }
    SECTION( "ILLITERATE" ) {
        who.set_mutation( trait_ILLITERATE );
        const std::optional<std::string> refusal = efile_iuse_hooks::efiledevice_refusal( who );
        who.unset_mutation( trait_ILLITERATE );
        REQUIRE( refusal.has_value() );
        CHECK( *refusal == "You can't read a computer screen." );
    }
    SECTION( "uncorrected HYPEROPIC" ) {
        who.set_mutation( trait_HYPEROPIC );
        const std::optional<std::string> refusal = efile_iuse_hooks::efiledevice_refusal( who );
        REQUIRE( refusal.has_value() );
        CHECK( *refusal == "You'll need to put on reading glasses before you can see the screen." );
        // contact lenses correct it
        who.add_effect( effect_contacts, 1_hours );
        CHECK_FALSE( efile_iuse_hooks::efiledevice_refusal( who ).has_value() );
        who.unset_mutation( trait_HYPEROPIC );
    }
}

TEST_CASE( "efile_iuse_efiledevice_refuses_a_rider", "[efile][iuse]" )
{
    start_clean();
    avatar &who = get_avatar();
    monster &horse = spawn_test_monster( "mon_horse", who.bub_pos() + point( 0, -1 ) );
    who.mount_creature( horse );
    REQUIRE( who.is_mounted() );
    const std::optional<std::string> refusal = efile_iuse_hooks::efiledevice_refusal( who );
    const std::optional<std::string> save_refusal = efile_iuse_hooks::ebooksave_refusal( who );
    who.forced_dismount();
    REQUIRE( refusal.has_value() );
    CHECK( *refusal == "You can't do that while mounted." );
    // iuse::ebooksave has no mounted check (DDA :8847-8897)
    CHECK_FALSE( save_refusal.has_value() );
}

TEST_CASE( "efile_iuse_ebooksave_refuses_with_the_dda_wording", "[efile][iuse]" )
{
    start_clean();
    avatar &who = get_avatar();
    CHECK_FALSE( efile_iuse_hooks::ebooksave_refusal( who ).has_value() );

    SECTION( "CANNOT_USE_COMPUTERS" ) {
        who.set_mutation( trait_test_efile_no_computers );
        const std::optional<std::string> refusal = efile_iuse_hooks::ebooksave_refusal( who );
        who.unset_mutation( trait_test_efile_no_computers );
        REQUIRE( refusal.has_value() );
        CHECK( *refusal == "You push some buttons but it's all meaningless to you." );
    }
    SECTION( "an NPC is refused without a message" ) {
        npc &guy = spawn_npc( who.bub_pos() + point( 5, 0 ), "test_talker" );
        const std::optional<std::string> refusal = efile_iuse_hooks::ebooksave_refusal( guy );
        REQUIRE( refusal.has_value() );
        CHECK( refusal->empty() );
    }
    SECTION( "underwater" ) {
        who.set_underwater( true );
        const std::optional<std::string> refusal = efile_iuse_hooks::ebooksave_refusal( who );
        who.set_underwater( false );
        REQUIRE( refusal.has_value() );
        CHECK( *refusal == "Unfortunately your device is not waterproof." );
    }
    SECTION( "uncorrected HYPEROPIC" ) {
        who.set_mutation( trait_HYPEROPIC );
        const std::optional<std::string> refusal = efile_iuse_hooks::ebooksave_refusal( who );
        who.unset_mutation( trait_HYPEROPIC );
        REQUIRE( refusal.has_value() );
        CHECK( *refusal == "You'll need to put on reading glasses before you can see the screen." );
    }
    SECTION( "too dark to see" ) {
        who.add_effect( effect_blind, 1_hours );
        REQUIRE( who.is_blind() );
        const std::optional<std::string> refusal = efile_iuse_hooks::ebooksave_refusal( who );
        REQUIRE( refusal.has_value() );
        CHECK( *refusal == "You can't see to do that!" );
    }
}

// DDA iuse.cpp:5389-5415: the entries of the Manage files menu, enabled by the state of the device.
TEST_CASE( "efile_iuse_efiledevice_menu_entries_follow_the_state_of_the_device", "[efile][iuse]" )
{
    start_clean();
    avatar &who = get_avatar();
    item &laptop = charged_device( who, itype_test_laptop, 10 );

    SECTION( "a device that was not browsed offers only the two browsing entries" ) {
        laptop.set_browsed( false );
        const std::vector<menu_row> rows = efile_iuse_hooks::efiledevice_rows( laptop );
        CHECK( keys_of( rows ) == "ab" );
        CHECK( row_with_key( rows, 'a' ).enabled );
        CHECK( row_with_key( rows, 'a' ).text == "Browse + move files from all devices" );
        CHECK( row_with_key( rows, 'b' ).enabled );
        CHECK( row_with_key( rows, 'b' ).text == "Browse devices" );
    }
    SECTION( "a browsed device without files cannot read or give files" ) {
        laptop.set_browsed( true );
        const std::vector<menu_row> rows = efile_iuse_hooks::efiledevice_rows( laptop );
        CHECK( keys_of( rows ) == "abremcofpW" );
        CHECK( row_with_key( rows, 'r' ).text == "Read files on this device" );
        CHECK_FALSE( row_with_key( rows, 'r' ).enabled );
        CHECK( row_with_key( rows, 'e' ).enabled );
        CHECK( row_with_key( rows, 'm' ).enabled );
        CHECK( row_with_key( rows, 'c' ).enabled );
        CHECK_FALSE( row_with_key( rows, 'o' ).enabled );
        CHECK_FALSE( row_with_key( rows, 'f' ).enabled );
        CHECK_FALSE( row_with_key( rows, 'p' ).enabled );
        CHECK( row_with_key( rows, 'p' ).text == "Process all map caches on this device (0)" );
        CHECK( row_with_key( rows, 'W' ).enabled );
    }
    SECTION( "a file turns on reading and moving files off the device" ) {
        laptop.set_browsed( true );
        store_file( laptop, itype_test_efile_small );
        const std::vector<menu_row> rows = efile_iuse_hooks::efiledevice_rows( laptop );
        CHECK( row_with_key( rows, 'r' ).enabled );
        CHECK( row_with_key( rows, 'o' ).enabled );
        CHECK( row_with_key( rows, 'f' ).enabled );
        // a plain file is not a map cache
        CHECK_FALSE( row_with_key( rows, 'p' ).enabled );
    }
    SECTION( "map caches count until they are read" ) {
        laptop.set_browsed( true );
        item &cache = store_file( laptop, itype_efile_map );
        store_file( laptop, itype_efile_map );
        std::vector<menu_row> rows = efile_iuse_hooks::efiledevice_rows( laptop );
        CHECK( row_with_key( rows, 'p' ).enabled );
        CHECK( row_with_key( rows, 'p' ).text == "Process all map caches on this device (2)" );
        cache.set_var( "map_cache", "read" );
        rows = efile_iuse_hooks::efiledevice_rows( laptop );
        CHECK( row_with_key( rows, 'p' ).enabled );
        CHECK( row_with_key( rows, 'p' ).text == "Process all map caches on this device (1)" );
        for( item *file : laptop.efiles() ) {
            file->set_var( "map_cache", "read" );
        }
        rows = efile_iuse_hooks::efiledevice_rows( laptop );
        CHECK_FALSE( row_with_key( rows, 'p' ).enabled );
        CHECK( row_with_key( rows, 'p' ).text == "Process all map caches on this device (0)" );
    }
}

// DDA iuse.cpp:6308-6334: the camera menu; a camera whose gallery does not exist has no photos.
TEST_CASE( "efile_iuse_camera_menu_entries_follow_the_gallery", "[efile][iuse][camera]" )
{
    start_clean();
    avatar &who = get_avatar();
    item &camera = charged_device( who, itype_test_efile_camera, 10 );

    std::vector<menu_row> rows = efile_iuse_hooks::camera_rows( camera );
    CHECK( keys_of( rows ) == "tv" );
    CHECK( row_with_key( rows, 't' ).enabled );
    CHECK( row_with_key( rows, 't' ).text == "Take a photo" );
    CHECK_FALSE( row_with_key( rows, 'v' ).enabled );
    CHECK( row_with_key( rows, 'v' ).text == "No photos in memory" );

    store_file( camera, itype_efile_photos );
    rows = efile_iuse_hooks::camera_rows( camera );
    CHECK( row_with_key( rows, 'v' ).enabled );
    CHECK( row_with_key( rows, 'v' ).text == "View photos" );
}

// DDA iuse.cpp:6320-6353: the gallery file is created in the E_FILE pocket when there is room for
// one, and a camera with no room for a new gallery refuses.
TEST_CASE( "efile_iuse_camera_creates_the_gallery_in_the_efile_pocket", "[efile][iuse][camera]" )
{
    start_clean();
    avatar &who = get_avatar();
    std::string refusal;

    SECTION( "room for a gallery: it is created and found again" ) {
        item &camera = charged_device( who, itype_test_efile_camera, 10 );
        REQUIRE( camera.get_photo_gallery() == nullptr );
        item *gallery = efile_iuse_hooks::camera_prepare_gallery( who, camera, refusal );
        REQUIRE( gallery != nullptr );
        CHECK( refusal.empty() );
        CHECK( gallery->typeId() == itype_efile_photos );
        CHECK( camera.get_photo_gallery() == gallery );
        CHECK( camera.efiles().size() == 1 );
        // the same gallery the second time
        CHECK( efile_iuse_hooks::camera_prepare_gallery( who, camera, refusal ) == gallery );
        CHECK( camera.efiles().size() == 1 );
    }
    SECTION( "a memory smaller than a gallery refuses" ) {
        item &camera = charged_device( who, itype_test_efile_camera_tiny, 10 );
        REQUIRE( camera.remaining_ememory() < itype_efile_photos->ememory_size );
        CHECK( efile_iuse_hooks::camera_prepare_gallery( who, camera, refusal ) == nullptr );
        CHECK( refusal == "Your camera cannot hold any more photos." );
        CHECK( camera.efiles().empty() );
    }
    SECTION( "a full memory refuses" ) {
        item &camera = charged_device( who, itype_test_efile_camera, 10 );
        store_file( camera, itype_test_efile_medium );
        REQUIRE( camera.remaining_ememory() < itype_efile_photos->ememory_size );
        CHECK( efile_iuse_hooks::camera_prepare_gallery( who, camera, refusal ) == nullptr );
        CHECK( refusal == "Your camera cannot hold any more photos." );
        CHECK( camera.get_photo_gallery() == nullptr );
    }
    SECTION( "an existing gallery is used even when the memory is full (DDA checks only for a new one)" ) {
        item &camera = charged_device( who, itype_test_efile_camera, 10 );
        item &existing = store_file( camera, itype_efile_photos );
        store_file( camera, itype_test_efile_medium );
        REQUIRE( camera.remaining_ememory() < itype_efile_photos->ememory_size );
        CHECK( efile_iuse_hooks::camera_prepare_gallery( who, camera, refusal ) == &existing );
        CHECK( refusal.empty() );
    }
}

// DDA iuse.cpp:6343-6499: a shot stores the photo in the gallery of the E_FILE pocket, nowhere else.
TEST_CASE( "efile_iuse_camera_stores_the_photo_in_the_gallery", "[efile][iuse][camera]" )
{
    start_clean();
    avatar &who = get_avatar();
    item &camera = charged_device( who, itype_test_efile_camera, 10 );
    std::string refusal;
    item *gallery = efile_iuse_hooks::camera_prepare_gallery( who, camera, refusal );
    REQUIRE( gallery != nullptr );
    REQUIRE( gallery->total_photos() == 0 );
    const tripoint_bub_ms aim = who.bub_pos() + point( 5, 0 );

    SECTION( "a photo of an empty tile" ) {
        const units::ememory before = camera.occupied_ememory();
        CHECK( efile_iuse_hooks::camera_shoot_at( who, camera, *gallery, aim ) );
        CHECK( gallery->total_photos() == 1 );
        // every photo takes the 4 MB of the gallery type
        CHECK( camera.occupied_ememory() == before + itype_efile_photos->ememory_size );
        std::vector<item::extended_photo_def> photos;
        REQUIRE( gallery->read_extended_photos( photos, "CAMERA_EXTENDED_PHOTOS", false ) );
        REQUIRE( photos.size() == 1 );
        CHECK( photos[0].quality >= 0 );
        CHECK( photos[0].quality <= 5 );
        CHECK_FALSE( photos[0].name.empty() );
        CHECK_FALSE( photos[0].description.empty() );

        CHECK( efile_iuse_hooks::camera_shoot_at( who, camera, *gallery, aim ) );
        CHECK( gallery->total_photos() == 2 );
        CHECK( camera.occupied_ememory() == before + 2 * itype_efile_photos->ememory_size );
        CHECK( camera.efiles().size() == 1 );
    }
    SECTION( "the new camera does not use the BN digital media" ) {
        CHECK( efile_iuse_hooks::camera_shoot_at( who, camera, *gallery, aim ) );
        CHECK_FALSE( camera.has_var( "CAMERA_EXTENDED_PHOTOS" ) );
        CHECK_FALSE( camera.has_var( "CAMERA_MONSTER_PHOTOS" ) );
        for( const item *content : camera.contents.all_items_top() ) {
            CHECK( content->typeId().str() != "digital_camera_photo_collection" );
        }
        for( const item *file : camera.efiles() ) {
            CHECK( file->typeId().str() != "digital_camera_photo_collection" );
        }
    }
    SECTION( "a monster in the picture is saved in the monster collection" ) {
        // 5 tiles away: no one_in blinding (dist < 4)
        spawn_test_monster( "mon_zombie", aim );
        CHECK( efile_iuse_hooks::camera_shoot_at( who, camera, *gallery, aim ) );
        CHECK( gallery->total_photos() == 1 );
        CHECK( gallery->get_var( "CAMERA_MONSTER_PHOTOS" ).find( ",mon_zombie," ) != std::string::npos );
    }
}

// DDA iuse.cpp:6252-6306 (show_photo_selection) and :6508-6574 (view_photos): the rows of the
// General and Monsters lists.
TEST_CASE( "efile_iuse_view_photos_lists_general_and_monster_photos", "[efile][iuse][photos]" )
{
    start_clean();
    avatar &who = get_avatar();

    SECTION( "general: the avatar's own name becomes You, the quality is appended" ) {
        detached_ptr<item> gallery = make_photo_gallery( who, 2 );
        const std::vector<menu_row> rows = efile_iuse_hooks::photo_rows( who, *gallery );
        REQUIRE( rows.size() == 2 );
        CHECK( rows[0].text == "You and a zombie [good]" );
        CHECK( rows[0].desc == "Photo number 0" );
        CHECK( rows[1].desc == "Photo number 1" );
        CHECK( rows[0].enabled );
    }
    SECTION( "a gallery without photos has no rows" ) {
        detached_ptr<item> gallery = item::spawn( itype_efile_photos );
        CHECK( efile_iuse_hooks::photo_rows( who, *gallery ).empty() );
    }
    SECTION( "monsters: one row per photographed monster with its quality" ) {
        detached_ptr<item> gallery = item::spawn( itype_efile_photos );
        gallery->set_var( "CAMERA_MONSTER_PHOTOS", ",mon_zombie,3,mon_dog,5," );
        const std::vector<menu_row> rows = efile_iuse_hooks::monster_photo_rows( *gallery );
        REQUIRE( rows.size() == 2 );
        CHECK( rows[0].text == "zombie [good]" );
        CHECK_FALSE( rows[0].desc.empty() );
        CHECK( rows[1].text == monster( mtype_id( "mon_dog" ) ).name() + " [exceptional]" );
    }
    SECTION( "no monster photos, no rows" ) {
        detached_ptr<item> gallery = item::spawn( itype_efile_photos );
        CHECK( efile_iuse_hooks::monster_photo_rows( *gallery ).empty() );
    }
}

// OWNER RULING AUTO-345 (D2): photo viewing keeps CDF's morale (+15..30 per 10 minutes, the
// MORALE_PHOTOS of src/game/photo_morale.cpp), now applied to the DDA gallery viewer.
TEST_CASE( "efile_iuse_viewing_photos_grants_the_photo_morale_once_per_ten_minutes",
           "[efile][iuse][photos]" )
{
    start_clean();
    // earlier cases may have set the 10 minute cooldown of the avatar
    calendar::turn += 1_days;
    avatar &who = get_avatar();
    who.clear_morale();
    REQUIRE_FALSE( who.has_morale( MORALE_PHOTOS ) );

    efile_iuse_hooks::photo_viewed( who );
    const int first = who.get_morale( MORALE_PHOTOS );
    CHECK( first >= 15 );
    CHECK( first <= 30 );

    // the second photo within ten minutes adds nothing
    efile_iuse_hooks::photo_viewed( who );
    CHECK( who.get_morale( MORALE_PHOTOS ) == first );

    // after ten minutes it does
    calendar::turn += 11_minutes;
    efile_iuse_hooks::photo_viewed( who );
    CHECK( who.get_morale( MORALE_PHOTOS ) > first );
    CHECK( who.has_morale( MORALE_PHOTOS ) );
}

// DDA iuse.cpp:6576-6588: the recipe list of a catalog.
TEST_CASE( "efile_iuse_view_recipes_lists_the_catalog", "[efile][iuse][recipes]" )
{
    start_clean();
    detached_ptr<item> catalog = item::spawn( itype_efile_recipes );
    CHECK( efile_iuse_hooks::view_recipes_names( *catalog ).empty() );

    catalog->set_saved_recipes( { recipe_balclava, recipe_2byarm_guard } );
    const std::set<recipe_id> saved = catalog->get_saved_recipes();
    REQUIRE( saved.size() == 2 );
    const std::vector<std::string> names = efile_iuse_hooks::view_recipes_names( *catalog );
    REQUIRE( names.size() == 2 );
    std::vector<std::string> expected;
    for( const recipe_id &rid : saved ) {
        expected.push_back( rid->result_name( true ) );
    }
    CHECK( names == expected );
}

// DDA iuse.cpp:8885-8896: the chosen books become an ebooksave activity on the reader.
TEST_CASE( "efile_iuse_ebooksave_starts_the_scan_activity", "[efile][iuse][ebooksave]" )
{
    start_clean();
    avatar &who = get_avatar();
    item &reader = charged_device( who, itype_test_laptop, 10 );
    item &book = who.i_add( item::spawn( itype_test_efile_scan_book_120 ) );
    drop_locations to_scan;
    to_scan.emplace_back( book, 1 );

    REQUIRE( who.activity->is_null() );
    efile_iuse_hooks::ebooksave_assign( who, reader, to_scan );
    REQUIRE_FALSE( who.activity->is_null() );
    CHECK( who.activity->id() == activity_id( "ACT_EBOOKSAVE" ) );
}

// DDA iuse.cpp:8899-8923: the restrictions of the binder, in DDA's wording.
TEST_CASE( "efile_iuse_binder_add_recipe_refuses_with_the_dda_wording", "[efile][iuse][binder]" )
{
    start_clean();
    avatar &who = get_avatar();
    who.i_add( item::spawn( itype_pen ) );
    CHECK_FALSE( efile_iuse_hooks::binder_add_recipe_refusal( who ).has_value() );

    SECTION( "underwater" ) {
        who.set_underwater( true );
        const std::optional<std::string> refusal = efile_iuse_hooks::binder_add_recipe_refusal( who );
        who.set_underwater( false );
        REQUIRE( refusal.has_value() );
        CHECK( *refusal == "You rethink trying to write underwater." );
    }
    SECTION( "no writing tool" ) {
        clear_avatar();
        const std::optional<std::string> refusal = efile_iuse_hooks::binder_add_recipe_refusal( who );
        REQUIRE( refusal.has_value() );
        CHECK( *refusal == "You do not have anything to write with." );
    }
    SECTION( "too dark to write" ) {
        who.add_effect( effect_blind, 1_hours );
        REQUIRE( who.is_blind() );
        const std::optional<std::string> refusal = efile_iuse_hooks::binder_add_recipe_refusal( who );
        REQUIRE( refusal.has_value() );
        CHECK( *refusal == "It's too dark to write!" );
    }
}

// DDA iuse.cpp:8946-8965: each recipe is an entry, greyed with the reason when it cannot be copied.
// CDF: paper is a GENERIC item, not DDA's ammo, so the room check asks the pocket whether the
// sheets fit (what bookbinder_copy_activity_actor::finish does) instead of remaining_ammo_capacity.
TEST_CASE( "efile_iuse_binder_add_recipe_entries_say_why_a_recipe_cannot_be_copied",
           "[efile][iuse][binder]" )
{
    start_clean();
    avatar &who = get_avatar();
    item &binder = who.i_add( item::spawn( itype_test_efile_binder ) );
    const recipe *recipe_a = &recipe_balclava.obj();
    const int pages = bookbinder_copy_activity_actor::pages_for_recipe( *recipe_a );
    REQUIRE( pages >= 1 );
    const std::vector<const recipe *> recipes{ recipe_a };

    SECTION( "no paper" ) {
        who.i_add( item::spawn( itype_pen ) );
        const std::vector<menu_row> rows = efile_iuse_hooks::binder_add_recipe_rows( who, binder, recipes );
        REQUIRE( rows.size() == 1 );
        CHECK_FALSE( rows[0].enabled );
        CHECK( rows[0].desc == "You do not have enough paper to copy this recipe." );
        CHECK( rows[0].text == recipe_a->result_name() );
        CHECK( rows[0].column == string_format( vgettext( "%1$d page", "%1$d pages", pages ), pages ) );
    }
    SECTION( "everything there: the entry is enabled and has no description" ) {
        who.i_add( item::spawn( itype_paper, calendar::turn, pages ) );
        who.i_add( item::spawn( itype_pen ) );
        const std::vector<menu_row> rows = efile_iuse_hooks::binder_add_recipe_rows( who, binder, recipes );
        REQUIRE( rows.size() == 1 );
        CHECK( rows[0].enabled );
        CHECK( rows[0].desc.empty() );
    }
    SECTION( "the binder is full" ) {
        who.i_add( item::spawn( itype_paper, calendar::turn, pages ) );
        who.i_add( item::spawn( itype_pen ) );
        // the fixture pocket holds 1 L, paper is 5 ml a sheet
        REQUIRE( binder.put_in( item::spawn( itype_paper, calendar::turn, 200 ),
                                pocket_type::MAGAZINE ).success() );
        const std::vector<menu_row> rows = efile_iuse_hooks::binder_add_recipe_rows( who, binder, recipes );
        REQUIRE( rows.size() == 1 );
        CHECK_FALSE( rows[0].enabled );
        CHECK( rows[0].desc == "Your recipe book can not fit this recipe." );
    }
    SECTION( "the binder already has the recipe" ) {
        who.i_add( item::spawn( itype_paper, calendar::turn, pages ) );
        who.i_add( item::spawn( itype_pen ) );
        binder.set_saved_recipes( { recipe_balclava } );
        const std::vector<menu_row> rows = efile_iuse_hooks::binder_add_recipe_rows( who, binder, recipes );
        REQUIRE( rows.size() == 1 );
        CHECK_FALSE( rows[0].enabled );
        CHECK( rows[0].desc == string_format( "Your recipe book already has a recipe for %s.",
                                              recipe_a->result_name() ) );
    }
    SECTION( "the pen has no ink" ) {
        who.i_add( item::spawn( itype_paper, calendar::turn, pages ) );
        item &pen = who.i_add( item::spawn( itype_pen ) );
        pen.ammo_consume( pen.ammo_remaining(), who.bub_pos(), &who );
        REQUIRE( pen.ammo_remaining() == 0 );
        const std::vector<menu_row> rows = efile_iuse_hooks::binder_add_recipe_rows( who, binder, recipes );
        REQUIRE( rows.size() == 1 );
        CHECK_FALSE( rows[0].enabled );
        CHECK( rows[0].desc == "Your writing tools do not have enough charges." );
    }
}

// DDA iuse.cpp:8975-8976: the chosen recipe becomes a bookbinder_copy activity on the binder.
TEST_CASE( "efile_iuse_binder_add_recipe_starts_the_copy_activity", "[efile][iuse][binder]" )
{
    start_clean();
    avatar &who = get_avatar();
    item &binder = who.i_add( item::spawn( itype_test_efile_binder ) );
    REQUIRE( who.activity->is_null() );
    CHECK( efile_iuse_hooks::binder_start_copy( who, binder, recipe_balclava ) );
    REQUIRE_FALSE( who.activity->is_null() );
    CHECK( who.activity->id() == activity_id( "ACT_BINDER_COPY_RECIPE" ) );
}

// DDA iuse.cpp:8981-9026 (binder_manage_recipe): the saved recipes are the entries (a known recipe
// is marked), removing one frees its pages of paper.
TEST_CASE( "efile_iuse_binder_manage_removes_a_recipe_and_its_paper", "[efile][iuse][binder]" )
{
    start_clean();
    avatar &who = get_avatar();
    item &binder = who.i_add( item::spawn( itype_test_efile_binder ) );
    const int pages = bookbinder_copy_activity_actor::pages_for_recipe( *recipe_balclava );
    REQUIRE( binder.put_in( item::spawn( itype_paper, calendar::turn, pages + 3 ),
                            pocket_type::MAGAZINE ).success() );
    binder.set_saved_recipes( { recipe_balclava } );

    const std::vector<menu_row> rows = efile_iuse_hooks::binder_manage_rows( who, binder );
    REQUIRE( rows.size() == 1 );
    CHECK( rows[0].enabled );
    CHECK( rows[0].text.find( recipe_balclava->result_name() ) == 0 );
    CHECK( rows[0].column == string_format( vgettext( "%1$d page", "%1$d pages", pages ), pages ) );

    auto paper_sheets = [&binder]() {
        int sheets = 0;
        for( const item *it : binder.contents.all_items_top( pocket_type::MAGAZINE ) ) {
            if( it->typeId() == itype_paper ) {
                sheets += it->charges;
            }
        }
        return sheets;
    };
    REQUIRE( paper_sheets() == pages + 3 );
    efile_iuse_hooks::binder_remove_recipe( who, binder, recipe_balclava, who.bub_pos() );
    CHECK( binder.get_saved_recipes().empty() );
    CHECK( paper_sheets() == 3 );
}

// An empty binder has nothing to manage (DDA :8992-8995 says "You have no recipes to manage.").
TEST_CASE( "efile_iuse_binder_manage_without_recipes_has_no_entries", "[efile][iuse][binder]" )
{
    start_clean();
    avatar &who = get_avatar();
    item &binder = who.i_add( item::spawn( itype_test_efile_binder ) );
    CHECK( efile_iuse_hooks::binder_manage_rows( who, binder ).empty() );
}
