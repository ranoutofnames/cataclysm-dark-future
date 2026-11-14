#include "ablative_coverage.h"
#include "activity_actor_definitions.h"
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "debug.h"
#include "flag.h"
#include "game_inventory.h"
#include "item.h"
#include "item_contents.h"
#include "item_handling_util.h"
#include "item_pocket.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "pocket_data.h"
#include "safe_reference.h"
#include "state_helpers.h"
#include "type_id.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// CDF: VEST1 (owner bug report: Insert refused a plate for a WORN "US ballistic vest").
// game_inventory_pockets_test.cpp pins the preset for an UNWORN vest only. These tests go the whole
// way for a vest that is worn on the torso: the denial predicate of the Insert picker
// (game_menus::inv::insert_preset_denial, the preset that gates the selection) and then the very
// activity the picker assigns (insert_item_activity_actor), run to its end by the activity loop.
// Outcomes asserted: where the plate ended up (which plate slot of the vest), that the original is
// gone from where it was carried, and that exactly one plate exists afterwards.
// Real ids: ballistic_vest_esapi has two ABLATIVE_LARGE and two ABLATIVE_MEDIUM plate pockets
// (data/json/items/armor/cdf_ballistic_vests.json); esapi_plate is ABLATIVE_LARGE and esbi_plate is
// ABLATIVE_MEDIUM (cdf_ballistic_armor.json).
// ---------------------------------------------------------------------------

namespace
{

const itype_id itype_esapi_plate( "esapi_plate" );
const itype_id itype_esbi_plate( "esbi_plate" );

// Wears a fresh US ballistic vest on the avatar and returns it (a worn item stays where it is).
auto wear_vest( avatar &you ) -> item &
{
    detached_ptr<item> vest = item::spawn( "ballistic_vest_esapi", calendar::start_of_cataclysm );
    item &ref = *vest;
    REQUIRE( !you.wear_item( std::move( vest ), false ) );
    REQUIRE( you.is_worn( ref ) );
    REQUIRE( ablative::plates( ref ).empty() );
    return ref;
}

// Wears a backpack so that carried items go into its pocket (parent_item() is the backpack), like the
// carried plate of a player who starts with a bag.
void wear_backpack( avatar &you )
{
    REQUIRE( !you.wear_item( item::spawn( "backpack", calendar::start_of_cataclysm ), false ) );
}

// How many items of the given type the avatar has anywhere (carried, wielded, worn, inside worn items).
auto count_of( avatar &you, const itype_id &id ) -> int
{
    int n = 0;
    you.visit_items( [&]( item * it ) -> VisitResponse {
        if( it->typeId() == id )
        {
            n++;
        }
        return VisitResponse::NEXT;
    } );
    return n;
}

// The plate flag that the slot holding `plate` inside `vest` takes, "" when it is not in a slot.
auto slot_flag_of( const item &vest, const item &plate ) -> std::string
{
    const item_pocket *slot = vest.contents.pocket_of( &plate );
    if( slot == nullptr || !slot->get_data().ablative ) {
        return std::string();
    }
    const pocket_data::FlagsSetType &flags = slot->get_data().get_flag_restrictions();
    REQUIRE( flags.size() == 1 );
    return flags.begin()->str();
}

// Runs one insert activity (what the Insert picker assigns) with `vest` as the holster and `what` as
// the only item, to its end. Returns what debugmsg said while it ran. The loop is bounded.
auto run_insert( avatar &you, item &vest, item &what ) -> std::string
{
    drop_locations locs;
    locs.emplace_back( what, 1 );
    you.assign_activity( std::make_unique<player_activity>(
                             std::make_unique<insert_item_activity_actor>( vest, locs ) ) );
    bool ended = false;
    const std::string captured = capture_debugmsg_during( [&]() {
        ended = process_activity_bounded( you, 2000, 100000 );
    } );
    REQUIRE( ended );
    REQUIRE( !you.activity );
    return captured;
}

} // namespace

TEST_CASE( "insert_a_carried_plate_into_a_worn_ballistic_vest", "[inventory][ui][p3]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    item &vest = wear_vest( you );
    wear_backpack( you );

    item &plate = you.i_add( item::spawn( itype_esapi_plate, calendar::start_of_cataclysm ) );
    REQUIRE( plate.parent_item() != nullptr );
    REQUIRE( plate.parent_item() != &vest );
    REQUIRE( count_of( you, itype_esapi_plate ) == 1 );
    const safe_reference<item> original( &plate );

    // the picker lists the plate and does not deny it
    CHECK( game_menus::inv::insert_preset_shows( vest, you, plate ) );
    CHECK( game_menus::inv::insert_preset_denial( vest, you, plate ).empty() );

    // and the activity it assigns puts it into a large plate slot
    const std::string dmsg = run_insert( you, vest, plate );
    CHECK( dmsg.empty() );
    CHECK_FALSE( static_cast<bool>( original ) );
    REQUIRE( ablative::plates( vest ).size() == 1 );
    const item &stored = *ablative::plates( vest ).front();
    CHECK( stored.typeId() == itype_esapi_plate );
    CHECK( stored.parent_item() == &vest );
    CHECK( slot_flag_of( vest, stored ) == "ABLATIVE_LARGE" );
    CHECK( count_of( you, itype_esapi_plate ) == 1 );
    CHECK( you.is_worn( vest ) );
}

TEST_CASE( "insert_a_wielded_plate_into_a_worn_ballistic_vest", "[inventory][ui][p3]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    item &vest = wear_vest( you );

    // no bag: the one plate of the owner's character is in the hands
    REQUIRE( !you.wield( item::spawn( itype_esapi_plate, calendar::start_of_cataclysm ) ) );
    item &plate = you.primary_weapon();
    REQUIRE( plate.typeId() == itype_esapi_plate );
    REQUIRE( you.is_wielding( plate ) );
    const safe_reference<item> original( &plate );

    CHECK( game_menus::inv::insert_preset_denial( vest, you, plate ).empty() );

    const std::string dmsg = run_insert( you, vest, plate );
    CHECK( dmsg.empty() );
    CHECK_FALSE( static_cast<bool>( original ) );
    CHECK_FALSE( you.is_armed() );
    REQUIRE( ablative::plates( vest ).size() == 1 );
    CHECK( slot_flag_of( vest, *ablative::plates( vest ).front() ) == "ABLATIVE_LARGE" );
    CHECK( count_of( you, itype_esapi_plate ) == 1 );
}

TEST_CASE( "insert_plates_into_the_slots_of_a_worn_ballistic_vest", "[inventory][ui][p3]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    item &vest = wear_vest( you );
    wear_backpack( you );

    SECTION( "a medium plate goes into a medium slot" ) {
        item &plate = you.i_add( item::spawn( itype_esbi_plate, calendar::start_of_cataclysm ) );
        CHECK( game_menus::inv::insert_preset_denial( vest, you, plate ).empty() );
        CHECK( run_insert( you, vest, plate ).empty() );
        REQUIRE( ablative::plates( vest ).size() == 1 );
        const item &stored = *ablative::plates( vest ).front();
        CHECK( stored.typeId() == itype_esbi_plate );
        CHECK( slot_flag_of( vest, stored ) == "ABLATIVE_MEDIUM" );
        CHECK( count_of( you, itype_esbi_plate ) == 1 );
    }

    SECTION( "two large plates take both large slots and the third is refused" ) {
        for( int i = 0; i < 2; i++ ) {
            item &plate = you.i_add( item::spawn( itype_esapi_plate, calendar::start_of_cataclysm ) );
            CHECK( game_menus::inv::insert_preset_denial( vest, you, plate ).empty() );
            CHECK( run_insert( you, vest, plate ).empty() );
        }
        REQUIRE( ablative::plates( vest ).size() == 2 );
        for( const item *stored : ablative::plates( vest ) ) {
            CHECK( slot_flag_of( vest, *stored ) == "ABLATIVE_LARGE" );
        }
        // the two plates sit in two different slots
        CHECK( vest.contents.pocket_of( ablative::plates( vest )[0] ) !=
               vest.contents.pocket_of( ablative::plates( vest )[1] ) );

        item &third = you.i_add( item::spawn( itype_esapi_plate, calendar::start_of_cataclysm ) );
        REQUIRE( count_of( you, itype_esapi_plate ) == 3 );
        const safe_reference<item> third_ref( &third );
        const std::string denial = game_menus::inv::insert_preset_denial( vest, you, third );
        CHECK( denial.find( "already contains a plate" ) != std::string::npos );
        CHECK( run_insert( you, vest, third ).empty() );
        CHECK( static_cast<bool>( third_ref ) );
        CHECK( ablative::plates( vest ).size() == 2 );
        CHECK( count_of( you, itype_esapi_plate ) == 3 );
    }
}

TEST_CASE( "insert_refuses_a_knife_for_a_worn_ballistic_vest", "[inventory][ui][p3]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    item &vest = wear_vest( you );
    wear_backpack( you );

    item &knife = you.i_add( item::spawn( "knife_combat", calendar::start_of_cataclysm ) );
    const safe_reference<item> knife_ref( &knife );
    REQUIRE( count_of( you, itype_id( "knife_combat" ) ) == 1 );

    const std::string denial = game_menus::inv::insert_preset_denial( vest, you, knife );
    CHECK_FALSE( denial.empty() );
    CHECK( denial.find( "holster does not accept" ) != std::string::npos );

    // the activity refuses too, and leaves the knife where it was
    run_insert( you, vest, knife );
    CHECK( static_cast<bool>( knife_ref ) );
    CHECK( ablative::plates( vest ).empty() );
    CHECK( count_of( you, itype_id( "knife_combat" ) ) == 1 );
    CHECK( vest.empty_container() );
}
