// CDF CAP1: carried_capacity() is the carry-pocket sum, the pair of carried_volume().
// volume_capacity() stays the DDA sum of every non-forbidden CONTAINER pocket.
#include <cstdlib>
#include <string>
#include <utility>

#include "avatar.h"
#include "bionics.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "inventory.h"
#include "item.h"
#include "morale_types.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"

namespace
{

// CDF: no avatar state from earlier tests; DEBUG_STORAGE (unlimited) is off unless a case sets it
avatar &fresh_avatar()
{
    clear_all_state();
    avatar &u = get_avatar();
    clear_character( u, false );
    u.unset_mutation( trait_id( "DEBUG_STORAGE" ) );
    u.unset_mutation( trait_id( "PACKMULE" ) );
    u.unset_mutation( trait_id( "HOARDER" ) );
    u.clear_morale();
    return u;
}

void wear( avatar &u, const char *id )
{
    detached_ptr<item> it = item::spawn( itype_id( id ), calendar::turn );
    // CDF: wear_item returns the item back on failure and an empty pointer on success
    REQUIRE( !u.wear_item( std::move( it ), false ) );
}

void add_rocks( avatar &u, int n )
{
    // CDF: TEST_DATA items.json: test_rock is 250 ml
    for( int i = 0; i < n; i++ ) {
        u.i_add( item::spawn( itype_id( "test_rock" ), calendar::turn ) );
    }
}

// CDF: storage.json:1143-1180: survivor_hiking_backpack has one 60 L CONTAINER (carry) pocket and
// two 2500 ml sheath holster pockets, so the carry sum is 60000 ml and the DDA sum 65000 ml
constexpr int pack_carry_ml = 60000;
constexpr int pack_dda_ml = 65000;

} // namespace

TEST_CASE( "cap1_backpack_splits_carried_and_dda_capacity", "[cap1][pocket]" )
{
    avatar &u = fresh_avatar();
    const units::volume cap0 = u.carried_capacity();
    const units::volume vcap0 = u.volume_capacity();
    const units::volume free0 = u.free_carry_volume();

    wear( u, "survivor_hiking_backpack" );
    CHECK( u.carried_capacity() == cap0 + units::from_milliliter( pack_carry_ml ) );
    // DDA: item_pocket.cpp:2548 ok_default_containers counts holster pockets too
    CHECK( u.volume_capacity() == vcap0 + units::from_milliliter( pack_dda_ml ) );
    CHECK( u.free_carry_volume() == free0 + units::from_milliliter( pack_carry_ml ) );

    clear_character( u, false );
    u.unset_mutation( trait_id( "DEBUG_STORAGE" ) );
    CHECK( u.carried_capacity() == cap0 );
    CHECK( u.volume_capacity() == vcap0 );
    CHECK( u.free_carry_volume() == free0 );
}

TEST_CASE( "cap1_plate_pockets_add_only_to_the_dda_total", "[cap1][pocket]" )
{
    avatar &u = fresh_avatar();
    const units::volume cap0 = u.carried_capacity();
    const units::volume vcap0 = u.volume_capacity();

    wear( u, "ballistic_vest_esapi" );
    CHECK( u.carried_capacity() == cap0 );
    // CDF: cdf_ballistic_vests.json:52-107: ablative CONTAINER pockets 2000+2000+800+800 ml
    // DDA: ablative pockets are CONTAINER pockets, ballistic_armor.json:36-45
    CHECK( u.volume_capacity() == vcap0 + 5600_ml );
}

TEST_CASE( "cap1_full_carry_pocket_is_a_full_bar", "[cap1][pocket]" )
{
    avatar &u = fresh_avatar();
    wear( u, "survivor_hiking_backpack" );
    // 240 * 250 ml fills the 60 L pocket
    add_rocks( u, pack_carry_ml / 250 );
    CHECK( u.free_carry_volume() == 0_ml );
    CHECK( u.carried_volume() == u.carried_capacity() );
    CHECK( u.carried_capacity() - u.carried_volume() == 0_ml );
    // the holster room is the 5 L that only the DDA total shows
    CHECK( u.volume_capacity() - u.carried_volume() == 5000_ml );
}

TEST_CASE( "cap1_packmule_scales_worn_carry_pockets_only", "[cap1][pocket]" )
{
    avatar &u = fresh_avatar();
    wear( u, "survivor_hiking_backpack" );
    u.set_mutation( trait_id( "PACKMULE" ) );
    // CDF: mutations.json PACKMULE packmule_modifier 1.4: 60000 * 1.4 and 65000 * 1.4
    CHECK( std::abs( u.carried_capacity().value() - 84000 ) <= 2 );
    CHECK( std::abs( u.volume_capacity().value() - 91000 ) <= 2 );
    u.unset_mutation( trait_id( "PACKMULE" ) );
    CHECK( u.carried_capacity() == 60000_ml );
    CHECK( u.volume_capacity() == 65000_ml );
}

TEST_CASE( "cap1_wielded_bag_counts_unscaled", "[cap1][pocket]" )
{
    avatar &u = fresh_avatar();
    // CDF: storage.json:639-666 jerrypack: one 10 L CONTAINER pocket (carry), torso
    REQUIRE( !u.wield( item::spawn( itype_id( "jerrypack" ), calendar::turn ) ) );
    CHECK( u.carried_capacity() == 10000_ml );
    CHECK( u.volume_capacity() == 10000_ml );

    u.set_mutation( trait_id( "PACKMULE" ) );
    CHECK( u.carried_capacity() == 10000_ml );
    CHECK( u.volume_capacity() == 10000_ml );

    // worn part scaled, wielded part not
    wear( u, "survivor_hiking_backpack" );
    CHECK( std::abs( u.carried_capacity().value() - ( 84000 + 10000 ) ) <= 2 );
    CHECK( std::abs( u.volume_capacity().value() - ( 91000 + 10000 ) ) <= 2 );
}

TEST_CASE( "cap1_debug_storage_and_body_storage", "[cap1][pocket]" )
{
    avatar &u = fresh_avatar();
    u.set_mutation( trait_id( "DEBUG_STORAGE" ) );
    CHECK( u.carried_capacity() == units::volume_max );
    CHECK( u.volume_capacity() == units::volume_max );
    u.unset_mutation( trait_id( "DEBUG_STORAGE" ) );

    // CDF: character_pocket_api_test.cpp:36-50: bio_storage gives a 2000 ml body storage
    u.add_bionic( bionic_id( "bio_storage" ) );
    u.recalculate_enchantment_cache();
    u.update_body_storage();
    REQUIRE( u.get_body_storage() != nullptr );
    CHECK( u.carried_capacity() == 2000_ml );
    CHECK( u.volume_capacity() == 2000_ml );
}

TEST_CASE( "cap1_drop_selector_base_is_the_carried_total", "[cap1][pocket]" )
{
    avatar &u = fresh_avatar();
    wear( u, "jerrypack" );
    const units::volume only_second = u.carried_capacity();
    REQUIRE( only_second == 10000_ml );
    wear( u, "survivor_hiking_backpack" );
    item *pack = nullptr;
    for( item * const &w : u.worn ) {
        if( w->typeId() == itype_id( "survivor_hiking_backpack" ) ) {
            pack = w;
        }
    }
    REQUIRE( pack != nullptr );
    excluded_stacks without;
    without[pack] = 1;
    CHECK( u.volume_capacity_reduced_by( 0_ml, without ) == only_second );

    u.set_mutation( trait_id( "PACKMULE" ) );
    CHECK( std::abs( u.volume_capacity_reduced_by( 0_ml, without ).value() - 14000 ) <= 2 );
}

TEST_CASE( "cap1_hoarder_penalty_uses_the_carry_room", "[cap1][pocket]" )
{
    avatar &u = fresh_avatar();
    u.set_mutation( trait_id( "HOARDER" ) );
    // CDF: the survivor pack stands in for the small bag (no small bag has a plain carry and a
    // holster pocket); its 5 L of holster room must not count as room to fill
    wear( u, "survivor_hiking_backpack" );
    // leave 1000 ml in the 60 L carry pocket: 236 * 250 ml
    add_rocks( u, ( pack_carry_ml - 1000 ) / 250 );
    REQUIRE( u.carried_volume() == 59000_ml );
    u.clear_morale();
    u.apply_persistent_morale();
    // 1000 ml / 125 ml
    CHECK( u.get_morale( MORALE_PERM_HOARDER ) == -8 );

    add_rocks( u, 4 );
    REQUIRE( u.free_carry_volume() == 0_ml );
    u.clear_morale();
    u.apply_persistent_morale();
    CHECK_FALSE( u.has_morale( MORALE_PERM_HOARDER ) );
}
