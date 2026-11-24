// CDF: PC-E1 step E1-9 (crafting, missions, computers). Tests for DDA b4b62254ae2
// character_inventory.cpp:222-275 (count_softwares, has_software), crafting.cpp:4145, 4493-4505
// (software as an item component, consume_software_container), mission.cpp:522, 623-642 (a
// find-item mission consumes software stored on e-devices), computer_session.cpp:294-306,
// 1000-1020 (pick_estorage, the download into the E_FILE pocket) and the plan's risk R4 (a software
// as a recipe TOOL). Written before the code.
// DEVIATION: DDA has no test of any of these, so every case is CDF-new. The software is the E1-3
// fixture test_software_fixture (the CDF software_* ids are BN GENERIC items until E1-10a flips
// them). The computer download menu cannot be driven headless: computer_session.cpp calls the hooks
// of namespace efile_computer_hooks, which the cases call directly. The mission cases build the
// mission as tests/npc_talk_test.cpp does.
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_utility.h"
#include "catch/catch.hpp"
#include "craft_command.h"
#include "detached_ptr.h"
#include "item.h"
#include "item_contents.h"
#include "itype.h"
#include "map/map.h"
#include "map_helpers.h"
#include "mission.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "requirements.h"
#include "state_helpers.h"
#include "temp_crafting_inventory.h"
#include "type_id.h"
#include "units.h"

namespace efile_computer_hooks
{
// src/computer_session.cpp: the filter of pick_estorage( can_hold_ememory ) (DDA :294-298)
auto estorage_with_room( const item &it, units::ememory size ) -> bool;
// the success part of action_download_software (DDA :1010-1014): the software goes into the
// E_FILE pocket of @p device for 0.3 seconds of moves; the message line it prints
auto download_to( Character &who, item &device, const itype_id &software_id,
                  int mission_id ) -> std::string;
// the error line when no device could hold the software (DDA :1016-1017)
auto no_device_message( const itype_id &software_id ) -> std::string;
} // namespace efile_computer_hooks

static const itype_id itype_battery( "battery" );
static const itype_id itype_efile_junk( "efile_junk" );
static const itype_id itype_test_ereader_tiny( "test_ereader_tiny" );
static const itype_id itype_test_laptop( "test_laptop" );
static const itype_id itype_test_software_fixture( "test_software_fixture" );
static const itype_id itype_rock( "rock" );

static const mission_type_id mission_TEST_EFILE_FIND_SOFTWARE( "TEST_EFILE_FIND_SOFTWARE" );

static const requirement_id requirement_test_efile_software_tool( "test_efile_software_tool" );

namespace
{

auto carrying_avatar() -> avatar &
{
    clear_all_state();
    avatar &who = get_avatar();
    clear_avatar();
    who.wear_item( item::spawn( "backpack" ), false );
    return who;
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

auto laptop_with( Character &who, int charges ) -> item &
{
    item &laptop = who.i_add( item::spawn( itype_test_laptop ) );
    laptop.ammo_set( itype_battery, charges );
    REQUIRE( laptop.ammo_remaining() == charges );
    return laptop;
}

auto software_count( const item &device ) -> int
{
    int count = 0;
    for( const item *file : device.efiles() ) {
        if( file->typeId() == itype_test_software_fixture ) {
            ++count;
        }
    }
    return count;
}

auto anything_goes( const item & ) -> bool
{
    return true;
}

} // namespace

TEST_CASE( "efile_mission_count_softwares_counts_the_files_of_carried_devices", "[efile][mission]" )
{
    avatar &who = carrying_avatar();
    REQUIRE( who.count_softwares( itype_test_software_fixture ) == 0 );
    item &laptop = laptop_with( who, 10 );
    CHECK( who.count_softwares( itype_test_software_fixture ) == 0 );

    store_file( laptop, itype_test_software_fixture );
    CHECK( who.count_softwares( itype_test_software_fixture ) == 1 );

    SECTION( "every copy on every device counts" ) {
        store_file( laptop, itype_test_software_fixture );
        item &second = laptop_with( who, 10 );
        store_file( second, itype_test_software_fixture );
        CHECK( who.count_softwares( itype_test_software_fixture ) == 3 );
    }

    SECTION( "only the asked id counts" ) {
        CHECK( who.count_softwares( itype_rock ) == 0 );
    }

    SECTION( "a device on the ground is not carried" ) {
        detached_ptr<item> gone = laptop.detach();
        item &dropped = *gone;
        get_map().add_item( who.bub_pos(), std::move( gone ) );
        REQUIRE( software_count( dropped ) == 1 );
        CHECK( who.count_softwares( itype_test_software_fixture ) == 0 );
    }
}

TEST_CASE( "efile_mission_has_software_asks_for_a_powered_device", "[efile][mission]" )
{
    avatar &who = carrying_avatar();
    item &laptop = laptop_with( who, 3 );
    store_file( laptop, itype_test_software_fixture );

    CHECK( who.has_software( itype_test_software_fixture ) );
    CHECK( who.has_software( itype_test_software_fixture, 3 ) );
    CHECK_FALSE( who.has_software( itype_test_software_fixture, 4 ) );
    CHECK( who.has_software( itype_test_software_fixture, 0, itype_test_laptop ) );
    CHECK_FALSE( who.has_software( itype_test_software_fixture, 0, itype_test_ereader_tiny ) );
    CHECK_FALSE( who.has_software( itype_rock ) );
}

// DDA-parity: DDA crafting.cpp:4493-4505 calls remove_item() on the e-DEVICE that stores the software
// (the function is named "container", AUTO-370a), so the whole carrying device goes, with every file
// on it. The E1-9 version of this case expected only the file to go. After a consume the device is
// destroyed: the cases never touch the old reference again.
TEST_CASE( "efile_mission_consuming_software_removes_the_whole_device", "[efile][mission]" )
{
    avatar &who = carrying_avatar();
    item &laptop = laptop_with( who, 10 );
    CHECK_FALSE( who.consume_software_container( itype_test_software_fixture ) );
    // nothing was consumed: the device without the software stays
    CHECK( who.has_item( laptop ) );

    store_file( laptop, itype_test_software_fixture );
    store_file( laptop, itype_test_software_fixture );
    store_file( laptop, itype_efile_junk );
    REQUIRE( software_count( laptop ) == 2 );

    SECTION( "the device that carries the software goes with all its files" ) {
        CHECK( who.consume_software_container( itype_test_software_fixture ) );
        CHECK( who.all_items_with_id( itype_test_laptop ).empty() );
        CHECK( who.count_softwares( itype_test_software_fixture ) == 0 );
        CHECK_FALSE( who.consume_software_container( itype_test_software_fixture ) );
    }

    SECTION( "only one device goes per call" ) {
        item &second = laptop_with( who, 10 );
        store_file( second, itype_test_software_fixture );
        REQUIRE( who.all_items_with_id( itype_test_laptop ).size() == 2 );
        CHECK( who.consume_software_container( itype_test_software_fixture ) );
        CHECK( who.all_items_with_id( itype_test_laptop ).size() == 1 );
        CHECK( who.count_softwares( itype_test_software_fixture ) >= 1 );
        CHECK( who.consume_software_container( itype_test_software_fixture ) );
        CHECK( who.all_items_with_id( itype_test_laptop ).empty() );
        CHECK_FALSE( who.consume_software_container( itype_test_software_fixture ) );
    }

    SECTION( "a device without the software is not touched" ) {
        item &plain = laptop_with( who, 10 );
        REQUIRE( who.all_items_with_id( itype_test_laptop ).size() == 2 );
        CHECK( who.consume_software_container( itype_test_software_fixture ) );
        REQUIRE( who.all_items_with_id( itype_test_laptop ).size() == 1 );
        CHECK( who.all_items_with_id( itype_test_laptop ).front() == &plain );
    }
}

TEST_CASE( "efile_mission_a_find_item_mission_wants_the_file_on_a_laptop", "[efile][mission]" )
{
    avatar &who = carrying_avatar();
    item &laptop = laptop_with( who, 10 );
    // the missions of the avatar outlive the case: the NPC talk cases count them
    const on_out_of_scope forget_missions( [&who]() {
        who.reset_all_missions();
    } );
    mission *miss = mission::reserve_new( mission_TEST_EFILE_FIND_SOFTWARE, character_id() );
    REQUIRE( miss != nullptr );
    REQUIRE( miss->get_type().goal == MGOAL_FIND_ITEM );
    if( miss->get_assigned_player_id() == who.getID() ) {
        who.on_mission_assignment( *miss );
    } else {
        miss->assign( who );
    }

    CHECK_FALSE( miss->is_complete( character_id() ) );

    store_file( laptop, itype_test_software_fixture );
    who.invalidate_crafting_inventory();
    CHECK( miss->is_complete( character_id() ) );

    // DDA-parity: the mission consumes through consume_software_container (DDA mission.cpp:519-533),
    // which removes the whole device that carries the software (AUTO-370a)
    SECTION( "wrapping the mission up consumes the laptop that carries the file" ) {
        miss->wrap_up();
        CHECK( who.count_softwares( itype_test_software_fixture ) == 0 );
        CHECK( who.all_items_with_id( itype_test_laptop ).empty() );
    }

    SECTION( "a laptop that is gone completes nothing" ) {
        detached_ptr<item> gone = laptop.detach();
        who.invalidate_crafting_inventory();
        CHECK_FALSE( miss->is_complete( character_id() ) );
    }
}

TEST_CASE( "efile_mission_a_recipe_component_can_be_software_on_a_device", "[efile][mission]" )
{
    avatar &who = carrying_avatar();
    item &laptop = laptop_with( who, 10 );
    const std::vector<item_comp> components = { item_comp( itype_test_software_fixture, 1 ) };
    temp_crafting_inventory map_inv;

    who.invalidate_crafting_inventory();
    comp_selection<item_comp> without = who.select_item_component( components, 1, map_inv, false,
                                        anything_goes );
    CHECK( without.use_from != usage_from::player );

    store_file( laptop, itype_test_software_fixture );
    who.invalidate_crafting_inventory();
    comp_selection<item_comp> with = who.select_item_component( components, 1, map_inv, false,
                                     anything_goes );
    CHECK( with.use_from == usage_from::player );
    CHECK( with.comp.type == itype_test_software_fixture );
}

// risk R4 of the plan: core recipes use software as a TOOL of a requirement (data/json/requirements/
// toolsets.json:177, 183, 189: hacking_standard, utility_software_standard, math_software_standard)
// through "using" with the factor -1 (recipe::add_requirements multiplies the requirement). The
// default item walks skip installed files, in DDA too (its visit_contents walks only CONTAINER
// pockets), so the crafting inventory has to offer them: DDA has no such recipe (it asks
// u_has_software in effects), CDF keeps them (BN-only use, bridged).
TEST_CASE( "efile_mission_software_on_a_device_is_a_recipe_tool", "[efile][mission]" )
{
    avatar &who = carrying_avatar();
    item &laptop = laptop_with( who, 10 );
    const requirement_data reqs = requirement_test_efile_software_tool.obj() *
                                  static_cast<unsigned>( -1 );
    REQUIRE( reqs.get_tools().size() == 1 );
    REQUIRE( reqs.get_tools().front().front().count == -1 );

    who.invalidate_crafting_inventory();
    CHECK_FALSE( reqs.can_make_with_inventory( who.crafting_inventory(), anything_goes ) );

    store_file( laptop, itype_test_software_fixture );
    who.invalidate_crafting_inventory();
    CHECK( reqs.can_make_with_inventory( who.crafting_inventory(), anything_goes ) );

    SECTION( "and not once the file is consumed" ) {
        REQUIRE( who.consume_software_container( itype_test_software_fixture ) );
        who.invalidate_crafting_inventory();
        CHECK_FALSE( reqs.can_make_with_inventory( who.crafting_inventory(), anything_goes ) );
    }
}

TEST_CASE( "efile_mission_a_download_needs_a_device_with_room", "[efile][mission]" )
{
    avatar &who = carrying_avatar();
    item &laptop = laptop_with( who, 10 );
    item &tiny = who.i_add( item::spawn( itype_test_ereader_tiny ) );
    detached_ptr<item> probe = item::spawn( itype_test_software_fixture );
    const units::ememory size = probe->ememory_size();
    REQUIRE( size > 0_KB );

    SECTION( "the picker offers only e-storage devices with enough free memory" ) {
        CHECK( efile_computer_hooks::estorage_with_room( laptop, size ) );
        CHECK_FALSE( efile_computer_hooks::estorage_with_room( tiny, size ) );
        CHECK_FALSE( efile_computer_hooks::estorage_with_room( *probe, size ) );
        // a device that is full no longer offers itself
        CHECK_FALSE( efile_computer_hooks::estorage_with_room( laptop, laptop.remaining_ememory() +
                     1_KB ) );
    }

    SECTION( "the refusal names the free memory that is required" ) {
        CHECK( efile_computer_hooks::no_device_message( itype_test_software_fixture ) ==
               string_format( "Electronic storage device with %s free required!",
                              units::display( size ) ) );
    }

    SECTION( "the download goes into the E_FILE pocket and costs 0.3 seconds" ) {
        const int moves_before = who.get_moves();
        const std::string line = efile_computer_hooks::download_to( who, laptop,
                                 itype_test_software_fixture, 7 );
        CHECK( line == string_format( "%s downloaded.", probe->tname() ) );
        CHECK( who.get_moves() == moves_before - 30 );
        REQUIRE( software_count( laptop ) == 1 );
        CHECK( laptop.efiles().front()->mission_id == 7 );
        CHECK( laptop.occupied_ememory() == size );
        CHECK( who.count_softwares( itype_test_software_fixture ) == 1 );
        // the earlier files stay: a download no longer wipes the device (the old USB rule)
        efile_computer_hooks::download_to( who, laptop, itype_test_software_fixture, 8 );
        CHECK( software_count( laptop ) == 2 );
    }
}
