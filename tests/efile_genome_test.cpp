// CDF: PC-E1 step E1-11a (the last consumers of the BN digital-media C++ move onto the e-file
// engine). Owner ruling O4 (E1-PLAN.md section 8): a genome sample ready for cloning is a HUGE e-file
// (digital_genome_sample, 4 TB, one itype for partial and complete samples), larger than every ordinary
// device, so it fits only the genome drives (8 TB E_FILE pocket); the drives are not locked to
// genome data. Replaces the two genome cases deleted with tests/digital_media_test.cpp
// (genome_workstation_finds_samples_inside_storage and legacy_genome_drives_migrate_to_copyable_sample_files).
// DDA has neither the cloning syringe nor the DNA editor, so every case here is CDF-new.
// The syringe and the editor ask menus, which cannot be answered headless: they call the small hooks of
// namespace efile_genome_hooks (declared below, defined in src/iuse_actor.cpp, no header), and the cases
// test the state those menus are built from.
// The same file holds the other consumers re-pointed in E1-11a: the Robofac mind scan on the data card
// (a find-item mission for an e-file that is not software; src/game/mission.cpp) and the file use action
// manualnoise (hello_world.exe, a stored file that costs no charges; src/iuse_actor.cpp).
// Written before the code.
#include <string>
#include <vector>

#include "activity_handlers.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_utility.h"
#include "catch/catch.hpp"
#include "character.h"
#include "detached_ptr.h"
#include "iuse.h"
#include "item.h"
#include "item_contents.h"
#include "itype.h"
#include "map_helpers.h"
#include "mission.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"

namespace efile_genome_hooks
{
// src/iuse_actor.cpp: the genome sample files of every e-device the character carries; a drive that
// still carries the BN specimen vars is converted first
auto samples( Character &who ) -> std::vector<item *>;
// the carried e-devices with room for one more sample file (the syringe's "store on which device" menu)
auto storage_for_new_sample( Character &who ) -> std::vector<item *>;
// the carried e-devices a copy of @p sample can go to: room for the file and no equal copy yet
auto copy_targets( Character &who, const item &sample ) -> std::vector<item *>;
// a first sample (progress 1) of @p specimen_id into the E_FILE pocket of @p device
auto store_new_sample( item &device, const std::string &specimen_id,
                       const std::string &specimen_name ) -> bool;
// a copy of @p sample into the E_FILE pocket of @p device
auto copy_sample_to( const item &sample, item &device ) -> bool;
// the BN legacy form (specimen vars on the drive) becomes a sample file inside the drive
auto convert_legacy_drive( item &drive ) -> bool;
} // namespace efile_genome_hooks

static const itype_id itype_battery( "battery" );
static const itype_id itype_data_card( "data_card" );
static const itype_id itype_digital_genome_sample( "digital_genome_sample" );
static const itype_id itype_digital_hello_world( "digital_hello_world" );
static const itype_id itype_genome_drive( "genome_drive" );
static const itype_id itype_genome_drive_black_rat( "genome_drive_black_rat" );
static const itype_id itype_genome_drive_wolf_mutant_huge( "genome_drive_wolf_mutant_huge" );
static const itype_id itype_hard_drive( "hard_drive" );
static const itype_id itype_mind_scan_robofac( "mind_scan_robofac" );
static const itype_id itype_software_hacking( "software_hacking" );
static const itype_id itype_test_laptop( "test_laptop" );
static const itype_id itype_usb_drive( "usb_drive" );

static const mission_type_id mission_TEST_EFILE_FIND_MIND_SCAN( "TEST_EFILE_FIND_MIND_SCAN" );

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

auto files_of( const item &device, const itype_id &id ) -> std::vector<item *>
{
    std::vector<item *> found;
    for( const item *file : device.efiles() ) {
        if( file->typeId() == id ) {
            found.push_back( const_cast<item *>( file ) );
        }
    }
    return found;
}

// a complete-looking sample file of the given specimen inside @p device (browsed, as the syringe makes it)
auto store_sample( item &device, const std::string &specimen, int progress ) -> item &
{
    detached_ptr<item> file = item::spawn( itype_digital_genome_sample );
    file->set_var( "specimen_sample", specimen );
    file->set_var( "specimen_sample_progress", std::to_string( progress ) );
    file->set_var( "specimen_name", "test specimen" );
    file->set_browsed( true );
    REQUIRE( device.put_in( std::move( file ), pocket_type::E_FILE_STORAGE, false, nullptr,
                            true ).success() );
    const std::vector<item *> samples = files_of( device, itype_digital_genome_sample );
    REQUIRE_FALSE( samples.empty() );
    return *samples.back();
}

auto contains( const std::vector<item *> &list, const item &what ) -> bool
{
    return std::find( list.begin(), list.end(), &what ) != list.end();
}

auto sample_size() -> units::ememory
{
    return itype_digital_genome_sample->ememory_size;
}

} // namespace

TEST_CASE( "efile_genome_a_sample_is_larger_than_every_ordinary_device", "[efile][genome]" )
{
    avatar &who = carrying_avatar();
    item &genome = who.i_add( item::spawn( itype_genome_drive ) );
    item &hard = who.i_add( item::spawn( itype_hard_drive ) );
    item &usb = who.i_add( item::spawn( itype_usb_drive ) );
    item &card = who.i_add( item::spawn( itype_data_card ) );

    // ruling O4: ready-for-cloning is huge, one size for partial and complete samples
    CHECK( sample_size() > hard.total_ememory() );
    CHECK( sample_size() > card.total_ememory() );
    CHECK( sample_size() <= genome.total_ememory() );

    const std::vector<item *> room = efile_genome_hooks::storage_for_new_sample( who );
    CHECK( contains( room, genome ) );
    CHECK_FALSE( contains( room, hard ) );
    CHECK_FALSE( contains( room, usb ) );
    CHECK_FALSE( contains( room, card ) );
    CHECK( room.size() == 1 );
}

TEST_CASE( "efile_genome_samples_are_found_inside_the_storage_of_a_carried_device",
           "[efile][genome]" )
{
    avatar &who = carrying_avatar();
    CHECK( efile_genome_hooks::samples( who ).empty() );

    item &genome = who.i_add( item::spawn( itype_genome_drive ) );
    CHECK( efile_genome_hooks::samples( who ).empty() );

    item &sample = store_sample( genome, "mon_black_rat", 2 );
    const std::vector<item *> found = efile_genome_hooks::samples( who );
    REQUIRE( found.size() == 1 );
    CHECK( found.front() == &sample );
    CHECK( found.front()->get_var( "specimen_sample" ) == "mon_black_rat" );

    SECTION( "another file on the same drive is no sample" ) {
        REQUIRE( genome.put_in( item::spawn( itype_software_hacking ), pocket_type::E_FILE_STORAGE,
                                false, nullptr, true ).success() );
        CHECK( efile_genome_hooks::samples( who ).size() == 1 );
    }

    SECTION( "a second device with a second sample" ) {
        item &second = who.i_add( item::spawn( itype_genome_drive ) );
        item &other = store_sample( second, "mon_wolf_mutant_huge", 5 );
        const std::vector<item *> both = efile_genome_hooks::samples( who );
        CHECK( both.size() == 2 );
        CHECK( contains( both, sample ) );
        CHECK( contains( both, other ) );
    }

    SECTION( "the genome drive takes ordinary files as well (ruling O4, not locked to genome data)" ) {
        REQUIRE( genome.put_in( item::spawn( itype_software_hacking ), pocket_type::E_FILE_STORAGE,
                                false, nullptr, true ).success() );
        CHECK( files_of( genome, itype_software_hacking ).size() == 1 );
        CHECK( genome.remaining_ememory() >= 0_KB );
    }
}

TEST_CASE( "efile_genome_a_legacy_drive_converts_its_specimen_vars_into_a_sample_file",
           "[efile][genome]" )
{
    avatar &who = carrying_avatar();
    item &rat = who.i_add( item::spawn( itype_genome_drive_black_rat ) );
    item &wolf = who.i_add( item::spawn( itype_genome_drive_wolf_mutant_huge ) );
    // the BN legacy form: the specimen sits on the drive, no file inside
    REQUIRE( rat.get_var( "specimen_sample" ) == "mon_black_rat" );
    REQUIRE( rat.efiles().empty() );
    REQUIRE( wolf.efiles().empty() );

    const std::vector<item *> found = efile_genome_hooks::samples( who );
    REQUIRE( found.size() == 2 );

    // the vars moved to the file; the drive is an ordinary drive again
    CHECK_FALSE( rat.has_var( "specimen_sample" ) );
    CHECK_FALSE( wolf.has_var( "specimen_sample" ) );
    REQUIRE( files_of( rat, itype_digital_genome_sample ).size() == 1 );
    REQUIRE( files_of( wolf, itype_digital_genome_sample ).size() == 1 );
    const item &rat_file = *files_of( rat, itype_digital_genome_sample ).front();
    const item &wolf_file = *files_of( wolf, itype_digital_genome_sample ).front();
    CHECK( rat_file.get_var( "specimen_sample" ) == "mon_black_rat" );
    CHECK( rat_file.get_var( "specimen_sample_progress", 0 ) == 2 );
    CHECK( rat_file.get_var( "specimen_name" ) == "black rat" );
    CHECK( wolf_file.get_var( "specimen_sample" ) == "mon_wolf_mutant_huge" );
    CHECK( wolf_file.get_var( "specimen_sample_progress", 0 ) == 5 );
    CHECK( wolf_file.get_var( "specimen_name" ) == "dire wolf" );

    SECTION( "the conversion happens once" ) {
        CHECK( efile_genome_hooks::samples( who ).size() == 2 );
        CHECK( files_of( rat, itype_digital_genome_sample ).size() == 1 );
        CHECK_FALSE( efile_genome_hooks::convert_legacy_drive( rat ) );
        CHECK( files_of( rat, itype_digital_genome_sample ).size() == 1 );
    }

    SECTION( "a drive without specimen vars converts nothing" ) {
        item &plain = who.i_add( item::spawn( itype_genome_drive ) );
        CHECK_FALSE( efile_genome_hooks::convert_legacy_drive( plain ) );
        CHECK( plain.efiles().empty() );
    }
}

TEST_CASE( "efile_genome_new_samples_and_copies_are_files_on_a_device_with_room", "[efile][genome]" )
{
    avatar &who = carrying_avatar();
    item &first = who.i_add( item::spawn( itype_genome_drive ) );
    item &second = who.i_add( item::spawn( itype_genome_drive ) );
    item &hard = who.i_add( item::spawn( itype_hard_drive ) );

    REQUIRE( efile_genome_hooks::store_new_sample( first, "mon_black_rat", "black rat" ) );
    REQUIRE( files_of( first, itype_digital_genome_sample ).size() == 1 );
    item &sample = *files_of( first, itype_digital_genome_sample ).front();
    CHECK( sample.get_var( "specimen_sample" ) == "mon_black_rat" );
    CHECK( sample.get_var( "specimen_sample_progress", 0 ) == 1 );
    CHECK( sample.get_var( "specimen_name" ) == "black rat" );
    CHECK( first.remaining_ememory() == first.total_ememory() - sample_size() );

    // the 8 TB pocket is full of one 4 TB sample twice over at most: a third does not fit
    CHECK( contains( efile_genome_hooks::storage_for_new_sample( who ), first ) );
    REQUIRE( efile_genome_hooks::store_new_sample( first, "mon_wolf_mutant_huge", "dire wolf" ) );
    CHECK_FALSE( contains( efile_genome_hooks::storage_for_new_sample( who ), first ) );

    SECTION( "a copy goes only where there is room and no equal copy yet" ) {
        const std::vector<item *> targets = efile_genome_hooks::copy_targets( who, sample );
        CHECK( contains( targets, second ) );
        CHECK_FALSE( contains( targets, first ) );
        CHECK_FALSE( contains( targets, hard ) );

        REQUIRE( efile_genome_hooks::copy_sample_to( sample, second ) );
        REQUIRE( files_of( second, itype_digital_genome_sample ).size() == 1 );
        const item &copy = *files_of( second, itype_digital_genome_sample ).front();
        CHECK( copy.get_var( "specimen_sample" ) == "mon_black_rat" );
        CHECK( copy.get_var( "specimen_sample_progress", 0 ) == 1 );
        // the original stays: a copy, not a move
        CHECK( files_of( first, itype_digital_genome_sample ).size() == 2 );
        CHECK_FALSE( contains( efile_genome_hooks::copy_targets( who, sample ), second ) );
    }

    SECTION( "progress on a sample is a var change on the file in place" ) {
        sample.set_var( "specimen_sample_progress", std::to_string( 3 ) );
        const std::vector<item *> found = efile_genome_hooks::samples( who );
        REQUIRE( found.size() == 2 );
        CHECK( contains( found, sample ) );
        CHECK( sample.get_var( "specimen_sample_progress", 0 ) == 3 );
    }
}

TEST_CASE( "efile_genome_a_mind_scan_lands_on_the_data_card_and_the_mission_finds_it",
           "[efile][genome][mission]" )
{
    avatar &who = carrying_avatar();
    item &card = who.i_add( item::spawn( itype_data_card ) );
    REQUIRE( card.is_estorage() );
    // the missions of the avatar outlive the case: the NPC talk cases count them
    const on_out_of_scope forget_missions( [&who]() {
        who.reset_all_missions();
    } );
    mission *miss = mission::reserve_new( mission_TEST_EFILE_FIND_MIND_SCAN, character_id() );
    REQUIRE( miss != nullptr );
    REQUIRE( miss->get_type().goal == MGOAL_FIND_ITEM );
    if( miss->get_assigned_player_id() == who.getID() ) {
        who.on_mission_assignment( *miss );
    } else {
        miss->assign( who );
    }
    CHECK_FALSE( miss->is_complete( character_id() ) );

    // the mind splicer: ACT_MIND_SPLICER ends by putting the scan on the card
    player_activity act( activity_id( "ACT_MIND_SPLICER" ), 100 );
    act.targets.emplace_back( &card );
    activity_handlers::mind_splicer_finish( &act, &who );
    REQUIRE( files_of( card, itype_mind_scan_robofac ).size() == 1 );
    CHECK( card.remaining_ememory() >= 0_KB );

    who.invalidate_crafting_inventory();
    CHECK( miss->is_complete( character_id() ) );

    SECTION( "wrapping the mission up takes the file, the card stays" ) {
        miss->wrap_up();
        CHECK( files_of( card, itype_mind_scan_robofac ).empty() );
        CHECK( who.has_item( card ) );
    }

    SECTION( "a card that is gone completes nothing" ) {
        detached_ptr<item> gone = card.detach();
        who.invalidate_crafting_inventory();
        CHECK_FALSE( miss->is_complete( character_id() ) );
    }
}

TEST_CASE( "efile_genome_a_stored_file_use_action_costs_no_charges", "[efile][genome]" )
{
    avatar &who = carrying_avatar();
    item &laptop = who.i_add( item::spawn( itype_test_laptop ) );
    REQUIRE( laptop.put_in( item::spawn( itype_digital_hello_world ), pocket_type::E_FILE_STORAGE,
                            false, nullptr, true ).success() );
    const std::vector<item *> files = files_of( laptop, itype_digital_hello_world );
    REQUIRE( files.size() == 1 );
    item &file = *files.front();
    // a file is not a tool: the reader pays, the file has no charges of its own
    REQUIRE_FALSE( file.is_tool() );
    const use_function *use = file.get_use( "manualnoise" );
    REQUIRE( use != nullptr );
    CHECK( use->can_call( who, file, false, who.bub_pos() ).success() );
}
