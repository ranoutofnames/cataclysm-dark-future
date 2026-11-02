// CDF: CONTAINER1. A garment whose container pockets are all carry (worn storage) is not
// item::is_container(), but items spawned "inside" it, nested inserts and spills must still see its
// carry pockets (DDA is_container() is any CONTAINER pocket).
#include <string>
#include <utility>
#include <vector>

#include "catch/catch.hpp"
#include "calendar.h"
#include "coordinates.h"
#include "item.h"
#include "item_group.h"
#include "map/map.h"
#include "map_helpers.h"
#include "profession.h"
#include "state_helpers.h"
#include "type_id.h"

static const itype_id itype_test_backpack_carry( "test_backpack_carry" );

static int sum_counts( const item &it )
{
    int n = 0;
    for( const item *c : it.all_items_top() ) {
        n += c->count();
    }
    return n;
}

TEST_CASE( "in_container_wraps_in_a_carry_only_garment", "[container1][item_spawn]" )
{
    clear_all_state();
    detached_ptr<item> apple = item::spawn( itype_id( "apple" ), calendar::turn );
    detached_ptr<item> res = item::in_container( itype_test_backpack_carry, std::move( apple ) );
    REQUIRE( res->typeId() == itype_test_backpack_carry );
    CHECK( sum_counts( *res ) == 1 );
}

// CDF: a profession entry with the string form of "container-item" is loaded as the entry modifier's
// container (item_factory.cpp load_sub_ref) and spawned by Item_modifier::modify, not by
// item::in_container; what matters there is the capacity of the carry garment (modifier charges).
// The real bandolier_shotgun has a plain (non-carry) container pocket, so it was never affected.
TEST_CASE( "profession_container_item_keeps_the_carry_garment", "[container1][item_spawn]" )
{
    clear_all_state();
    const profession &prof = string_id<profession>( "test_container1_carry_prof" ).obj();
    std::vector<detached_ptr<item>> spawned = prof.items( true, {} );
    int garments = 0;
    int charges = 0;
    for( const detached_ptr<item> &it : spawned ) {
        if( it->typeId() == itype_test_backpack_carry ) {
            garments++;
            charges += sum_counts( *it );
        }
        // CDF: no loose item beside the garment
        CHECK( it->typeId() != itype_id( "test_item_with_charges" ) );
    }
    CHECK( garments == 1 );
    CHECK( charges == 7 );
}

TEST_CASE( "profession_breacher_shape_with_a_plain_pocket_bandolier", "[container1][item_spawn]" )
{
    clear_all_state();
    // CDF: the real bandolier_shotgun has a plain pocket (is_container()), so the in_container guard never
    // affected it; the shells went in the bandolier but, without the modifier capacity fallback, as 1 charge
    const profession &prof = string_id<profession>( "test_container1_breacher" ).obj();
    std::vector<detached_ptr<item>> spawned = prof.items( true, {} );
    int bandoliers = 0;
    int shells = 0;
    for( const detached_ptr<item> &it : spawned ) {
        if( it->typeId() == itype_id( "bandolier_shotgun" ) ) {
            bandoliers++;
            shells += sum_counts( *it );
        }
        CHECK( it->typeId() != itype_id( "shot_00" ) );
    }
    CHECK( bandoliers == 1 );
    CHECK( shells == 12 );
}

TEST_CASE( "spill_contents_of_a_carry_only_garment", "[container1][item_spawn]" )
{
    clear_all_state();
    clear_map();
    map &here = get_map();
    const tripoint_bub_ms pos( 60, 60, 0 );
    detached_ptr<item> bag = item::spawn( itype_test_backpack_carry, calendar::turn );
    item &bag_ref = *bag;
    REQUIRE( bag_ref.put_in( item::spawn( itype_id( "apple" ), calendar::turn ), pocket_type::CONTAINER ).success() );
    REQUIRE( bag_ref.put_in( item::spawn( itype_id( "test_rock" ), calendar::turn ), pocket_type::CONTAINER ).success() );
    REQUIRE( !bag_ref.is_container_empty() );
    CHECK( bag_ref.spill_contents( pos ) );
    CHECK( bag_ref.is_container_empty() );
    CHECK( here.i_at( pos ).size() == 2 );
}

TEST_CASE( "modifier_container_with_charges_and_a_carry_garment", "[container1][item_spawn]" )
{
    clear_all_state();
    std::vector<detached_ptr<item>> res = item_group::items_from( item_group_id( "test_container1_modifier_carry" ),
                                          calendar::turn );
    REQUIRE( res.size() == 1 );
    REQUIRE( res[0]->typeId() == itype_test_backpack_carry );
    // CDF: test_item_with_charges is 1 ml per 4 charges and the pocket holds 15 L, so 60000 of the
    // 100000 asked for
    CHECK( sum_counts( *res[0] ) == 60000 );
}

TEST_CASE( "is_container_pin", "[container1][item_spawn]" )
{
    clear_all_state();
    detached_ptr<item> bag = item::spawn( itype_test_backpack_carry, calendar::turn );
    CHECK( !bag->is_container() );
    CHECK( item::spawn( itype_id( "holster" ), calendar::turn )->is_container() );
    // CDF: police_belt (holster pockets only) is not is_container() at this base, so it is not pinned
    const units::volume cap_before = bag->get_total_capacity();
    const std::string empty_cat = bag->get_category_id();
    REQUIRE( bag->put_in( item::spawn( itype_id( "apple" ), calendar::turn ), pocket_type::CONTAINER ).success() );
    CHECK( bag->get_category_id() == empty_cat );
    CHECK( bag->get_category_id() != "food" );
    CHECK( !bag->is_container() );
    // CDF: the contents of a worn carry pocket do not add capacity (double-count guard)
    CHECK( bag->get_total_capacity() == cap_before );
}

TEST_CASE( "nested_insert_into_a_carry_garment_in_a_bag", "[container1][item_spawn]" )
{
    clear_all_state();
    detached_ptr<item> bag = item::spawn( itype_test_backpack_carry, calendar::turn );
    detached_ptr<item> apple = item::spawn( itype_id( "apple" ), calendar::turn );
    CHECK( bag->can_contain( *apple, /*nested=*/true, /*ignore_rigidity=*/true ).success() );
}

TEST_CASE( "entry_level_container_item_uses_a_garment", "[container1][item_spawn]" )
{
    clear_all_state();
    // CDF: the object form of the entry-level container-item ("container-item": { "item": ... }) wraps
    // the entry with item::in_container in Single_item_creator::create_single (item_from reaches it
    // directly; items_from/professions use the string form via Item_modifier and put_into_container)
    detached_ptr<item> res = item_group::item_from( item_group_id( "test_container1_entry_carry" ),
                             calendar::turn );
    REQUIRE( res );
    CAPTURE( res->typeId().str() );
    CHECK( res->typeId() == itype_test_backpack_carry );
    CHECK( sum_counts( *res ) == 9 );
}
