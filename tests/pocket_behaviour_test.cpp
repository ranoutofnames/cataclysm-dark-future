// Pocket behaviours from the PL-64 ENGINE-CENSUS rows E1-023, E1-030, E1-037, E1-047 and
// E1-057. One test per behaviour, written against DDA's result (DDA pin b4b62254ae2); the two
// behaviours where reading CDF showed a different result are ported in src/item_pocket.cpp and
// the rest stay as regression guards (see the comments on each test).
#include "avatar.h"
#include "cached_options.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"
#include "map/map.h"
#include "map_helpers.h"
#include "pocket_data.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"

namespace
{

auto magazine_pocket( item &mag ) -> item_pocket *
{
    for( item_pocket &pocket : mag.contents.get_pockets() ) {
        if( pocket.is_type( pocket_type::MAGAZINE ) ) {
            return &pocket;
        }
    }
    return nullptr;
}

auto restricted_pocket( item &it ) -> item_pocket *
{
    for( item_pocket &pocket : it.contents.get_pockets() ) {
        if( !pocket.get_data().ammo_restriction.empty() ) {
            return &pocket;
        }
    }
    return nullptr;
}

auto pocket_charges( const item_pocket &pocket ) -> int
{
    int total = 0;
    for( const item *it : pocket.all_items_top() ) {
        total += it->count();
    }
    return total;
}

// test_mode_spilling_action is how handle_liquid chooses a destination in tests
// (handle_liquid.cpp get_liquid_target).
class spill_action_guard
{
    public:
        explicit spill_action_guard( test_mode_spilling_action_t action )
            : saved( test_mode_spilling_action ) {
            test_mode_spilling_action = action;
        }
        ~spill_action_guard() {
            test_mode_spilling_action = saved;
        }
    private:
        test_mode_spilling_action_t saved;
};

} // namespace

// E1-023: DDA item_pocket.cpp:600-603 returns pocket_data::volume_capacity(). CDF used to
// size an ammo pocket from the ammotype's default ammo only. The quiver accepts arrows and
// bolts; arrow_flamming is 500 ml for a stack of 5, so its 20-round pocket is 2 L, not the
// 500 ml the default arrow_wood (250 ml for a stack of 10) gave.
TEST_CASE( "ammo pocket capacity follows the largest accepted ammo", "[pocket][fm][capacity]" )
{
    clear_all_state();
    detached_ptr<item> quiver = item::spawn( itype_id( "quiver" ) );
    item_pocket *pocket = restricted_pocket( *quiver );
    REQUIRE( pocket != nullptr );

    CHECK( pocket->volume_capacity() == pocket->get_data().volume_capacity() );
    // CDF: 20 * arrow_flamming volume / stack_size (data/json/items/ranged/archery.json)
    CHECK( pocket->volume_capacity() == 2_liter );

    // CDF: the per-instance override (body storage enchantment) still wins.
    pocket->set_capacity_override( 3_liter );
    CHECK( pocket->volume_capacity() == 3_liter );
}

// E1-030: DDA item_pocket.cpp:314-366. After merging two magazine stacks DDA keeps the same
// outer item and tries to merge the next one into it. CDF advanced the index after a merge, so
// a run of mergeable stacks stayed split.
TEST_CASE( "magazine restack merges a whole run of stacks", "[pocket][fm][restack]" )
{
    clear_all_state();
    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    mag->contents.clear_items();
    item_pocket *pocket = magazine_pocket( *mag );
    REQUIRE( pocket != nullptr );

    // Insert without the per-insert restack so three separate stacks exist, newest first.
    for( const int charges : { 2, 3, 5 } ) {
        REQUIRE( pocket->insert_item( item::spawn( itype_id( "556" ), calendar::turn, charges ),
                                      false, false ).success() );
    }
    REQUIRE( pocket->size() == 3 );

    pocket->restack();

    // DDA: all three become one stack of 2 + 3 + 5.
    REQUIRE( pocket->size() == 1 );
    CHECK( pocket->front().charges == 10 );
    CHECK( pocket->front().typeId() == itype_id( "556" ) );
}

// E1-047: DDA item_pocket.cpp:1813-1920, ammo branch. An overfull ammo-restricted pocket keeps
// its allowed count and sheds the surplus. DDA's own code at the pin builds `dropped_ammo` and
// then hands the reduced stack to move_to_parent_pocket_recursive (an unused local; the reduced
// stack is also still in the pocket), which would clone rounds. CDF sheds only the surplus,
// which is what the DDA lines name; the test pins that result.
TEST_CASE( "ammo pocket overflow sheds only the surplus", "[pocket][fm][overflow]" )
{
    clear_all_state();
    map &here = get_map();
    const tripoint_bub_ms origin( 60, 60, 0 );
    here.i_clear( origin );

    detached_ptr<item> mag = item::spawn( itype_id( "stanag30" ) );
    mag->contents.clear_items();
    item_pocket *pocket = magazine_pocket( *mag );
    REQUIRE( pocket != nullptr );

    // Two stacks that each fit the pocket volume; together they exceed the ammo restriction.
    pocket->items().push_back( item::spawn( itype_id( "556" ), calendar::turn, 25 ) );
    pocket->items().push_back( item::spawn( itype_id( "556" ), calendar::turn, 25 ) );
    pocket->invalidate_cache();
    REQUIRE( pocket->size() == 2 );
    REQUIRE( pocket_charges( *pocket ) == 50 );

    mag->contents.overflow( origin, nullptr );

    // stanag30 holds 30: twenty leave, thirty stay, nothing is duplicated.
    CHECK( pocket_charges( *pocket ) == 30 );
    int on_ground = 0;
    for( const item *it : here.i_at( origin ) ) {
        if( it->typeId() == itype_id( "556" ) ) {
            on_ground += it->count();
        }
    }
    CHECK( on_ground == 20 );
}

// E1-037: DDA item_pocket.cpp:827-855 passes the avoid item and a persistent liquid_dest_opt
// to handle_liquid, so the container being emptied is not offered as its own target and the
// choice is reused. CDF's liquid_handler::handle_liquid has no avoid/destination parameter, so
// that part cannot be ported from item_pocket.cpp; this test guards the observable contract
// (the liquid leaves the container and lands on the ground) and is marked in the report.
TEST_CASE( "on_pickup pours a spillable pocket out, not into itself", "[pocket][fm][liquid]" )
{
    clear_all_state();
    avatar &you = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms pos = you.bub_pos();
    here.i_clear( pos );

    const spill_action_guard spill( test_mode_spilling_action_t::spill_all );

    detached_ptr<item> can = item::spawn( itype_id( "test_watertight_open_sealed_container_250ml" ) );
    REQUIRE( can->put_in( item::spawn( itype_id( "test_liquid_1ml" ), calendar::turn, 5 ),
                          pocket_type::CONTAINER ).success() );

    item *const avoid = &*can;
    can->handle_liquid_or_spill( you, avoid );

    CHECK( can->empty() );
    int on_ground = 0;
    for( const item *it : here.i_at( pos ) ) {
        if( it->typeId() == itype_id( "test_liquid_1ml" ) ) {
            on_ground += it->charges;
        }
    }
    CHECK( on_ground == 5 );
}

// E1-057: DDA item_pocket.cpp:911-918 seals a pocket only through `_sealed`. CDF also treats a
// pocket of a `sealed_by_type` item (BN's unseals_into factory seal) as sealed. That is a BN-only
// feature and is retained per the owner rule; this test pins both halves of the result.
TEST_CASE( "sealed_by_type keeps a factory-sealed pocket sealed", "[pocket][fm][sealed]" )
{
    clear_all_state();
    detached_ptr<item> owner = item::spawn( itype_id( "test_box" ) );

    SECTION( "sealed_data alone is not sealed until seal()" ) {
        pocket_data data;
        data.max_item_length = 2_km; // CDF: C++-built pockets have no default length
        data.sealed_data = sealable_data();
        item_pocket pocket( &*owner, &data );
        CHECK( pocket.sealable() );
        CHECK_FALSE( pocket.sealed() );
        REQUIRE( pocket.insert_item( item::spawn( itype_id( "test_rock" ), calendar::turn, 1 ) ).success() );
        CHECK( pocket.seal() );
        CHECK( pocket.sealed() );
        pocket.unseal();
        CHECK_FALSE( pocket.sealed() );
    }

    SECTION( "sealed_by_type is sealed without seal() and unseal() does not open it" ) {
        pocket_data data;
        data.max_item_length = 2_km; // CDF: C++-built pockets have no default length
        data.sealed_data = sealable_data();
        data.sealed_by_type = true;
        item_pocket pocket( &*owner, &data );
        CHECK( pocket.sealable() );
        CHECK( pocket.sealed() );
        pocket.unseal();
        CHECK( pocket.sealed() );
    }

    SECTION( "a can_drink from the factory is sealed, its opened type is not" ) {
        detached_ptr<item> can = item::spawn( itype_id( "can_drink" ) );
        const std::vector<item_pocket *> can_pockets = can->get_container_pockets();
        REQUIRE( can_pockets.size() == 1 );
        CHECK( can_pockets.front()->sealed() );
        detached_ptr<item> opened = item::spawn( itype_id( "can_drink_unsealed" ) );
        const std::vector<item_pocket *> opened_pockets = opened->get_container_pockets();
        REQUIRE( opened_pockets.size() == 1 );
        CHECK_FALSE( opened_pockets.front()->sealed() );
    }
}
