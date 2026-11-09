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

namespace
{
// the size check of molle_attach_actor::use (src/iuse_actor.cpp:4451): the pouch must be PALS and fit the vacancies
auto pouch_fits( const item &vest, const item &pouch ) -> bool
{
    const int vacancies = molle_size( vest ) - vest.contents.get_additional_space_used();
    return pouch.can_attach_as_pocket() && pouch.get_pocket_size() <= vacancies;
}

// attaches the way the MOLLE tests do (tests/molle_test.cpp:92); false when the size check refuses it
auto attach_pouch( item &vest, const char *pouch_id ) -> bool
{
    detached_ptr<item> pouch = item::spawn( itype_id( pouch_id ), calendar::turn );
    if( !pouch_fits( vest, *pouch ) ) {
        return false;
    }
    vest.contents.add_pocket( std::move( pouch ) );
    return true;
}
} // namespace

TEST_CASE( "pch1_pouch_attaches_to_a_vest", "[armor][pch1]" )
{
    clear_all_state();
    REQUIRE( itype_id( "load_bearing_vest" ).is_valid() );
    detached_ptr<item> vest = item::spawn( itype_id( "load_bearing_vest" ), calendar::turn );
    // CDF: load_bearing_vest attach_molle size 8 (cdf_molle_carriers.json, DDA ammo_pouch.json:748)
    REQUIRE( molle_size( *vest ) == 8 );
    REQUIRE( vest->contents.get_additional_space_used() == 0 );
    // CDF: radio_pouch is PALS_SMALL (size 1), h2o_pouch is PALS_MEDIUM (size 2): cdf_molle_pouches.json
    CHECK( attach_pouch( *vest, "radio_pouch" ) );
    CHECK( vest->contents.get_additional_space_used() == 1 );
    CHECK( attach_pouch( *vest, "h2o_pouch" ) );
    CHECK( vest->contents.get_additional_space_used() == 3 );
    const std::vector<const item *> attached = vest->contents.get_added_pockets();
    REQUIRE( attached.size() == 2 );
    CHECK( attached[0]->typeId() == itype_id( "radio_pouch" ) );
    CHECK( attached[1]->typeId() == itype_id( "h2o_pouch" ) );
    // the pouches' pockets are the vest's now (h2o_pouch has 2, radio_pouch 1)
    CHECK( vest->contents.get_pockets().size() >= 3 );
}

TEST_CASE( "pch1_oversize_pouch_is_refused", "[armor][pch1]" )
{
    clear_all_state();
    REQUIRE( itype_id( "light_load_bearing_vest" ).is_valid() );
    detached_ptr<item> vest = item::spawn( itype_id( "light_load_bearing_vest" ), calendar::turn );
    // CDF: light_load_bearing_vest attach_molle size 4 (cdf_molle_carriers.json, DDA ammo_pouch.json:721)
    REQUIRE( molle_size( *vest ) == 4 );
    // CDF: tool_pouch is PALS_LARGE (size 3), flashlight_pouch PALS_SMALL (size 1); the brief names
    // flashlight_pouch as the large one but DDA gives it PALS_SMALL, so the large pouch here is tool_pouch
    CHECK( attach_pouch( *vest, "tool_pouch" ) );
    CHECK( vest->contents.get_additional_space_used() == 3 );
    // a second large pouch needs 3 of the 1 remaining
    CHECK_FALSE( attach_pouch( *vest, "tool_pouch" ) );
    CHECK( vest->contents.get_additional_space_used() == 3 );
    CHECK( vest->contents.get_added_pockets().size() == 1 );
    // a small pouch exactly fills the 4
    CHECK( attach_pouch( *vest, "flashlight_pouch" ) );
    CHECK( vest->contents.get_additional_space_used() == molle_size( *vest ) );
    CHECK_FALSE( attach_pouch( *vest, "radio_pouch" ) );
    CHECK( vest->contents.get_added_pockets().size() == 2 );
}

namespace
{
struct extra_row {
    const char *id;
    int pockets;           // pocket count
    const char *pals;      // the one PALS_* flag, or nullptr
};

struct belt_loop_row {
    const char *id;
    const char *pals;
    int pals_size;
    int weight_g;
    int volume_ml;
    int pocket_ml; // max_contains_volume of the single holster pocket
};
} // namespace

TEST_CASE( "pch1_extra_items_load", "[armor][pch1]" )
{
    clear_all_state();
    // CDF: values of data/json/items/armor/cdf_pouch_extras.json (DDA ammo_pouch.json:206-1524)
    const std::vector<extra_row> rows = {
        { "xl_chestrig", -1, nullptr },
        { "xs_chestrig", -1, nullptr },
        { "nylon_quiver", 1, "PALS_LARGE" },
        { "pipe_quiver", 1, nullptr },
        { "pipe_quiver_large", 1, nullptr },
        { "quiver_simple_cloth", 1, nullptr },
        { "xl_tacvest", -1, nullptr },
        { "xs_tacvest", -1, nullptr },
        { "load_bearing_vest_sling", 1, nullptr },
        { "heavy_load_bearing_vest_sling", 1, nullptr },
        { "ammo_satchel_leather", 1, "PALS_LARGE" },
        { "chestpouch_leather", 1, "PALS_SMALL" },
        { "belt_loop_small", 1, "PALS_SMALL" },
        { "belt_loop_medium", 1, "PALS_MEDIUM" },
        { "belt_loop_large", 1, "PALS_LARGE" },
    };
    for( const extra_row &row : rows ) {
        CAPTURE( row.id );
        REQUIRE( itype_id( row.id ).is_valid() );
        detached_ptr<item> it = item::spawn( itype_id( row.id ), calendar::turn );
        if( row.pockets >= 0 ) {
            CHECK( static_cast<int>( it->contents.get_pockets().size() ) == row.pockets );
        }
        if( row.pals != nullptr ) {
            CHECK( it->has_flag( flag_id( row.pals ) ) );
            CHECK( it->can_attach_as_pocket() );
        }
    }
    // CDF: the sized variants copy-from chestrig/tacvest and add the size flags (DDA ammo_pouch.json:206-217, 703-712; the PREFIX_* flags are not in CDF)
    CHECK( item::spawn( itype_id( "xl_chestrig" ), calendar::turn )->has_flag( flag_id( "OVERSIZE" ) ) );
    CHECK( item::spawn( itype_id( "xs_tacvest" ), calendar::turn )->has_flag( flag_id( "UNDERSIZE" ) ) );
    // CDF: the slings keep the load bearing vests' MOLLE size (DDA ammo_pouch.json:775, 835)
    CHECK( molle_size( *item::spawn( itype_id( "load_bearing_vest_sling" ), calendar::turn ) ) == 8 );
    CHECK( molle_size( *item::spawn( itype_id( "heavy_load_bearing_vest_sling" ), calendar::turn ) ) == 14 );
    // CDF: belt loops hold what cdf_pouch_extras.json says (DDA ammo_pouch.json:1464-1524)
    const std::vector<belt_loop_row> loops = {
        { "belt_loop_small", "PALS_SMALL", 1, 25, 25, 500 },
        { "belt_loop_medium", "PALS_MEDIUM", 2, 50, 50, 1500 },
        { "belt_loop_large", "PALS_LARGE", 3, 100, 100, 2000 },
    };
    for( const belt_loop_row &row : loops ) {
        CAPTURE( row.id );
        detached_ptr<item> it = item::spawn( itype_id( row.id ), calendar::turn );
        CHECK( it->get_pocket_size() == row.pals_size );
        CHECK( it->weight() == units::from_gram( row.weight_g ) );
        CHECK( it->volume() == units::from_milliliter( row.volume_ml ) );
        CHECK( it->has_flag( flag_id( "CANT_WEAR" ) ) );
        const std::list<item_pocket> &pockets = it->contents.get_pockets();
        REQUIRE( pockets.size() == 1 );
        CHECK( pockets.front().volume_capacity() == units::from_milliliter( row.pocket_ml ) );
    }
    // CDF: modular_papr_strap (item_restriction molle_papr_blower_off/on) is shipped by PAPR1, which adds the
    // blower it restricts to (was: not shipped, CHECK_FALSE). quiver_takedown_bow is shipped by TAKEDOWN1, which adds the
    // takedown_recurbow chain it needs (was: not shipped, CHECK_FALSE)
    CHECK( itype_id( "modular_papr_strap" ).is_valid() );
    CHECK( itype_id( "quiver_takedown_bow" ).is_valid() );
}
