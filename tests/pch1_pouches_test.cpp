#include "catch/catch.hpp"

#include <list>
#include <string>
#include <vector>

#include "bodypart.h"
#include "calendar.h"
#include "item.h"
#include "item_contents.h"
#include "item_factory.h"
#include "item_pocket.h"
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

namespace
{
struct pouch_row {
    const char *id;
    const char *pals;    // the one PALS_* flag
    int size;            // get_pocket_size() of that flag
    int pockets;         // number of pockets
    int volume_ml;       // max_contains_volume of pocket 1 in ml, or -1 when DDA gives none
};
} // namespace

TEST_CASE( "pch1_pouches_have_pals_size_and_pockets", "[armor][pch1]" )
{
    clear_all_state();
    // CDF: values of data/json/items/armor/cdf_molle_pouches.json (DDA ammo_pouch.json:914-1554)
    const std::vector<pouch_row> rows = {
        { "radio_pouch", "PALS_SMALL", 1, 1, 500 },
        { "gadget_pouch", "PALS_SMALL", 1, 1, 1000 },
        { "h2o_pouch", "PALS_MEDIUM", 2, 2, 2048 },
        { "canteen_pouch", "PALS_MEDIUM", 2, 1, 1751 },
        { "tool_pouch", "PALS_LARGE", 3, 1, 2753 },
        { "flashlight_pouch", "PALS_SMALL", 1, 1, 500 },
        { "tactical_holster", "PALS_SMALL", 1, 1, 1000 },
        { "gas_mask_pouch", "PALS_MEDIUM", 2, 3, 3546 },
        { "tactical_grenade_pouch", "PALS_SMALL", 1, 1, 750 },
        { "triple_stacker_pouch", "PALS_LARGE", 3, 6, 300 },
        { "tactical_shotshell_pouch", "PALS_MEDIUM", 2, 1, -1 },
        { "sustainment_pouch", "PALS_MEDIUM", 2, 1, 7000 },
    };
    const std::vector<std::string> all_flags = { "PALS_SMALL", "PALS_MEDIUM", "PALS_LARGE" };
    for( const pouch_row &row : rows ) {
        CAPTURE( row.id );
        REQUIRE( itype_id( row.id ).is_valid() );
        detached_ptr<item> it = item::spawn( itype_id( row.id ), calendar::turn );
        int flag_count = 0;
        for( const std::string &flag : all_flags ) {
            flag_count += it->has_flag( flag_id( flag ) ) ? 1 : 0;
        }
        CHECK( flag_count == 1 );
        CHECK( it->has_flag( flag_id( row.pals ) ) );
        CHECK( it->can_attach_as_pocket() );
        CHECK( it->get_pocket_size() == row.size );
        const std::list<item_pocket> &pockets = it->contents.get_pockets();
        REQUIRE( static_cast<int>( pockets.size() ) == row.pockets );
        if( row.volume_ml >= 0 ) {
            CHECK( pockets.front().volume_capacity() == units::from_milliliter( row.volume_ml ) );
        }
    }
}
