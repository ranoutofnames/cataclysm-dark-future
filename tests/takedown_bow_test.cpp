// TAKEDOWN1: the takedown recurve bow, its folded form and the multi-function quiver.
// DDA: data/json/items/ranged/archery.json:653-745, data/json/items/armor/ammo_pouch.json:585-625
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "flag.h"
#include "item.h"
#include "item_contents.h"
#include "item_factory.h"
#include "item_group.h"
#include "itype.h"
#include "iuse_actor.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "subbodypart.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

const itype_id bow_id( "takedown_recurbow" );
const itype_id folded_id( "takedown_recurbow_folded" );
const itype_id quiver_id( "quiver_takedown_bow" );

// Wield `id`, run its transform use action and return what the wielded item is afterwards.
auto transform_wielded( const itype_id &id ) -> itype_id
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_avatar();
    REQUIRE( !you.wield( item::spawn( id, calendar::turn ) ) );
    item &held = you.primary_weapon();
    REQUIRE( held.typeId() == id );
    const use_function *use = held.type->get_use( "transform" );
    REQUIRE( use != nullptr );
    const iuse_transform *actor = dynamic_cast<const iuse_transform *>( use->get_actor_ptr() );
    REQUIRE( actor != nullptr );
    actor->use( you, held, false, you.bub_pos() );
    return you.primary_weapon().typeId();
}

} // namespace

// K1
TEST_CASE( "takedown recurve bow is a gun that is also worn on the back", "[armor][ranged][takedown]" )
{
    item &bow = *item::spawn_temporary( bow_id, calendar::turn );
    // CDF: the location comes from the TORSO1 sub-locations (data/json/body_parts_sub.json torso_hanging_back)
    const std::vector<sub_bodypart_id> covered = bow.get_covered_sub_body_parts();
    REQUIRE( covered.size() == 1 );
    CHECK( covered[0]->id.str() == "torso_hanging_back" );
    CHECK( bow.get_coverage( bodypart_id( "torso" ) ) == 10 );
    CHECK( bow.has_flag( flag_id( "BELTED" ) ) );
    CHECK( bow.has_flag( flag_id( "SHEATH_BOW" ) ) );
    CHECK( bow.is_gun() );
    CHECK( bow.is_armor() );
    CHECK( bow.ammo_types().count( ammotype( "arrow" ) ) == 1 );
}

// K2
TEST_CASE( "folded takedown bow has no ranged mode and is kept off turrets", "[ranged][takedown]" )
{
    CHECK( flag_id( "NO_TURRET" ).is_valid() );
    item &folded = *item::spawn_temporary( folded_id, calendar::turn );
    CHECK( folded.is_gun() );
    CHECK( folded.has_flag( flag_id( "NO_TURRET" ) ) );
    CHECK_FALSE( folded.has_flag( flag_id( "BELTED" ) ) );
    // DDA: modes [["DEFAULT","disassembled",0,["MELEE"]]]: only the melee mode, no ranged shot
    const auto &modes = folded.type->gun->modes;
    REQUIRE( modes.size() == 1 );
    CHECK( modes.begin()->second.qty() == 0 );
    CHECK( modes.begin()->second.flags().count( "MELEE" ) == 1 );
    // the assembled bow is not turret-blocked
    CHECK_FALSE( item::spawn_temporary( bow_id, calendar::turn )->has_flag( flag_id( "NO_TURRET" ) ) );
}

// K3
TEST_CASE( "takedown bow disassembles and assembles when wielded", "[item][ranged][takedown]" )
{
    SECTION( "bow to folded" ) {
        CHECK( transform_wielded( bow_id ) == folded_id );
    }
    SECTION( "folded to bow" ) {
        CHECK( transform_wielded( folded_id ) == bow_id );
    }
    SECTION( "the use needs the bow wielded" ) {
        clear_all_state();
        avatar &you = get_avatar();
        clear_avatar();
        item &carried = you.i_add( item::spawn( bow_id, calendar::turn ) );
        const iuse_transform *actor = dynamic_cast<const iuse_transform *>(
                                          carried.type->get_use( "transform" )->get_actor_ptr() );
        REQUIRE( actor != nullptr );
        actor->use( you, carried, false, you.bub_pos() );
        CHECK( carried.typeId() == bow_id );
    }
}

// K4
TEST_CASE( "multi-function quiver pockets", "[item][pocket][takedown]" )
{
    // DDA: ammo_pouch.json quiver_takedown_bow
    item &quiver = *item::spawn_temporary( quiver_id, calendar::turn );
    item &folded = *item::spawn_temporary( folded_id, calendar::turn );
    item &bow = *item::spawn_temporary( bow_id, calendar::turn );
    SECTION( "16 arrows fit and the 17th does not" ) {
        // CDF: arrow_cf is CDF's plain arrow; the pocket is ammo-typed arrow 16 as in DDA
        item &arrows = *item::spawn_temporary( itype_id( "arrow_cf" ), calendar::turn );
        arrows.charges = 16;
        CHECK( quiver.can_contain( arrows ).success() );
        arrows.charges = 17;
        CHECK_FALSE( quiver.can_contain( arrows ).success() );
    }
    SECTION( "the holster pocket takes the folded bow and refuses the assembled one" ) {
        CHECK( quiver.can_holster( folded ) );
        CHECK_FALSE( quiver.can_holster( bow ) );
        // the assembled bow also does not fit the general pocket (35 cm side limit)
        CHECK_FALSE( quiver.can_contain( bow ).success() );
    }
    SECTION( "the holster pocket refuses another item" ) {
        CHECK_FALSE( quiver.can_holster( *item::spawn_temporary( itype_id( "longbow" ), calendar::turn ) ) );
        // negative fixture: an item that fits the size limits of the pocket is still refused, only
        // the folded bow is let in (// DDA: item_restriction takedown_recurbow_folded)
        item &hatchet = *item::spawn_temporary( itype_id( "hatchet" ), calendar::turn );
        REQUIRE( hatchet.volume() >= 500_ml );
        REQUIRE( hatchet.volume() <= 4_liter );
        REQUIRE( hatchet.length() <= 75_cm );
        CHECK_FALSE( quiver.can_holster( hatchet ) );
    }
}

// K5
TEST_CASE( "multi-function quiver and a rucksack are worn together on the back", "[armor][takedown]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    clear_avatar();
    item &quiver = *item::spawn_temporary( quiver_id, calendar::turn );
    const std::vector<sub_bodypart_id> covered = quiver.get_covered_sub_body_parts();
    REQUIRE( covered.size() == 1 );
    CHECK( covered[0]->id.str() == "torso_hanging_back" );
    CHECK( quiver.get_coverage( bodypart_id( "torso" ) ) == 30 );
    you.wear_item( item::spawn( itype_id( "rucksack" ), calendar::turn ), false );
    const ret_val<bool> can = you.can_wear( quiver );
    CAPTURE( can.str() );
    CHECK( can.success() );
    you.wear_item( item::spawn( quiver_id, calendar::turn ), false );
    CHECK( you.amount_of( quiver_id ) == 1 );
    CHECK( you.amount_of( itype_id( "rucksack" ) ) == 1 );
}

// K6
TEST_CASE( "takedown bow spawn entries are in the CDF groups that play DDA's roles", "[item_group][takedown]" )
{
    struct entry {
        const char *group;
        bool bow;
        bool quiver;
    };
    // DDA: itemgroups activities_hobbies:213, Clothing_Gear/bags:17,140, clothing:1974,
    // locations_commercial:492, mansion:783-784, main:120-121, SUS/lodge:13-14
    // CDF: the group ids are CDF's own counterparts (bags and bags_unisex live in clothing.json)
    const std::vector<entry> expected = {
        { "allsporting", false, true }, { "bags", false, true }, { "bags_unisex", false, true },
        { "survivorzed_extra", false, true }, { "pawn", false, true }, { "sports_bulk", true, true },
        { "archery", true, true }, { "SUS_hunting_archery", true, true }
    };
    for( const entry &e : expected ) {
        CAPTURE( e.group );
        const item_group_id gid( e.group );
        REQUIRE( item_group::group_is_defined( gid ) );
        CHECK( item_group::group_contains_item( gid, bow_id ) == e.bow );
        CHECK( item_group::group_contains_item( gid, quiver_id ) == e.quiver );
        // every item a group can produce is a defined type
        for( const itype *t : item_group::every_possible_item_from( gid ) ) {
            CHECK( t->get_id().is_valid() );
        }
    }
}
