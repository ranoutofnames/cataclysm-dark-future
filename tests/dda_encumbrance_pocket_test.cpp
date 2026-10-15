// DDA source: tests/encumbrance_test.cpp (DDA commit b4b62254ae2), P1-transplant J5.
// TEST_CASEs copied (2): off_limb_ablative_encumbrance, additional_pockets_encumbrance
// Translated for CDF (TT1-TT8 of orchestration/plans/P1-transplant/COMMON.md): the clothing is a
// list of detached items put on with `worn.push_back` as the BN encumbrance_test does (the DDA
// `outfit::wear_item` has no counterpart), the encumbrance is read from the character's
// encumbrance cache, and `add_pocket( item )` takes a detached pouch.
#include <string>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "catch/catch.hpp"
#include "character.h"
#include "character_encumbrance.h"
#include "item.h"
#include "item_contents.h"
#include "npc.h"
#include "player.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "state_helpers.h"
#include "type_id.h"

static const itype_id itype_holster( "holster" );

static const itype_id itype_test_ghost_vest( "test_ghost_vest" );

static const itype_id itype_test_load_bearing_vest( "test_load_bearing_vest" );

static const itype_id itype_test_longshirt( "test_longshirt" );

static const itype_id itype_test_plate_skirt( "test_plate_skirt" );

static constexpr int longshirt_e = 3;
static constexpr int load_bearing = 2;
static constexpr int plate = 2;

static void test_encumbrance_on( player &p, std::vector<detached_ptr<item>> &clothing,
                                 const std::string &body_part, int expected_encumbrance )
{
    CAPTURE( body_part );
    p.set_body();
    p.clear_mutations();
    p.worn.clear();
    for( detached_ptr<item> &i : clothing ) {
        p.worn.push_back( std::move( i ) );
    }
    p.reset_encumbrance();
    const encumbrance_data enc = p.get_encumbrance().elems[bodypart_str_id( body_part )];
    CHECK( enc.encumbrance == expected_encumbrance );
}

static void test_encumbrance_items( std::vector<detached_ptr<item>> &clothing,
                                    const std::string &body_part, const int expected_encumbrance )
{
    // Test NPC first because NPC code can accidentally end up using properties
    // of g->u, and such bugs are hidden if we test the other way around.
    SECTION( "testing on npc" ) {
        npc example_npc;
        test_encumbrance_on( example_npc, clothing, body_part, expected_encumbrance );
    }
    SECTION( "testing on player" ) {
        test_encumbrance_on( get_avatar(), clothing, body_part, expected_encumbrance );
    }
}

TEST_CASE( "off_limb_ablative_encumbrance", "[encumbrance]" )
{
    clear_all_state();
    std::vector<detached_ptr<item>> clothing;
    detached_ptr<item> with_plates = item::spawn( itype_test_ghost_vest );
    with_plates->force_insert_item( item::spawn( itype_test_plate_skirt ), pocket_type::CONTAINER );
    clothing.push_back( std::move( with_plates ) );
    test_encumbrance_items( clothing, "leg_l", plate );
}

// make sure ordering still works with pockets
TEST_CASE( "additional_pockets_encumbrance", "[encumbrance]" )
{
    clear_all_state();
    std::vector<detached_ptr<item>> clothing;
    clothing.push_back( item::spawn( itype_test_longshirt ) );
    detached_ptr<item> addition_vest = item::spawn( itype_test_load_bearing_vest );
    addition_vest->contents.add_pocket( item::spawn( itype_holster ) );
    clothing.push_back( std::move( addition_vest ) );

    test_encumbrance_items( clothing, "torso", longshirt_e + load_bearing );
}
