#include "activity_actor.h"
#include "activity_actor_definitions.h"
#include "avatar.h"
#include "avatar_action.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "flag.h"
#include "item.h"
#include "item_functions.h"
#include "item_pocket.h"
#include "itype.h"
#include "map/map.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "reload/reload.h"
#include "state_helpers.h"
#include "type_id.h"
#include "vehicle/vehicle.h"
#include "itype_slots/islot_gunmod.h"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// CDF: P3-6-3b test hooks, defined in src/avatar_action.cpp (precedent: game_menus::inv::select_ammo_shows).
namespace avatar_action
{
auto reload_item_picker_shows( const item &it ) -> bool;
auto reload_item_picker_opened_count() -> int;
} // namespace avatar_action

namespace {

// CDF: sets the thief mode and pins it with THIEF_MODE_KEEP = YES, so avatar_action::reload's
// steal check never reaches the pickup::query_thief() popup; the dtor restores the defaults so no
// state leaks between cases.
struct thief_mode_guard {
    explicit thief_mode_guard(const std::string& mode) {
        avatar& who = get_avatar();
        who.set_value("THIEF_MODE", mode);
        who.set_value("THIEF_MODE_KEEP", "YES");
    }
    ~thief_mode_guard() {
        avatar& who = get_avatar();
        who.set_value("THIEF_MODE", "THIEF_ASK");
        who.set_value("THIEF_MODE_KEEP", "NO");
    }
};

struct floor_gun_fixture {
    item& gun;
    item& ammo;
};

// CDF: sw_619 takes 38_special (data/json/items/ranged/pistols.json); both ids are CDF data.
auto spawn_floor_gun_and_ammo(avatar& who) -> floor_gun_fixture {
    who.wear_item(item::spawn("backpack", calendar::start_of_cataclysm), false);
    detached_ptr<item> det = item::spawn("sw_619", calendar::start_of_cataclysm, 0);
    item& gun = *det;
    get_map().add_item(who.bub_pos(), std::move(det));
    item& ammo = who.i_add(item::spawn("38_special", calendar::start_of_cataclysm, 6));
    REQUIRE(gun.can_reload_with(ammo.typeId()));
    return {gun, ammo};
}

auto is_on_floor_at(const tripoint_bub_ms& pos, const item& it) -> bool {
    for (const item* floor_it : get_map().i_at(pos)) {
        if (floor_it == &it) { return true; }
    }
    return false;
}

} // namespace

// CDF: P3-6-3a; DDA game.cpp:7111-7120 and activity_actor.cpp:10004-10024 (check_stealing).
TEST_CASE("reload_item_owned_by_other_honest_mode_declines", "[p3][reload][entry]") {
    clear_all_state();
    avatar& who = get_avatar();
    const auto fixture = spawn_floor_gun_and_ammo(who);
    item& gun = fixture.gun;
    const tripoint_bub_ms pos = who.bub_pos();
    // CDF: foreign faction, data/json/npcs/factions.json:468; the avatar is your_followers
    // (avatar.cpp:1280-1283).
    gun.set_owner(faction_id("hells_raiders"));
    REQUIRE_FALSE(gun.is_owned_by(who, true));
    REQUIRE(is_on_floor_at(pos, gun));

    const thief_mode_guard guard("THIEF_HONEST");
    avatar_action::reload(gun, false, false);

    CHECK_FALSE(who.activity);
    CHECK_FALSE(who.has_item(gun));
    CHECK(is_on_floor_at(pos, gun));
}

TEST_CASE("reload_item_owned_by_other_steal_mode_proceeds", "[p3][reload][entry]") {
    clear_all_state();
    avatar& who = get_avatar();
    const auto fixture = spawn_floor_gun_and_ammo(who);
    item& gun = fixture.gun;
    item& ammo = fixture.ammo;

    std::string mode = "THIEF_HONEST";
    SECTION("THIEF_STEAL mode with a foreign owner") {
        // CDF: foreign faction, data/json/npcs/factions.json:468.
        gun.set_owner(faction_id("hells_raiders"));
        REQUIRE_FALSE(gun.is_owned_by(who, true));
        mode = "THIEF_STEAL";
    }
    SECTION("own faction under THIEF_HONEST") {
        gun.set_owner(who);
        REQUIRE(gun.is_owned_by(who, true));
    }
    SECTION("no owner under THIEF_HONEST") { REQUIRE(gun.is_owned_by(who, true)); }

    const thief_mode_guard guard(mode);
    avatar_action::reload(gun, false, false);

    REQUIRE(who.activity);
    CHECK(who.activity->id() == activity_id("ACT_RELOAD"));
    CHECK(who.has_item(gun));
    // CDF: P3-6-2a; the old targets facts, asserted through the reload actor.
    const auto* actor = dynamic_cast<const reload_activity_actor *>( who.activity->get_actor() );
    REQUIRE(actor);
    CHECK(actor->target() == &gun);
    CHECK(actor->ammo() == &ammo);
}

TEST_CASE("reload_carried_item_costs_no_obtain_moves", "[p3][reload][entry]") {
    clear_all_state();
    avatar& who = get_avatar();

    SECTION("carried gun") {
        who.wear_item(item::spawn("backpack", calendar::start_of_cataclysm), false);
        // CDF: sw_619 takes 38_special.
        item& gun = who.i_add(item::spawn("sw_619", calendar::start_of_cataclysm, 0));
        item& ammo = who.i_add(item::spawn("38_special", calendar::start_of_cataclysm, 6));
        REQUIRE(gun.can_reload_with(ammo.typeId()));
        REQUIRE(who.has_item(gun));
        REQUIRE_FALSE(who.is_wielding(gun));
        REQUIRE_FALSE(who.is_worn(gun));
        // CDF: character_item_location::obtain_cost (locations.cpp:179-185) is an
        // inventory-handling cost.
        const int cost = gun.obtain_cost(who);
        REQUIRE(cost > 0);

        const int before = who.moves;
        avatar_action::reload(gun, false, false);

        REQUIRE(who.activity);
        CHECK(who.moves == before);
        CHECK(who.has_item(gun));
    }

    SECTION("gun on the floor is still fetched") {
        const auto fixture = spawn_floor_gun_and_ammo(who);
        item& gun = fixture.gun;
        REQUIRE_FALSE(who.has_item(gun));

        const int before = who.moves;
        avatar_action::reload(gun, false, false);

        REQUIRE(who.activity);
        CHECK(who.has_item(gun));
        CHECK(who.moves < before);
    }
}

// CDF: P3-6-3a; DDA game.cpp:7176-7184 (reload_wielded).
TEST_CASE("reload_wielded_reloads_only_the_wielded_item", "[p3][reload][entry]") {
    clear_all_state();
    avatar& who = get_avatar();
    who.wear_item(item::spawn("backpack", calendar::start_of_cataclysm), false);

    SECTION("wielded gun") {
        // CDF: sw_619 takes 38_special.
        REQUIRE(!who.wield(item::spawn("sw_619", calendar::start_of_cataclysm, 0)));
        who.i_add(item::spawn("38_special", calendar::start_of_cataclysm, 6));

        avatar_action::reload_wielded();

        REQUIRE(who.activity);
        // CDF: P3-6-2a; the old targets facts, asserted through the reload actor.
        const auto* actor = dynamic_cast<const reload_activity_actor *>( who.activity->get_actor() );
        REQUIRE(actor);
        REQUIRE(actor->ammo());
        REQUIRE(!who.wielded_items().empty());
        CHECK(actor->target() == who.wielded_items().front());
    }

    SECTION("nothing wielded") {
        who.i_add(item::spawn("sw_619", calendar::start_of_cataclysm, 0));
        who.i_add(item::spawn("38_special", calendar::start_of_cataclysm, 6));
        REQUIRE(who.wielded_items().empty());

        avatar_action::reload_wielded();

        CHECK_FALSE(who.activity);
    }

    SECTION("wielded item is not reloadable") {
        // CDF: rock is not reloadable.
        REQUIRE(!who.wield(item::spawn("rock", calendar::start_of_cataclysm)));
        REQUIRE(!who.wielded_items().empty());
        REQUIRE_FALSE(who.wielded_items().front()->is_reloadable());
        who.i_add(item::spawn("sw_619", calendar::start_of_cataclysm, 0));
        who.i_add(item::spawn("38_special", calendar::start_of_cataclysm, 6));

        avatar_action::reload_wielded();

        CHECK_FALSE(who.activity);
    }
}

// TODO: nested containers and frozen liquids.
// CDF: P3-6-3a; ported whole from DDA tests/reloading_test.cpp:1129-1229.
TEST_CASE("reload_liquid_container", "[p3][reload][entry][liquid]") {
    // CDF: clear_all_state() replaces clear_avatar() + clear_map_without_vision().
    clear_all_state();
    avatar& dummy = get_avatar();
    // CDF: item::spawn and detached_ptr replace stack-built items; ids are CDF data (storage.json,
    // containers.json, comestibles/drink.json).
    dummy.wear_item(item::spawn("bigback", calendar::start_of_cataclysm), false);
    // CDF: wield( detached_ptr ) returns an empty detached_ptr on success.
    REQUIRE(!dummy.wield(item::spawn("2lcanteen", calendar::start_of_cataclysm)));
    // CDF: no get_wielded_item(); wielded_items() holds at most one item (melee.cpp:1197-1209).
    const auto wielded = [&dummy]() -> item& {
        const auto items = dummy.wielded_items();
        REQUIRE(!items.empty());
        return *items.front();
    };

    // CDF: i_add returns item &, not an item_location.
    item* ammo_jug = &dummy.i_add(item::spawn("jug_plastic", calendar::start_of_cataclysm));
    ammo_jug->put_in(item::spawn("water_clean", calendar::turn_zero, 2), pocket_type::CONTAINER);
    units::volume ammo_volume = ammo_jug->get_contents_volume();

    SECTION("reload liquid into empty container") {
        avatar_action::reload_wielded();
        REQUIRE(dummy.activity);
        process_activity(dummy);
        CHECK(wielded().get_contents_volume() == ammo_volume);
        CHECK(ammo_jug->get_contents_volume() == units::volume());
    }

    SECTION("reload liquid into partially filled container with same type liquid") {
        detached_ptr<item> water_one = item::spawn("water_clean", calendar::turn_zero, 1);
        units::volume initial_volume = water_one->volume();
        // CDF: put_in( detached_ptr, pocket_type ) returns ret_val<void>; REQUIRE so a failed
        // put_in cannot pass silently.
        REQUIRE(wielded().put_in(std::move(water_one), pocket_type::CONTAINER).success());
        avatar_action::reload_wielded();
        REQUIRE(dummy.activity);
        process_activity(dummy);
        CHECK(wielded().get_contents_volume() == ammo_volume + initial_volume);
        CHECK(ammo_jug->get_contents_volume() == units::volume());
    }

    SECTION("reload liquid into partially filled container with different type liquid") {
        detached_ptr<item> milk_one = item::spawn("milk", calendar::turn_zero, 1);
        units::volume initial_volume = milk_one->volume();
        // CDF: put_in( detached_ptr, pocket_type ) returns ret_val<void>; REQUIRE so a failed
        // put_in cannot pass silently.
        REQUIRE(wielded().put_in(std::move(milk_one), pocket_type::CONTAINER).success());
        avatar_action::reload_wielded();
        if (!!dummy.activity) { process_activity(dummy); }
        CHECK(wielded().get_contents_volume() == initial_volume);
        CHECK(ammo_jug->get_contents_volume() == ammo_volume);
    }

    SECTION("reload liquid into container containing a non-liquid") {
        detached_ptr<item> pebble = item::spawn("pebble", calendar::turn_zero, 1);
        units::volume initial_volume = pebble->volume();
        // CDF: put_in( detached_ptr, pocket_type ) returns ret_val<void>; REQUIRE so a failed
        // put_in cannot pass silently.
        REQUIRE(wielded().put_in(std::move(pebble), pocket_type::CONTAINER).success());
        avatar_action::reload_wielded();
        if (!!dummy.activity) { process_activity(dummy); }
        CHECK(wielded().get_contents_volume() == initial_volume);
        CHECK(ammo_jug->get_contents_volume() == ammo_volume);
    }

    SECTION("reload liquid container with more liquid than it can hold") {
        // CDF: fill_with( const item & ) is the DDA-shaped overload (item.h:1754); it takes a
        // prototype item.
        const detached_ptr<item> water = item::spawn("water_clean", calendar::turn_zero, 1);
        ammo_jug->fill_with(*water);
        ammo_volume = ammo_jug->get_contents_volume();
        avatar_action::reload_wielded();
        REQUIRE(dummy.activity);
        process_activity(dummy);
        CHECK(wielded().get_volume_capacity() == wielded().get_contents_volume());
        CHECK(ammo_jug->get_contents_volume() + wielded().get_contents_volume() == ammo_volume);
    }

    SECTION("liquid reload from map") {
        const tripoint_bub_ms test_origin(60, 60, 0);
        map& here = get_map();
        dummy.setpos(test_origin);
        const tripoint_bub_ms near_point = test_origin + tripoint_east;

        SECTION("liquid in container on floor") {
            // CDF: remove the jug from the inventory (DDA: ammo_jug.remove_item()), then place a
            // floor bottle.
            static_cast<void>(dummy.inv_remove_item(ammo_jug));
            detached_ptr<item> bottle = item::spawn("bottle_plastic", calendar::start_of_cataclysm);
            ammo_jug = &*bottle;
            here.add_item(near_point, std::move(bottle));
            const detached_ptr<item> water =
                item::spawn("water_clean", calendar::start_of_cataclysm);
            ammo_jug->fill_with(*water);
            ammo_volume = ammo_jug->get_contents_volume();
            avatar_action::reload_wielded();
            REQUIRE(dummy.activity);
            process_activity(dummy);
            CHECK(wielded().get_contents_volume() == ammo_volume);
            CHECK(ammo_jug->get_contents_volume() == units::volume());
        }

        SECTION("liquid spill on floor") {
            REQUIRE(ammo_jug->spill_contents(near_point));
            avatar_action::reload_wielded();
            if (!!dummy.activity) { process_activity(dummy); }
            CHECK(ammo_jug->get_contents_volume() == units::volume());
            CHECK(wielded().get_contents_volume() == units::volume());
        }
    }
}

// ---------------------------------------------------------------------------
// CDF: P3-6-3b; CDF-only counterpart of DDA tests/reloading_test.cpp 514-635 (speedloader_reloading) and its
// helper test_reloading (67-101). The whole DDA case is ported by the 9-5 lane (cut-tests.md, 9-5 B9) once this
// job is merged; this case pins the behaviour that job fixes with checks that can bite here. p3_sw_610
// (data/mods/TEST_DATA/reload_speedloader_p3.json) is DDA's sw_610: a revolver whose integral MAGAZINE pocket
// takes the 6-round speedloader (allowed_speedloaders). CDF's own sw_610 uses the legacy `magazines` list and has
// no MAGAZINE pocket, so it never reaches the code under test. Items are item pointers (D1). The case sets no
// avatar::ammo_location and finishes every activity it starts, so it leaves no safe_reference state alive
// (the 8-2a-sel lesson).
// ---------------------------------------------------------------------------

static detached_ptr<item> speedloader_holding( int rounds )
{
    detached_ptr<item> loader = item::spawn( "40_speedloader6", calendar::start_of_cataclysm );
    if( rounds > 0 ) {
        REQUIRE( loader->put_in( item::spawn( "40sw", calendar::start_of_cataclysm, rounds ),
                                 pocket_type::MAGAZINE ).success() );
    }
    return loader;
}

TEST_CASE( "speedloader_reload_resolves_the_integral_magazine_pocket", "[p3][reload][entry][6-3b]" )
{
    clear_all_state();
    avatar &who = get_avatar();
    who.wear_item( item::spawn( "backpack", calendar::start_of_cataclysm ), false );

    // Fixture premise: the gun is reloadable, its pocket allows the loader and the loader fits now. Green with
    // the old body too; a red check here means the fixture is wrong on CDF data.
    SECTION( "the fixture gun takes its speedloader at pocket level" ) {
        item &gun = who.i_add( item::spawn( "p3_sw_610", calendar::start_of_cataclysm ) );
        item &loader = who.i_add( speedloader_holding( 3 ) );
        CHECK( gun.is_reloadable() );
        CHECK( gun.allows_speedloader( itype_id( "40_speedloader6" ) ) );
        CHECK( gun.can_reload_with( loader, true ) );
    }

    SECTION( "item::reload loads a partly filled speedloader into an empty gun" ) {
        item &gun = who.i_add( item::spawn( "p3_sw_610", calendar::start_of_cataclysm ) );
        item &loader = who.i_add( speedloader_holding( 3 ) );
        REQUIRE( gun.ammo_remaining() == 0 );
        REQUIRE( loader.ammo_remaining() == 3 );

        const bool loaded = gun.reload( who, loader, 3 );
        CHECK( loaded );
        CHECK( gun.ammo_remaining() == 3 );
        CHECK( loader.ammo_remaining() == 0 );
    }

    SECTION( "item::reload loads a full speedloader into an empty gun" ) {
        item &gun = who.i_add( item::spawn( "p3_sw_610", calendar::start_of_cataclysm ) );
        item &loader = who.i_add( speedloader_holding( 6 ) );
        REQUIRE( gun.ammo_remaining() == 0 );
        REQUIRE( loader.ammo_remaining() == 6 );

        const bool loaded = gun.reload( who, loader, 6 );
        CHECK( loaded );
        CHECK( gun.ammo_remaining() == 6 );
        CHECK( loader.ammo_remaining() == 0 );
    }

    // DDA 526-533 ("empty gun" / "partially empty speedloader"): the wielded gun is reloaded through the
    // activity, which calls item::reload without a pocket index.
    SECTION( "reload_wielded loads a partly filled speedloader into the wielded gun" ) {
        REQUIRE( !who.wield( item::spawn( "p3_sw_610", calendar::start_of_cataclysm ) ) );
        REQUIRE( !who.wielded_items().empty() );
        item &gun = *who.wielded_items().front();
        who.i_add( speedloader_holding( 3 ) );
        REQUIRE( gun.ammo_remaining() == 0 );

        avatar_action::reload_wielded();
        REQUIRE( who.activity );
        process_activity( who );
        CHECK( gun.ammo_remaining() == 3 );
    }

    // DDA 535-542 ("empty gun" / "full speedloader").
    SECTION( "reload_wielded loads a full speedloader into the wielded gun" ) {
        REQUIRE( !who.wield( item::spawn( "p3_sw_610", calendar::start_of_cataclysm ) ) );
        REQUIRE( !who.wielded_items().empty() );
        item &gun = *who.wielded_items().front();
        who.i_add( speedloader_holding( 6 ) );
        REQUIRE( gun.ammo_remaining() == 0 );

        avatar_action::reload_wielded();
        REQUIRE( who.activity );
        process_activity( who );
        CHECK( gun.ammo_remaining() == 6 );
    }

    // DDA 544-551 ("empty gun" / "speedloader with wrong ammo"): guard, nothing is started or loaded.
    SECTION( "a speedloader with the wrong ammo is not loaded" ) {
        REQUIRE( !who.wield( item::spawn( "p3_sw_610", calendar::start_of_cataclysm ) ) );
        REQUIRE( !who.wielded_items().empty() );
        item &gun = *who.wielded_items().front();
        detached_ptr<item> wrong = item::spawn( "40_speedloader6", calendar::start_of_cataclysm );
        wrong->force_insert_item( item::spawn( "9mm", calendar::start_of_cataclysm, 6 ),
                                  pocket_type::MAGAZINE );
        item &loader = who.i_add( std::move( wrong ) );
        REQUIRE( loader.ammo_remaining() == 6 );

        avatar_action::reload_wielded();
        CHECK_FALSE( who.activity );
        CHECK( gun.ammo_remaining() == 0 );
    }

    // DDA 555-594 ("full gun"): guard, a full gun is not reloadable, so reload_wielded starts nothing.
    SECTION( "a full gun takes no speedloader" ) {
        REQUIRE( !who.wield( item::spawn( "p3_sw_610", calendar::start_of_cataclysm ) ) );
        REQUIRE( !who.wielded_items().empty() );
        item &gun = *who.wielded_items().front();
        REQUIRE( gun.put_in( item::spawn( "40sw", calendar::start_of_cataclysm, 6 ),
                             pocket_type::MAGAZINE ).success() );
        who.i_add( speedloader_holding( 6 ) );
        REQUIRE( gun.ammo_remaining() == 6 );

        avatar_action::reload_wielded();
        CHECK_FALSE( who.activity );
        CHECK( gun.ammo_remaining() == 6 );
    }

    // [UNSURE] DDA 596-635 ("gun full of casings" / "partially empty speedloader"): the casings are ejected
    // first and the three rounds are loaded. The derivation of the load count (casings_handle inside
    // reload_targeted, then the pocket gate) is by reading only, and SELF-46 reports this DDA section passes
    // with the old body for a reason nobody traced. If a REQUIRE of this section fails (or it hangs), delete this
    // section only, with a log line in the report and a Blocked line; do not change the assertions.
    SECTION( "a gun full of casings takes a partly filled speedloader" ) {
        REQUIRE( !who.wield( item::spawn( "p3_sw_610", calendar::start_of_cataclysm ) ) );
        REQUIRE( !who.wielded_items().empty() );
        item &gun = *who.wielded_items().front();
        detached_ptr<item> casings = item::spawn( "40_casing", calendar::start_of_cataclysm, 6 );
        casings->set_flag( flag_CASING );
        gun.force_insert_item( std::move( casings ), pocket_type::MAGAZINE );
        who.i_add( speedloader_holding( 3 ) );

        avatar_action::reload_wielded();
        REQUIRE( who.activity );
        process_activity( who );
        CHECK( gun.ammo_remaining() == 3 );
    }
}

// CDF: P3-6-3b; root cause of the full-speedloader activity case. A pocket filled exactly to its
// ammo_restriction must survive overflow(): drop_invalid_inventory() runs it when an activity ends.
// p3_sw_610 holds 6 x 40sw (250 ml / 40 per stack => 6 rounds are 37.5 ml, item::volume() rounds up to 38 ml).
TEST_CASE( "full_ammo_restricted_magazine_pocket_survives_overflow", "[p3][reload][entry][6-3b]" )
{
    clear_all_state();
    avatar &who = get_avatar();
    who.wear_item( item::spawn( "backpack", calendar::start_of_cataclysm ), false );

    item &gun = who.i_add( item::spawn( "p3_sw_610", calendar::start_of_cataclysm ) );
    detached_ptr<item> rounds = item::spawn( "40sw", calendar::start_of_cataclysm, 6 );
    const units::volume stack_volume = rounds->volume();
    REQUIRE( gun.put_in( std::move( rounds ), pocket_type::MAGAZINE ).success() );
    REQUIRE( gun.ammo_remaining() == 6 );

    std::vector<item_pocket *> pockets = gun.get_contents().get_pockets( pocket_type::MAGAZINE );
    REQUIRE( pockets.size() == 1 );
    CHECK( pockets[0]->volume_capacity() >= stack_volume );

    gun.overflow( who.bub_pos(), &who );
    CHECK( gun.ammo_remaining() == 6 );
}

// ---------------------------------------------------------------------------
// CDF: P3-6-3b; the reload entry points take the DDA picker, sort and turret rule (DDA game.cpp:7142-7259).
// ---------------------------------------------------------------------------

namespace
{

auto picker_shows( const item &it ) -> bool
{
    return avatar_action::reload_item_picker_shows( it );
}

auto picker_set( avatar &who ) -> std::vector<const item *>
{
    std::vector<const item *> shown;
    for( const item *it : who.all_items() ) {
        if( picker_shows( *it ) ) {
            shown.push_back( it );
        }
    }
    return shown;
}

// Calls reload_weapon( false ) until `done` holds, finishing each activity it starts. Every call must start an
// activity, so a sort that reloads some other item first shows up in the caller's checks on the other items.
void reload_weapon_until( avatar &who, const std::function<bool()> &done )
{
    for( int i = 0; i < 20 && !done(); ++i ) {
        avatar_action::reload_weapon( false );
        REQUIRE( who.activity );
        process_activity( who );
    }
    REQUIRE( done() );
}

} // namespace

// CDF: P3-6-3b P1; DDA game.cpp:7142-7174 (reload_selector_preset: rate_action_reload == good).
TEST_CASE( "reload_item_picker_lists_reloadable_items_only", "[p3][reload][entry][6-3b]" )
{
    clear_all_state();
    avatar &who = get_avatar();
    const time_point bday = calendar::start_of_cataclysm;
    who.wear_item( item::spawn( "backpack", bday ), false );

    // CDF: sw_619 (data/json/items/gun/38.json) keeps direct charges, which item::is_reloadable always counts as
    // reloadable (item_gun_tool_ammo.cpp:90), so the empty gun is a sw_619 and the full one a glock_19 holding a
    // full glockmag (glock_19 has a magazine well, so a full magazine makes it not reloadable)
    item &empty_gun = who.i_add( item::spawn( "sw_619", bday, 0 ) );
    detached_ptr<item> full_mag = item::spawn( "glockmag", bday, 0 );
    REQUIRE( full_mag->put_in( item::spawn( "9mm", bday, 15 ), pocket_type::MAGAZINE ).success() );
    detached_ptr<item> full_det = item::spawn( "glock_19", bday, 0 );
    full_det->put_in( std::move( full_mag ) );
    item &full_gun = who.i_add( std::move( full_det ) );
    item &ammo = who.i_add( item::spawn( "38_special", bday, 6 ) );
    // CDF: knife_combat (swords_and_blades.json) is not reloadable
    item &knife = who.i_add( item::spawn( "knife_combat", bday ) );
    // CDF: belt223 (magazine/223.json) is an ammo belt with linkage ammolink223 and the avatar carries no link,
    // so rate_action_reload says iffy (a reloadable item that can_reload refuses)
    item &belt = who.i_add( item::spawn( "belt223", bday ) );
    REQUIRE( empty_gun.is_reloadable() );
    REQUIRE_FALSE( full_gun.is_reloadable() );
    REQUIRE_FALSE( ammo.is_reloadable() );
    REQUIRE_FALSE( knife.is_reloadable() );
    REQUIRE( belt.is_reloadable() );
    REQUIRE_FALSE( who.can_reload( belt ) );

    const std::vector<const item *> shown = picker_set( who );
    CHECK( shown == std::vector<const item *> { &empty_gun } );
    CHECK_FALSE( picker_shows( belt ) );
}

// CDF: P3-6-3b P2; DDA item_location.cpp:1181-1191 (is_invisible_installed_gunmod) and game.cpp:7151-7153.
TEST_CASE( "installed_gunmods_are_not_separate_picker_rows", "[p3][reload][entry][6-3b]" )
{
    clear_all_state();
    avatar &who = get_avatar();
    const time_point bday = calendar::start_of_cataclysm;
    who.wear_item( item::spawn( "backpack", bday ), false );

    // CDF: ar15 (gun/223.json) takes the underslung shotgun u_shotgun (gunmod/underbarrel.json, 2-round clip)
    detached_ptr<item> rifle = item::spawn( "ar15", bday, 0 );
    REQUIRE( rifle->put_in( item::spawn( "u_shotgun", bday, 0 ), pocket_type::MOD ).success() );
    REQUIRE( rifle->gunmods().size() == 1 );
    item &gun = who.i_add( std::move( rifle ) );
    item &mod = *gun.gunmods().front();
    REQUIRE( mod.is_gunmod() );
    REQUIRE( mod.is_reloadable() );
    REQUIRE( mod.parent_item() == &gun );

    CHECK( picker_shows( gun ) );
    CHECK_FALSE( picker_shows( mod ) );
}


// CDF: P3-5-4 P7 (the 6-3b draft's P7); DDA item_location.cpp:1181-1191 and game.cpp:7151-7153. DDA keeps
// mounted_flashlight and mipim visible; CDF has neither item (git grep of data/ is empty), so the TEST_DATA
// fixture test_visible_underslung_shotgun (u_shotgun with is_visible_when_installed) stands in.
TEST_CASE( "installed_gunmods_with_visible_flag_are_picker_rows", "[p3][reload][entry][5-4]" )
{
    clear_all_state();
    avatar &who = get_avatar();
    const time_point bday = calendar::start_of_cataclysm;
    who.wear_item( item::spawn( "backpack", bday ), false );

    // CDF: ar15 (gun/223.json) takes underbarrel mods (gunmod/underbarrel.json:623)
    detached_ptr<item> rifle = item::spawn( "ar15", bday, 0 );
    REQUIRE( rifle->put_in( item::spawn( "test_visible_underslung_shotgun", bday, 0 ),
                            pocket_type::MOD ).success() );
    item &gun = who.i_add( std::move( rifle ) );
    REQUIRE( gun.gunmods().size() == 1 );
    item &mod = *gun.gunmods().front();
    REQUIRE( mod.typeId() == itype_id( "test_visible_underslung_shotgun" ) );
    REQUIRE( mod.parent_item() == &gun );
    REQUIRE( mod.is_reloadable() );

    CHECK( mod.type->gunmod->is_visible_when_installed );
    CHECK_FALSE( item_funcs::is_invisible_installed_gunmod( mod ) );
    CHECK( picker_shows( gun ) );
    CHECK( picker_shows( mod ) );

    // a mod outside a gun is no installed mod, and the plain u_shotgun keeps the DDA rule
    item &loose = who.i_add( item::spawn( "u_shotgun", bday, 0 ) );
    CHECK_FALSE( item_funcs::is_invisible_installed_gunmod( loose ) );
    detached_ptr<item> plain_rifle = item::spawn( "ar15", bday, 0 );
    REQUIRE( plain_rifle->put_in( item::spawn( "u_shotgun", bday, 0 ), pocket_type::MOD ).success() );
    item &plain_gun = who.i_add( std::move( plain_rifle ) );
    REQUIRE( plain_gun.gunmods().size() == 1 );
    CHECK( item_funcs::is_invisible_installed_gunmod( *plain_gun.gunmods().front() ) );
    CHECK_FALSE( picker_shows( *plain_gun.gunmods().front() ) );
}

// CDF: P3-6-3b P3; DDA game.cpp:7186-7229 (the sort of reload_weapon).
TEST_CASE( "reload_weapon_prefers_wielded_then_compatible_then_gun_then_speed", "[p3][reload][entry][6-3b]" )
{
    clear_all_state();
    avatar &who = get_avatar();
    const time_point bday = calendar::start_of_cataclysm;
    who.wear_item( item::spawn( "backpack", bday ), false );

    SECTION( "wielded gun, compatible magazine, other gun, incompatible magazine" ) {
        // CDF: glock_19 takes glockmag (item::is_compatible, probed: it accepts glockmag and not stanag30 or
        // sw_619), glockmag takes 9mm, sw_619 takes 38_special, stanag30 takes 556; knife_combat is not reloadable
        detached_ptr<item> inserted_det = item::spawn( "glockmag", bday, 0 );
        REQUIRE( inserted_det->put_in( item::spawn( "9mm", bday, 3 ), pocket_type::MAGAZINE ).success() );
        detached_ptr<item> glock_det = item::spawn( "glock_19", bday, 0 );
        glock_det->put_in( std::move( inserted_det ) );
        REQUIRE( !who.wield( std::move( glock_det ) ) );
        REQUIRE( !who.wielded_items().empty() );
        item &wielded = *who.wielded_items().front();
        REQUIRE( wielded.magazine_current() != nullptr );
        item &spare = who.i_add( item::spawn( "glockmag", bday, 0 ) );
        item &other = who.i_add( item::spawn( "sw_619", bday, 0 ) );
        item &foreign = who.i_add( item::spawn( "stanag30", bday, 0 ) );
        item &knife = who.i_add( item::spawn( "knife_combat", bday ) );
        who.i_add( item::spawn( "9mm", bday, 100 ) );
        who.i_add( item::spawn( "38_special", bday, 100 ) );
        who.i_add( item::spawn( "556", bday, 100 ) );
        REQUIRE( wielded.is_compatible( spare ).success() );
        REQUIRE_FALSE( wielded.is_compatible( other ).success() );
        REQUIRE_FALSE( wielded.is_compatible( foreign ).success() );
        REQUIRE_FALSE( knife.is_reloadable() );

        reload_weapon_until( who, [&wielded]() {
            return wielded.ammo_remaining() == wielded.ammo_capacity();
        } );
        CHECK( spare.ammo_remaining() == 0 );
        CHECK( other.ammo_remaining() == 0 );
        CHECK( foreign.ammo_remaining() == 0 );

        reload_weapon_until( who, [&spare]() {
            return spare.ammo_remaining() == spare.ammo_capacity();
        } );
        CHECK( other.ammo_remaining() == 0 );
        CHECK( foreign.ammo_remaining() == 0 );

        reload_weapon_until( who, [&other]() {
            return other.ammo_remaining() == other.ammo_capacity();
        } );
        CHECK( foreign.ammo_remaining() == 0 );

        reload_weapon_until( who, [&foreign]() {
            return foreign.ammo_remaining() == foreign.ammo_capacity();
        } );
    }

    SECTION( "a gun with the smaller reload key is reloaded first (DDA: reload time x remaining capacity)" ) {
        // CDF: nothing wielded, so the affiliation key is skipped. The empty sw_619 (38.json, MAGAZINE pocket, 7
        // rounds) has remaining_ammo_capacity() 7 under the pocket shape (7ENDC; 0 under the old direct-charges
        // shape), where capacity minus loaded gives 7 and 70 * 7 = 490. The glock_19 holding 12 of 15 rounds has 3 left either way, 100 * 3 = 300. So
        // the DDA key puts the empty revolver first and the capacity-minus-loaded key puts the glock first.
        // CDF: 7-R2a; the ported sw_619 has an integral MAGAZINE pocket (remaining_ammo_capacity 7).
        // CDF: 7ENDC; the empty gun is now the pocket shape (empty pocket, remaining_ammo_capacity 7, was 0
        // under the removed direct-charges shape). The old premise (capacity 0 for an empty legacy gun) is gone
        // with the legacy path, and the order now follows DDA's key: the partial glock (300) goes before the
        // empty revolver (490).
        item &empty_gun = who.i_add( item::spawn( "legacy_sw_619", bday, 0 ) );
        detached_ptr<item> mag_det = item::spawn( "glockmag", bday, 0 );
        REQUIRE( mag_det->put_in( item::spawn( "9mm", bday, 12 ), pocket_type::MAGAZINE ).success() );
        detached_ptr<item> glock_det = item::spawn( "glock_19", bday, 0 );
        glock_det->put_in( std::move( mag_det ) );
        item &partial_gun = who.i_add( std::move( glock_det ) );
        who.i_add( item::spawn( "38_special", bday, 100 ) );
        who.i_add( item::spawn( "9mm", bday, 100 ) );
        REQUIRE( partial_gun.ammo_remaining() == 12 );
        REQUIRE( empty_gun.remaining_ammo_capacity() == 7 ); // CDF: 7ENDC, was 0 (direct-charges shape)
        REQUIRE( partial_gun.remaining_ammo_capacity() == 3 );
        REQUIRE( empty_gun.get_reload_time() * ( empty_gun.ammo_capacity() - empty_gun.ammo_remaining() ) >
                 partial_gun.get_reload_time() * partial_gun.remaining_ammo_capacity() );

        avatar_action::reload_weapon( false );
        REQUIRE( who.activity );
        process_activity( who );

        CHECK( partial_gun.ammo_remaining() == 15 );
        CHECK( empty_gun.ammo_remaining() == 0 );
    }
}

// CDF: P3-6-3b P4; DDA game.cpp:7200-7202 and :7231-7233 (non guns and non magazines are sorted last and skipped).
TEST_CASE( "reload_weapon_skips_non_guns_and_non_magazines", "[p3][reload][entry][6-3b]" )
{
    clear_all_state();
    avatar &who = get_avatar();
    const time_point bday = calendar::start_of_cataclysm;
    who.wear_item( item::spawn( "backpack", bday ), false );

    // CDF: bottle_plastic (containers.json) is an empty watertight bottle and jug_plastic holding water_clean is
    // its liquid source (the fixture of reload_liquid_container above); neither is a gun or a magazine
    item &bottle = who.i_add( item::spawn( "bottle_plastic", bday ) );
    item &jug = who.i_add( item::spawn( "jug_plastic", bday ) );
    REQUIRE( jug.put_in( item::spawn( "water_clean", calendar::turn_zero, 2 ), pocket_type::CONTAINER ).success() );
    REQUIRE( bottle.is_reloadable() );
    REQUIRE_FALSE( bottle.is_gun() );
    REQUIRE_FALSE( bottle.is_magazine() );
    std::vector<item_reload_option> bottle_ammo;
    REQUIRE( who.list_ammo( bottle, bottle_ammo, false ) );
    REQUIRE_FALSE( bottle_ammo.empty() );

    SECTION( "a gun with ammo goes first and the bottle is left alone" ) {
        item &gun = who.i_add( item::spawn( "sw_619", bday, 0 ) );
        who.i_add( item::spawn( "38_special", bday, 100 ) );

        reload_weapon_until( who, [&gun]() {
            return gun.ammo_remaining() == gun.ammo_capacity();
        } );

        CHECK( bottle.get_contents_volume() == units::volume() );
        CHECK( jug.get_contents_volume() > units::volume() );
    }

    SECTION( "nothing else to reload leaves the bottle alone" ) {
        // the gun has no ammo to take, so the bottle is the only candidate that has a source
        who.i_add( item::spawn( "sw_619", bday, 0 ) );

        avatar_action::reload_weapon( false );

        CHECK_FALSE( who.activity );
        CHECK( bottle.get_contents_volume() == units::volume() );
    }
}

// CDF: P3-6-3b P5; DDA game.cpp:7231-7238 (u.list_ammo( candidate, ammo_list, false ), then !ammo_list.empty()).
TEST_CASE( "reload_weapon_uses_list_ammo", "[p3][reload][entry][6-3b]" )
{
    clear_all_state();
    avatar &who = get_avatar();
    const time_point bday = calendar::start_of_cataclysm;
    who.wear_item( item::spawn( "backpack", bday ), false );

    // CDF: test_multimag_gun (TEST_DATA/items.json) has two wells, glockmag (9mm) and stanag30 (223 ammo type,
    // item 556); the first well holds a full glockmag
    detached_ptr<item> glockmag_det = item::spawn( "glockmag", bday, 0 );
    REQUIRE( glockmag_det->put_in( item::spawn( "9mm", bday, 15 ), pocket_type::MAGAZINE ).success() );
    item &glockmag = *glockmag_det;
    detached_ptr<item> gun_det = item::spawn( "test_multimag_gun", bday, 0 );
    REQUIRE( gun_det->put_in( std::move( glockmag_det ), pocket_type::MAGAZINE_WELL ).success() );
    item &gun = who.i_add( std::move( gun_det ) );
    REQUIRE( glockmag.ammo_remaining() == glockmag.ammo_capacity() );
    REQUIRE( gun.is_reloadable() );

    SECTION( "a loaded magazine for the empty second well makes the gun a candidate" ) {
        detached_ptr<item> stanag_det = item::spawn( "stanag30", bday, 0 );
        REQUIRE( stanag_det->put_in( item::spawn( "556", bday, 30 ), pocket_type::MAGAZINE ).success() );
        item &stanag = who.i_add( std::move( stanag_det ) );
        std::vector<item_reload_option> list;
        REQUIRE( who.list_ammo( gun, list, false ) );
        REQUIRE_FALSE( list.empty() );

        avatar_action::reload_weapon( false );

        // CDF: the candidate is picked and the reload is queued for the gun with that magazine as the source.
        // What the actor then loads is not this packet's: with the first well full the actor adds nothing
        // (probed; DDA TODO(multimag), the aggregate check is first-well only).
        REQUIRE( who.activity );
        const auto *actor = dynamic_cast<const reload_activity_actor *>( who.activity->get_actor() );
        REQUIRE( actor );
        CHECK( actor->target() == &gun );
        CHECK( actor->ammo() == &stanag );
        process_activity( who );
        CHECK( glockmag.ammo_remaining() == glockmag.ammo_capacity() );
    }

    SECTION( "an empty magazine is not ammo for the second well" ) {
        who.i_add( item::spawn( "stanag30", bday, 0 ) );
        std::vector<item_reload_option> list;
        who.list_ammo( gun, list, false );
        REQUIRE( list.empty() );

        avatar_action::reload_weapon( false );

        CHECK_FALSE( who.activity );
    }

    SECTION( "a candidate with only an empty magazine to offer does not stop the next one" ) {
        // CDF: reload_weapon starts reload( candidate, false, false ) for the first candidate whose list is not
        // empty and returns, so a list that counted the empty stanag30 would end the search at the wielded gun
        // (which then loads nothing) before the revolver is tried. sw_619 takes 38_special (38.json).
        REQUIRE( !who.wield( who.inv_remove_item( &gun ) ) );
        who.i_add( item::spawn( "stanag30", bday, 0 ) );
        item &revolver = who.i_add( item::spawn( "sw_619", bday, 0 ) );
        who.i_add( item::spawn( "38_special", bday, 100 ) );
        std::vector<item_reload_option> list;
        who.list_ammo( gun, list, false );
        REQUIRE( list.empty() );
        list.clear();
        REQUIRE( who.list_ammo( gun, list, true ) );
        REQUIRE_FALSE( list.empty() );

        avatar_action::reload_weapon( false );

        REQUIRE( who.activity );
        const auto *actor = dynamic_cast<const reload_activity_actor *>( who.activity->get_actor() );
        REQUIRE( actor );
        CHECK( actor->target() == &revolver );
        process_activity( who );
    }
}

// CDF: P3-6-3b P6; DDA game.cpp:7241-7258 (a vehicle part at the player position always returns).
TEST_CASE( "reload_weapon_on_vehicle_without_turret_does_not_open_the_picker", "[p3][reload][entry][6-3b]" )
{
    clear_all_state();
    avatar &who = get_avatar();
    map &here = get_map();
    // CDF: test_turret_rig (TEST_DATA/vehicle.json) is a frame with a turret_mount and a storage_battery: no turret
    vehicle *veh = here.add_vehicle( vproto_id( "test_turret_rig" ), tripoint_bub_ms( 65, 65, 0 ), 0_degrees, 0,
                                     0 );
    REQUIRE( veh );
    who.setpos( veh->bub_part_location( 0 ) );
    REQUIRE( here.veh_at( who.bub_pos() ) );
    REQUIRE( who.all_items().empty() );

    const int opened = avatar_action::reload_item_picker_opened_count();
    avatar_action::reload_weapon( true );
    CHECK_FALSE( who.activity );
    CHECK( avatar_action::reload_item_picker_opened_count() == opened );

    // the same call off the vehicle reaches the picker (which has nothing to list and returns)
    who.setpos( tripoint_bub_ms( 60, 60, 0 ) );
    REQUIRE_FALSE( here.veh_at( who.bub_pos() ) );
    avatar_action::reload_weapon( true );
    CHECK_FALSE( who.activity );
    CHECK( avatar_action::reload_item_picker_opened_count() == opened + 1 );
}
