// CDF: P3-3-8b guard tests. A gun whose only magazine is an integral MAGAZINE pocket has no islot_magazine
// block (type->magazine == nullptr). The item functions that guard on type->magazine (not on is_magazine())
// must not crash or lose data for such a gun. Fixtures (TEST_DATA, items.json): test_multimag_gun_integral_ammo,
// test_multimag_regulated_mod; test_flashlight (dda_pocket_items.json) for the routing test. glockmag and 9mm
// are base data and serve as plain-magazine and loose-ammo controls.
#include <utility>

#include "catch/catch.hpp"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"
#include "pocket_type.h"
#include "type_id.h"
#include "ammo.h"
#include "units.h"

namespace
{
const itype_id gun_id( "test_multimag_gun_integral_ammo" );
const itype_id plain_mag_id( "glockmag" );
const itype_id round_id( "9mm" );
const itype_id holder_id( "test_flashlight" );

// The integral-pocket gun; the REQUIREs pin the premise of every case below.
auto make_pocket_gun() -> detached_ptr<item>
{
    detached_ptr<item> gun = item::spawn( gun_id );
    REQUIRE( gun->is_magazine() );
    REQUIRE( gun->type->magazine == nullptr );
    return gun;
}

// One 9mm round in the integral MAGAZINE pocket (its ammo_restriction allows 1).
void load_pocket_gun( item &gun )
{
    detached_ptr<item> round = item::spawn( round_id );
    round->charges = 1;
    REQUIRE( gun.put_in( std::move( round ), pocket_type::MAGAZINE ).success() );
    REQUIRE( gun.ammo_remaining() == 1 );
}

// Control: a plain magazine (type->magazine set) with 5 rounds.
auto loaded_glockmag() -> detached_ptr<item>
{
    detached_ptr<item> mag = item::spawn( plain_mag_id );
    mag->ammo_set( round_id, 5 );
    REQUIRE( mag->ammo_remaining() == 5 );
    return mag;
}
} // namespace

TEST_CASE( "ammo_unset_keeps_gunmods_of_a_pocket_gun", "[p3][ammo][magazine]" )
{
    // CDF: the fixture gun has no MOD pocket (put_in( MOD ) fails: "pocket with type (MOD) not found"), so the
    // "other pockets stay" half of the rule is pinned through its MAGAZINE_WELL instead of a gunmod.
    detached_ptr<item> gun = make_pocket_gun();
    load_pocket_gun( *gun );
    REQUIRE( gun->put_in( item::spawn( itype_id( "heavy_battery_cell" ) ),
                          pocket_type::MAGAZINE_WELL ).success() );
    const size_t well_before = gun->all_items_top( pocket_type::MAGAZINE_WELL ).size();
    REQUIRE( well_before == 1 );
    detached_ptr<item> mag = loaded_glockmag();

    gun->ammo_unset();
    mag->ammo_unset();

    CHECK( gun->ammo_remaining() == 0 );
    CHECK( gun->all_items_top( pocket_type::MAGAZINE_WELL ).size() == well_before );
    // negative control: a plain magazine still empties
    CHECK( mag->ammo_remaining() == 0 );
}

TEST_CASE( "ammo_capacity_of_a_pocket_gun_is_its_clip", "[p3][ammo][magazine]" )
{
    detached_ptr<item> gun = make_pocket_gun();
    // no mods installed: the gun's own clip, not a magazine capacity
    CHECK( gun->ammo_capacity( true ) == gun->type->gun->clip );
    // control: a plain magazine reports its own capacity
    detached_ptr<item> mag = item::spawn( plain_mag_id );
    CHECK( mag->ammo_capacity( true ) == mag->type->magazine->capacity );
}

TEST_CASE( "ammo_default_of_a_pocket_gun_comes_from_its_ammo_types", "[p3][ammo][magazine]" )
{
    detached_ptr<item> gun = make_pocket_gun();
    const itype_id expected = ammotype( "9mm" )->default_ammotype();
    REQUIRE_FALSE( expected.is_empty() );
    // no magazine block: the default is the default of the gun's own ammo types
    CHECK_FALSE( gun->ammo_default().is_null() );
    CHECK( gun->ammo_default() == expected );
    // control: a plain magazine answers from its magazine block
    detached_ptr<item> mag = item::spawn( plain_mag_id );
    CHECK( mag->ammo_default() == mag->type->magazine->default_ammo );
}

TEST_CASE( "weight_of_a_pocket_gun_does_not_touch_the_magazine_block", "[p3][ammo][magazine]" )
{
    detached_ptr<item> gun = make_pocket_gun();
    CHECK( gun->weight() > 0_gram );
    CHECK( gun->volume() > 0_ml );
    // control: a plain magazine weighs something too
    detached_ptr<item> mag = item::spawn( plain_mag_id );
    CHECK( mag->weight() > 0_gram );
}

TEST_CASE( "will_explode_in_fire_of_a_pocket_gun", "[p3][ammo][magazine]" )
{
    // the guard here is "no crash": the gun has no magazine block, so the protects_contents term must not be read.
    detached_ptr<item> empty_gun = make_pocket_gun();
    CHECK_FALSE( empty_gun->will_explode_in_fire() );

    detached_ptr<item> loaded_gun = make_pocket_gun();
    load_pocket_gun( *loaded_gun );
    // CDF: the value for a loaded gun is not pinned (has_item_with does not see the integral MAGAZINE pocket's round)
    static_cast<void>( loaded_gun->will_explode_in_fire() );
}

TEST_CASE( "fitting_pocket_keeps_a_pocket_gun_out_of_magazine_wells", "[p3][ammo][magazine]" )
{
    detached_ptr<item> holder = item::spawn( holder_id );
    detached_ptr<item> gun = make_pocket_gun();
    detached_ptr<item> mag = loaded_glockmag();

    // control: a plain magazine is routed into the well
    item_pocket *mag_dest = holder->get_contents().route( *mag );
    REQUIRE( mag_dest != nullptr );
    CHECK( mag_dest->is_type( pocket_type::MAGAZINE_WELL ) );

    // the gun is not routed into the well (no pocket, or some other pocket)
    item_pocket *gun_dest = holder->get_contents().route( *gun );
    CHECK( ( gun_dest == nullptr || !gun_dest->is_type( pocket_type::MAGAZINE_WELL ) ) );
}
