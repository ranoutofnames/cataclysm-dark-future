// DDA source: tests/item_group_test.cpp (DDA commit b4b62254ae2), cases
// truncate_spawn_when_items_dont_fit and spill_when_items_dont_fit. Translated for CDF (TT1-TT8):
// spawned items are detached_ptr<item>, and the contents come from all_items_top().
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "catch/catch.hpp"
#include "item.h"
#include "item_group.h"
#include "type_id.h"

static const itype_id itype_test_balloon( "test_balloon" );

TEST_CASE( "truncate_spawn_when_items_dont_fit", "[item_group]" )
{
    // This item group is a 10L container with three 4L objects.  We should
    // always see exactly 2 of the 3.
    // Moreover, we should see all possible pairs chosen from amongst those 3.
    item_group_id truncate_test_id( "test_truncating_to_container" );
    std::set<std::pair<itype_id, itype_id>> observed_pairs;

    for( int i = 0; i < 100; ++i ) {
        const std::vector<detached_ptr<item>> items = item_group::items_from( truncate_test_id );
        REQUIRE( items.size() == 1 );
        REQUIRE( items[0]->typeId() == itype_test_balloon );
        const std::vector<item *> contents = items[0]->all_items_top();
        REQUIRE( contents.size() == 2 );
        observed_pairs.emplace( contents.front()->typeId(), contents.back()->typeId() );
    }

    CAPTURE( observed_pairs );

    CHECK( observed_pairs.size() == 6 );
}

TEST_CASE( "spill_when_items_dont_fit", "[item_group]" )
{
    // This item group is a 10L container with three 4L objects.  We should
    // always see 2 in the container and one outside.
    // Moreover, we should see all possible combinations chosen from amongst those 3.
    item_group_id truncate_test_id( "test_spilling_from_container" );
    std::set<std::pair<itype_id, itype_id>> observed_pairs_inside;
    std::set<itype_id> observed_outside;

    for( int i = 0; i < 100; ++i ) {
        const std::vector<detached_ptr<item>> items = item_group::items_from( truncate_test_id );
        REQUIRE( items.size() == 2 );
        const item *container;
        const item *other;
        if( items[0]->typeId() == itype_test_balloon ) {
            container = &*items[0];
            other = &*items[1];
        } else {
            container = &*items[1];
            other = &*items[0];
        }
        REQUIRE( container->typeId() == itype_test_balloon );
        const std::vector<const item *> contents = container->all_items_top();
        REQUIRE( contents.size() == 2 );
        observed_pairs_inside.emplace( contents.front()->typeId(), contents.back()->typeId() );
        observed_outside.emplace( other->typeId() );
    }

    CAPTURE( observed_pairs_inside );
    CAPTURE( observed_outside );

    CHECK( observed_pairs_inside.size() == 6 );
    CHECK( observed_outside.size() == 3 );
}
