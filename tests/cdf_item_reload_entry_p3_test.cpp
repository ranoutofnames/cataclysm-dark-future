#include "avatar.h"
#include "avatar_action.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "flag.h"
#include "item.h"
#include "map/map.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "type_id.h"

#include <algorithm>
#include <string>
#include <utility>

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
    REQUIRE(who.activity->targets.size() == 2);
    CHECK(&*who.activity->targets.back() == &ammo);
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
        REQUIRE(who.activity->targets.size() == 2);
        REQUIRE(!who.wielded_items().empty());
        CHECK(&*who.activity->targets.front() == who.wielded_items().front());
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
