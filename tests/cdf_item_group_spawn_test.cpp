// CDF-authored (J6). With the DDA overflow rule (owner decision D13) an item that does not fit the
// container-item or contents-item target of a group is lost with a debugmsg, and nothing at
// load time spawns the groups. This spawns every item group a few times so that authored data
// that no longer fits (D3: no extra length factor) is found by the test run instead of in play.
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "catch/catch.hpp"
#include "debug.h"
#include "item.h"
#include "item_factory.h"
#include "item_group.h"
#include "type_id.h"

TEST_CASE( "every item group spawns without losing items", "[item_group][spawn_all]" )
{
    std::vector<std::string> failures;
    // These two TEST_DATA groups exist to show the overflow rule (cdf_item_pocket_test.cpp)
    const std::set<item_group_id> expected_loss = { item_group_id( "test_long_rod_in_pouch" ),
                                                    item_group_id( "test_pouch_with_long_rod" )
                                                  };
    for( const item_group_id &group_id : item_controller->get_all_group_names() ) {
        if( expected_loss.count( group_id ) > 0 ) {
            continue;
        }
        // Several rolls: most groups are random
        for( int i = 0; i < 3; ++i ) {
            const std::string msg = capture_debugmsg_during( [&group_id]() {
                std::vector<detached_ptr<item>> spawned = item_group::items_from( group_id );
            } );
            if( !msg.empty() ) {
                failures.push_back( group_id.str() + ": " + msg );
                break;
            }
        }
    }
    CAPTURE( failures );
    CHECK( failures.empty() );
}
