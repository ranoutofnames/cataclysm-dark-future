// DDA source: tests/item_spawn_test.cpp (DDA commit b4b62254ae2), case
// correct_amounts_of_an_item_spawn_inside_a_container. Translated for CDF (TT1-TT8): spawned
// items are detached_ptr<item>, the cargo of a vehicle tile is read through the part with the
// cargo flag, clear_map_without_vision() is clear_map().
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "catch/catch.hpp"
#include "coordinates.h"
#include "enums.h"
#include "item.h"
#include "item_group.h"
#include "map/map.h"
#include "map_helpers.h"
#include "point.h"
#include "profession.h"
#include "recipe.h"
#include "test_data.h"
#include "type_id.h"
#include "units.h"
#include "vehicle/veh_type.h"
#include "vehicle/vehicle.h"
#include "vehicle/vpart_position.h"

static std::string get_section_name( const spawn_type &type )
{
    switch( type ) {
        case spawn_type::item_group:
            return "items spawned by itemgroups";
        case spawn_type::recipe:
            return "items spawned by recipe";
        case spawn_type::vehicle:
            return "items spawned by vehicle";
        case spawn_type::profession:
            return "items spawned by profession substitution";
        case spawn_type::map:
            return "items spawned by map::spawn_item";
        default:
            return "unknown type";
    }

    return "unknown type";
}

TEST_CASE( "correct_amounts_of_an_item_spawn_inside_a_container", "[item_spawn]" )
{
    REQUIRE_FALSE( test_data::container_spawn_data.empty() );

    for( std::pair<const spawn_type, std::vector<container_spawn_test_data>> &spawn_category :
         test_data::container_spawn_data ) {
        SECTION( get_section_name( spawn_category.first ) ) {
            REQUIRE_FALSE( spawn_category.second.empty() );
            for( const container_spawn_test_data &cs_data : spawn_category.second ) {
                GIVEN( cs_data.given ) {
                    // CDF: the items stay where they were created, the test only reads them
                    std::vector<detached_ptr<item>> spawned;
                    std::vector<item *> items;

                    switch( spawn_category.first ) {
                        case spawn_type::item_group:
                            spawned = item_group::items_from( cs_data.group );
                            for( detached_ptr<item> &it : spawned ) {
                                items.push_back( &*it );
                            }
                            break;
                        case spawn_type::recipe:
                            spawned = cs_data.recipe->create_results();
                            for( detached_ptr<item> &it : spawned ) {
                                items.push_back( &*it );
                            }
                            break;
                        case spawn_type::vehicle: {
                            clear_map();
                            map &here = get_map();
                            const tripoint_bub_ms vehpos = tripoint_bub_ms::zero();
                            vehicle *veh = here.add_vehicle( cs_data.vehicle, vehpos, 0_degrees, 0, 0 );
                            REQUIRE( veh );
                            REQUIRE( here.get_vehicles().size() == 1 );
                            const tripoint_bub_ms pos = vehpos;
                            const std::optional<vpart_reference> ovp_cargo =
                                here.veh_at( pos ).part_with_feature( vpart_bitflags::VPFLAG_CARGO, true );
                            REQUIRE( ovp_cargo );
                            for( item * const &it : ovp_cargo->vehicle().get_items( ovp_cargo->part_index() ) ) {
                                items.push_back( it );
                            }
                            break;
                        }
                        case spawn_type::profession: {
                            std::vector<trait_id> traits;
                            for( const trait_id &trait : cs_data.profession->get_locked_traits() ) {
                                traits.push_back( trait );
                            }
                            spawned = cs_data.profession->items( false, traits );
                            for( detached_ptr<item> &it : spawned ) {
                                items.push_back( &*it );
                            }
                            break;
                        }
                        case spawn_type::map: {
                            clear_map();
                            map &here = get_map();
                            here.spawn_item( tripoint_bub_ms::zero(), cs_data.item, 1, cs_data.charges );
                            for( item * const &it : here.i_at( tripoint_bub_ms::zero() ) ) {
                                items.push_back( it );
                            }
                            break;
                        }
                        default:
                            continue;
                    }

                    REQUIRE( items.size() == 1 );
                    CAPTURE( items[0]->tname() );
                    REQUIRE_FALSE( items[0]->empty() );

                    int count = 0;
                    for( const item *it : items[0]->all_items_top() ) {
                        count += it->count();
                    }

                    CHECK( count == cs_data.expected_amount );
                }
            }
        }
    }
}
