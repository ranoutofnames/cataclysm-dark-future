#include "catch/catch.hpp"

#include <string>
#include <vector>

#include "bodypart.h"
#include "calendar.h"
#include "item.h"
#include "item_factory.h"
#include "itype.h"
#include "material.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "state_helpers.h"
#include "units.h"

// PCH1: DDA load bearing vests, MOLLE rucksacks and pouches as CDF data
// (data/json/items/armor/cdf_molle_carriers.json and followers). Cases are appended per step.

namespace
{
// size of the attach_molle action of an item, or -1 when it has none
auto molle_size( const item &it ) -> int
{
    const use_function *use = it.get_use( "attach_molle" );
    if( use == nullptr ) {
        return -1;
    }
    const molle_attach_actor *actor = dynamic_cast<const molle_attach_actor *>( use->get_actor_ptr() );
    return actor == nullptr ? -1 : actor->size;
}

struct carrier_row {
    const char *id;
    int molle;       // attach_molle size
    int weight_g;
    int coverage;    // torso coverage (of the sub-location when sub is set, else of the whole torso)
    const char *sub; // sub-location that carries the coverage, or nullptr
};
} // namespace

TEST_CASE( "pch1_carriers_exist_with_dda_molle_sizes", "[armor][pch1]" )
{
    clear_all_state();
    const bodypart_id torso( "torso" );
    // CDF: values of data/json/items/armor/cdf_molle_carriers.json (DDA ammo_pouch.json:721-914, storage.json:2105-2300)
    const std::vector<carrier_row> rows = {
        { "light_load_bearing_vest", 4, 500, 55, "torso_upper" },
        { "load_bearing_vest", 8, 1250, 85, "torso_upper" },
        { "heavy_load_bearing_vest", 14, 2000, 85, "torso_upper" },
        { "heavy_load_bearing_vest_breacher", 8, 2900, 85, "torso_upper" },
        { "molle_medium_rucksack", 8, 1600, 80, nullptr },
        { "molle_large_rucksack", 10, 2200, 90, nullptr },
    };
    for( const carrier_row &row : rows ) {
        CAPTURE( row.id );
        REQUIRE( itype_id( row.id ).is_valid() );
        detached_ptr<item> it = item::spawn( itype_id( row.id ), calendar::turn );
        CHECK( molle_size( *it ) == row.molle );
        CHECK( it->get_use( "detach_molle" ) != nullptr );
        CHECK( it->covers( torso ) );
        if( row.sub != nullptr ) {
            CHECK( it->get_coverage( torso, sub_bodypart_str_id( row.sub ) ) == row.coverage );
        } else {
            CHECK( it->get_coverage( torso ) == row.coverage );
        }
        CHECK( it->weight() == units::from_gram( row.weight_g ) );
    }
    // CDF: deployment_bag is a PALS pouch in DDA (storage.json:1005, flag PALS_LARGE), not a MOLLE carrier
    REQUIRE( itype_id( "deployment_bag" ).is_valid() );
    detached_ptr<item> bag = item::spawn( itype_id( "deployment_bag" ), calendar::turn );
    CHECK( bag->has_flag( flag_id( "PALS_LARGE" ) ) );
    CHECK( bag->get_pocket_size() == 3 );
    CHECK( molle_size( *bag ) == -1 );
    CHECK( bag->weight() == units::from_gram( 480 ) );
    CHECK( bag->volume() == units::from_milliliter( 300 ) );
}

TEST_CASE( "pch1_breacher_inherits_the_heavy_vest", "[armor][pch1]" )
{
    clear_all_state();
    const bodypart_id torso( "torso" );
    REQUIRE( itype_id( "heavy_load_bearing_vest_breacher" ).is_valid() );
    detached_ptr<item> breacher = item::spawn( itype_id( "heavy_load_bearing_vest_breacher" ), calendar::turn );
    // CDF: volume is not restated by the breacher; it comes from heavy_load_bearing_vest (1750 ml, DDA ammo_pouch.json:808)
    CHECK( breacher->volume() == units::from_milliliter( 1750 ) );
    // CDF: coverage 85 and the nylon / material_thickness of the heavy vest, price 130 USD
    CHECK( breacher->get_coverage( torso, sub_bodypart_str_id( "torso_upper" ) ) == 85 );
    CHECK( breacher->made_of( material_id( "nylon" ) ) );
    // CDF: the breacher's own weight (2900 g) and the holster action DDA adds in front of attach_molle
    CHECK( breacher->weight() == units::from_gram( 2900 ) );
    CHECK( breacher->get_use( "holster" ) != nullptr );
    // CDF: DDA's breacher webbing is 8, not the heavy vest's 14
    CHECK( molle_size( *breacher ) == 8 );
    // CDF: the heavy vest has no holster action
    detached_ptr<item> heavy = item::spawn( itype_id( "heavy_load_bearing_vest" ), calendar::turn );
    CHECK( heavy->get_use( "holster" ) == nullptr );
    CHECK( heavy->weight() == units::from_gram( 2000 ) );
    CHECK( heavy->volume() == breacher->volume() );
    CHECK( heavy->get_coverage( torso, sub_bodypart_str_id( "torso_upper" ) ) == 85 );
}
