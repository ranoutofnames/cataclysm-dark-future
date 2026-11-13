#include "activity_actor_definitions.h"
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "debug.h"
#include "item.h"
#include "item_handling_util.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "type_id.h"

#include <climits>
#include <memory>
#include <string>

// CDF: P3-9-4 step 1 (regression R1). "Insert item" into a gun.
//
// Premise of PLAN.md row 18 (checked here, not assumed): the insert path refuses a gun because a
// gun has no CONTAINER pocket. What the code really does, in CDF and in DDA alike
// (CDF src/activity_actor.cpp:2167 try_insert, DDA src/activity_actor.cpp:8084 try_insert, same
// opening):
//  - the first gate is item::can_contain_directly( it ), which passes ignore_non_container_pocket =
//    false (CDF src/item_container.cpp:382, DDA src/item_container.cpp:517), so the MAGAZINE_WELL of
//    a glock_19 is looked at and a glockmag IS accepted by that gate;
//  - the refusal comes one step later, from item::put_in( ..., pocket_type::CONTAINER ): a gun has
//    no CONTAINER pocket, find_pocket_for gives "pocket with type (CONTAINER) not found" and put_in
//    raises its (non-quiet) debugmsg (CDF src/item.cpp:1497, DDA src/item_container.cpp:267);
//  - loose ammo is refused already by the gate, so it raises no debugmsg.
// So the premise "the insert path never offers a gun" is false in both trees; what is true is: the gate
// accepts a magazine for a gun with a well, and the put into the CONTAINER pocket refuses it.
// DDA does that put in try_insert on a COPY and keeps the original when it fails. CDF used to put
// the DETACHED original after printing "You put your ...", which destroyed the magazine (found by
// R1a); NINEFOUR1b ported DDA's order (put a copy in try_insert, remove the original in do_turn only
// on success). R1a, the refusal-reports-once case and the box/charges cases guard that.
// Messages are stubs in the test build (tests/fake_messages.cpp), so the refusal is read from the
// state (the magazine is still carried, the well is still empty) and from the captured debugmsg.

namespace {

const itype_id itype_glock_19( "glock_19" );
const itype_id itype_glockmag( "glockmag" );
const itype_id itype_9mm( "9mm" );

// CDF: glockmag takes 9mm (data/json/items/magazine/9mm.json: ammo_type 9mm, 250 ml, MAG_COMPACT) and is
// listed in the item_restriction of the glock_19 MAGAZINE_WELL (data/json/items/gun/9mm.json:114-130).
auto loaded_glockmag() -> detached_ptr<item> {
    detached_ptr<item> mag = item::spawn( itype_glockmag, calendar::turn );
    mag->put_in( item::spawn( itype_9mm, calendar::turn, 15 ) );
    REQUIRE( mag->ammo_remaining() == 15 );
    return mag;
}

auto empty_glock() -> detached_ptr<item> {
    detached_ptr<item> gun = item::spawn( itype_glock_19, calendar::turn, 0 );
    REQUIRE( gun->is_gun() );
    REQUIRE( gun->magazine_current() == nullptr );
    REQUIRE( gun->ammo_remaining() == 0 );
    return gun;
}

// Runs one insert activity with `gun` as the holster and `what` as the only item, to its end.
// Returns what debugmsg said while it ran. The activity is bounded: a failing run must not spin.
auto run_insert_all( avatar &dummy, item &gun, drop_locations &locs ) -> std::string {
    dummy.assign_activity( std::make_unique<player_activity>(
                               std::make_unique<insert_item_activity_actor>( gun, locs ) ) );
    std::string captured;
    bool ended = false;
    captured = capture_debugmsg_during( [&]() {
        ended = process_activity_bounded( dummy, 2000, 100000 );
    } );
    REQUIRE( ended );
    REQUIRE( !dummy.activity );
    return captured;
}

auto run_insert( avatar &dummy, item &gun, item &what, int count ) -> std::string {
    drop_locations locs;
    locs.emplace_back( what, count );
    return run_insert_all( dummy, gun, locs );
}

// how many times `needle` occurs in `text`
auto count_occurrences( const std::string &text, const std::string &needle ) -> int {
    int n = 0;
    for( size_t pos = text.find( needle ); pos != std::string::npos;
         pos = text.find( needle, pos + needle.size() ) ) {
        n++;
    }
    return n;
}

// Total rounds loaded in every glockmag under `where` (charges_of does not look inside magazines).
auto glockmag_rounds( Character &where ) -> int {
    int rounds = 0;
    where.visit_items( [&]( item * it ) -> VisitResponse {
        if( it->typeId() == itype_glockmag ) {
            rounds += it->ammo_remaining();
        }
        return VisitResponse::NEXT;
    } );
    return rounds;
}

// Wields `gun`: a wielded gun has no parent pocket, so try_insert's parent-space check is out of the way
// (a gun carried in a backpack is refused earlier, by that check, which is not what R1 is about).
auto wield_gun( avatar &dummy ) -> item & {
    REQUIRE( !dummy.wield( empty_glock() ) );
    REQUIRE( dummy.is_armed() );
    return dummy.primary_weapon();
}

} // namespace

// Control: the actor does move a magazine into a real container, so the refusals below are the
// gun's and not a dead actor (no state change would otherwise look the same).
TEST_CASE( "insert_activity_moves_a_magazine_into_a_box", "[p3][insert]" )
{
    clear_all_state();
    avatar &dummy = get_avatar();
    // Make sure the player doesn't drop anything :P
    dummy.wear_item( item::spawn( "backpack", calendar::start_of_cataclysm ) );

    // the magazine goes in first: i_add would put it into a wielded box or gun
    item &mag = dummy.i_add( loaded_glockmag() );
    REQUIRE( !dummy.wield( item::spawn( "box_small", calendar::turn ) ) );
    item &box = dummy.primary_weapon();
    REQUIRE( !box.has_item_with_id( itype_glockmag ) );

    const std::string dmsg = run_insert( dummy, box, mag, 1 );

    CHECK( dmsg.empty() );
    CHECK( box.has_item_with_id( itype_glockmag ) );
    CHECK( dummy.inv_position_by_type( itype_glockmag ) == INT_MIN );
}

// NINEFOUR1b, regression guard for the fix: a successful insert behaves as before. Two non-charge
// items in one activity both go in (the original is removed after the copy went in, once each) and
// keep their contents.
TEST_CASE( "insert_activity_moves_two_magazines_into_a_box", "[p3][insert]" )
{
    clear_all_state();
    avatar &dummy = get_avatar();
    // Make sure the player doesn't drop anything :P
    dummy.wear_item( item::spawn( "backpack", calendar::start_of_cataclysm ) );

    item &mag1 = dummy.i_add( loaded_glockmag() );
    item &mag2 = dummy.i_add( loaded_glockmag() );
    REQUIRE( dummy.amount_of( itype_glockmag ) == 2 );
    REQUIRE( !dummy.wield( item::spawn( "box_small", calendar::turn ) ) );
    item &box = dummy.primary_weapon();
    REQUIRE( !box.has_item_with_id( itype_glockmag ) );

    drop_locations locs;
    locs.emplace_back( mag1, 1 );
    locs.emplace_back( mag2, 1 );
    const std::string dmsg = run_insert_all( dummy, box, locs );

    CHECK( dmsg.empty() );
    CHECK( dummy.inv_position_by_type( itype_glockmag ) == INT_MIN );
    // the two magazines are in the box now, nothing was lost or duplicated, 15 rounds each
    CHECK( dummy.amount_of( itype_glockmag ) == 2 );
    CHECK( box.amount_of( itype_glockmag ) == 2 );
    CHECK( glockmag_rounds( dummy ) == 30 );
}

// NINEFOUR1b, regression guard: the charges path (loose ammo) is not touched by the fix; a partial
// count moves only that many charges and the rest stays carried, a full count moves the whole stack.
TEST_CASE( "insert_activity_still_splits_charges_into_a_box", "[p3][insert]" )
{
    clear_all_state();
    avatar &dummy = get_avatar();
    // Make sure the player doesn't drop anything :P
    dummy.wear_item( item::spawn( "backpack", calendar::start_of_cataclysm ) );

    item &ammo = dummy.i_add( item::spawn( itype_9mm, calendar::turn, 30 ) );
    REQUIRE( !dummy.wield( item::spawn( "box_small", calendar::turn ) ) );
    item &box = dummy.primary_weapon();
    REQUIRE( ammo.count_by_charges() );
    REQUIRE( ammo.charges == 30 );
    REQUIRE( box.charges_of( itype_9mm ) == 0 );

    SECTION( "partial count" ) {
        const std::string dmsg = run_insert( dummy, box, ammo, 20 );
        CHECK( dmsg.empty() );
        CHECK( box.charges_of( itype_9mm ) == 20 );
        const int pos = dummy.inv_position_by_type( itype_9mm );
        REQUIRE( pos != INT_MIN );
        CHECK( dummy.inv_find_item( pos ).charges == 10 );
    }

    SECTION( "full count" ) {
        const std::string dmsg = run_insert( dummy, box, ammo, 30 );
        CHECK( dmsg.empty() );
        CHECK( box.charges_of( itype_9mm ) == 30 );
        CHECK( dummy.inv_position_by_type( itype_9mm ) == INT_MIN );
    }
}

// R1a. The magazine does not end up in the gun. CDF: the DDA outcome, not the draft's premise: the
// gate says yes, put_in( CONTAINER ) says no and raises its debugmsg, and the magazine stays carried.
TEST_CASE( "insert_activity_refuses_a_magazine_for_a_gun", "[p3][insert]" )
{
    clear_all_state();
    avatar &dummy = get_avatar();
    // Make sure the player doesn't drop anything :P
    dummy.wear_item( item::spawn( "backpack", calendar::start_of_cataclysm ) );

    // the magazine goes in first: i_add would put it into the wielded gun's well
    item &mag = dummy.i_add( loaded_glockmag() );
    item &gun = wield_gun( dummy );
    REQUIRE( dummy.inv_position_by_type( itype_glockmag ) != INT_MIN );

    const std::string dmsg = run_insert( dummy, gun, mag, 1 );

    // DDA: put_in( ..., CONTAINER ) on a gun raises this debugmsg and try_insert returns its failure
    CHECK( dmsg.find( "tried to put an item (glockmag)" ) != std::string::npos );
    CHECK( dmsg.find( "that cannot contain it" ) != std::string::npos );
    CHECK( dmsg.find( "pocket with type (CONTAINER) not found" ) != std::string::npos );

    // the magazine is still carried, still loaded, and the gun has no magazine in its well
    CHECK( dummy.primary_weapon().magazine_current() == nullptr );
    CHECK( dummy.primary_weapon().ammo_remaining() == 0 );
    CHECK( !dummy.primary_weapon().has_item_with_id( itype_glockmag ) );
    // DDA try_insert fails before it removes anything, so the original item is untouched
    const int mag_pos = dummy.inv_position_by_type( itype_glockmag );
    CHECK( mag_pos != INT_MIN );
    if( mag_pos != INT_MIN ) {
        CHECK( dummy.inv_find_item( mag_pos ).ammo_remaining() == 15 );
    }
}

// NINEFOUR1b (b): the refused insert loses nothing and reports once. Two magazines are queued: the
// first one fails, the activity stops there (DDA do_turn/finish stop on the first failure), both are
// still carried and loaded, the well is empty, and the only debugmsg is put_in's own (one, not the
// second "ACT_INSERT_ITEM could not put" of the old detach-first code).
TEST_CASE( "insert_activity_refused_for_a_gun_loses_no_item_and_reports_once", "[p3][insert]" )
{
    clear_all_state();
    avatar &dummy = get_avatar();
    // Make sure the player doesn't drop anything :P
    dummy.wear_item( item::spawn( "backpack", calendar::start_of_cataclysm ) );

    item &mag1 = dummy.i_add( loaded_glockmag() );
    item &mag2 = dummy.i_add( loaded_glockmag() );
    item &gun = wield_gun( dummy );
    REQUIRE( dummy.amount_of( itype_glockmag ) == 2 );
    REQUIRE( glockmag_rounds( dummy ) == 30 );

    drop_locations locs;
    locs.emplace_back( mag1, 1 );
    locs.emplace_back( mag2, 1 );
    const std::string dmsg = run_insert_all( dummy, gun, locs );

    CHECK( count_occurrences( dmsg, "tried to put an item (glockmag)" ) == 1 );
    CHECK( dmsg.find( "ACT_INSERT_ITEM could not put" ) == std::string::npos );
    // item count in the inventory is unchanged: 2 magazines, 30 rounds, none inside the gun
    CHECK( dummy.amount_of( itype_glockmag ) == 2 );
    CHECK( glockmag_rounds( dummy ) == 30 );
    CHECK( !dummy.primary_weapon().has_item_with_id( itype_glockmag ) );
    CHECK( dummy.primary_weapon().magazine_current() == nullptr );
}

// R1b. The gate and the pocket step are different questions. can_contain_directly says yes for a
// magazine into a gun with a well (so R1a does not pass because the gate refused), put_in into a
// CONTAINER pocket says no, and a gun with an integral magazine (no well) is refused by the gate.
TEST_CASE( "can_contain_directly_for_a_gun_pocket_type", "[p3][insert]" )
{
    clear_all_state();
    detached_ptr<item> gun = empty_glock();
    detached_ptr<item> mag = loaded_glockmag();

    // the gate (DDA try_insert opens with it)
    const ret_val<void> gate = gun->can_contain_directly( *mag );
    INFO( "gate said: " << gate.str() );
    CHECK( gate.success() );

    // the step that refuses; quiet: the debugmsg is R1a's business
    const ret_val<void> pocket_step = gun->put_in( *mag, pocket_type::CONTAINER, true, nullptr, true );
    INFO( "put_in said: " << pocket_step.str() );
    CHECK( !pocket_step.success() );
    CHECK( pocket_step.str().find( "pocket with type (CONTAINER) not found" ) != std::string::npos );
    CHECK( gun->magazine_current() == nullptr );

    // a revolver has an integral magazine and no MAGAZINE_WELL: the gate itself refuses
    // CDF: sw_619 (.38 revolver, integral magazine) as in tests/cdf_item_reload_p3_test.cpp
    detached_ptr<item> revolver = item::spawn( "sw_619", calendar::turn, 0 );
    REQUIRE( revolver->magazine_integral() );
    CHECK( !revolver->can_contain_directly( *mag ).success() );
}

// R1c. Loose ammo is refused by the gate (a well takes magazines only), so no put_in and no debugmsg.
TEST_CASE( "insert_activity_refuses_ammo_for_a_gun_without_a_container_pocket", "[p3][insert]" )
{
    clear_all_state();
    avatar &dummy = get_avatar();
    // Make sure the player doesn't drop anything :P
    dummy.wear_item( item::spawn( "backpack", calendar::start_of_cataclysm ) );

    item &ammo = dummy.i_add( item::spawn( itype_9mm, calendar::turn, 30 ) );
    item &gun = wield_gun( dummy );
    REQUIRE( ammo.count_by_charges() );
    REQUIRE( ammo.charges == 30 );
    CHECK( !gun.can_contain_partial_directly( ammo ).success() );

    const std::string dmsg = run_insert( dummy, gun, ammo, 30 );

    // refused at the gate: nothing was put anywhere, so put_in raised nothing
    CHECK( dmsg.empty() );

    const int ammo_pos = dummy.inv_position_by_type( itype_9mm );
    REQUIRE( ammo_pos != INT_MIN );
    CHECK( dummy.inv_find_item( ammo_pos ).charges == 30 );
    CHECK( dummy.primary_weapon().ammo_remaining() == 0 );
    CHECK( dummy.primary_weapon().magazine_current() == nullptr );
}
