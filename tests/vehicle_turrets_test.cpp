#include "../src/map/map.h"
#include "../src/vehicle/vehicle_part.h"
#include "ammo.h"
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "creature_functions.h"
#include "faction.h"
#include "game.h"
#include "item.h"
#include "itype.h"
#include "map_helpers.h"
#include "monster.h"
#include "npc.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "string_id.h"
#include "type_id.h"
#include "units.h"
#include "value_ptr.h"
#include "vehicle/veh_type.h"
#include "vehicle/vehicle.h"
#include "vehicle/vehicle_functions.h"

#include <algorithm>
#include <map>
#include <ranges>
#include <utility>
#include <vector>

static auto turret_types() -> std::vector<const vpart_info*> {
    std::vector<const vpart_info*> res;

    for (const auto& vp : vpart_info::get_all()) {
        if (vp.has_flag("TURRET")) { res.push_back(&vp); }
    }

    return res;
}

static auto biggest_tank(const itype_id& ammo) -> const vpart_info* {
    std::vector<const vpart_info*> res;

    for (const auto& vp : vpart_info::get_all()) {
        if (item::spawn_temporary(vp.item)->can_reload_with(ammo)) { res.push_back(&vp); }
    }

    if (res.empty()) { return nullptr; }

    return *std::ranges::max_element(res, {}, &vpart_info::size);
}

static auto update_player_visibility_cache(map& here, const tripoint_bub_ms& player_pos) -> void {
    here.invalidate_map_cache(player_pos.z());
    here.build_map_cache(player_pos.z());
    here.update_visibility_cache(player_pos.z());
}

TEST_CASE("vehicle_turret", "[vehicle][gun][magazine][.]") {
    clear_all_state();
    map& here = get_map();
    avatar& player_character = get_avatar();
    for (auto e : turret_types()) {
        SECTION(e->name()) {
            vehicle* veh =
                here.add_vehicle(vproto_id("none"), tripoint_bub_ms(65, 65, 0), 270_degrees, 0, 0);
            REQUIRE(veh);

            const int idx = veh->install_part(tripoint_mnt_veh::zero(), e->get_id(), true);
            REQUIRE(idx >= 0);

            REQUIRE(veh->install_part(tripoint_mnt_veh::zero(), vpart_id("storage_battery"), true)
                    >= 0);
            veh->charge_battery(10000);

            auto& gun = veh->part(idx).get_base();
            const auto ammo = gun.ammo_default();

            if (veh->part_flag(idx, "USE_TANKS")) {
                auto* tank = biggest_tank(ammo);
                REQUIRE(tank);
                INFO(tank->get_id().str());

                auto tank_idx = veh->install_part(tripoint_mnt_veh::zero(), tank->get_id(), true);
                REQUIRE(tank_idx >= 0);
                REQUIRE(veh->part(tank_idx).ammo_set(ammo));

            } else if (!ammo.is_null()) {
                veh->part(idx).ammo_set(ammo);
            }

            auto qry = veh->turret_query(veh->part(idx));
            REQUIRE(qry);
            REQUIRE(qry.query() == turret_data::status::ready);
            REQUIRE(qry.range() > 0);

            player_character.setpos(veh->bub_part_location(idx));
            REQUIRE(
                qry.fire(player_character, player_character.abs_pos() + point_rel_ms(qry.range(), 0))
                > 0);

            here.destroy_vehicle(veh);
        }
    }
}

TEST_CASE("vehicle_turret_autoloader_integral_magazine", "[vehicle][gun][turret][autoload]") {
    clear_all_state();
    map& here = get_map();
    vehicle* veh =
        here.add_vehicle(vproto_id("none"), tripoint_bub_ms(65, 65, 0), 270_degrees, 0, 0);
    REQUIRE(veh);

    const auto turret_part_id = vpart_id("mounted_rebar_rifle");
    const auto autoloader_part_id = vpart_id("turret_autoloader");
    const auto cargo_part_id = vpart_id("box");
    const auto battery_part_id = vpart_id("storage_battery");

    const auto turret_index = veh->install_part(tripoint_mnt_veh::zero(), turret_part_id, true);
    REQUIRE(turret_index >= 0);
    REQUIRE(veh->install_part(tripoint_mnt_veh::zero(), autoloader_part_id, true) >= 0);
    const auto cargo_index = veh->install_part(tripoint_mnt_veh::zero(), cargo_part_id, true);
    REQUIRE(cargo_index >= 0);
    REQUIRE(veh->install_part(tripoint_mnt_veh::zero(), battery_part_id, true) >= 0);
    veh->charge_battery(10000);

    vehicle_part& turret_part = veh->part(turret_index);
    item& gun = turret_part.get_base();
    REQUIRE(gun.magazine_integral());
    const auto ammo_capacity = gun.ammo_capacity();
    REQUIRE(ammo_capacity > 1);

    const auto ammo_id = itype_id("rebar_rail");
    gun.ammo_set(ammo_id, 0);
    const auto ammo_stack = ammo_capacity * 2;
    auto remaining = veh->add_item(cargo_index, item::spawn(ammo_id, calendar::turn, ammo_stack));
    REQUIRE(!remaining);

    auto current_ammo = gun.ammo_remaining();
    auto last_ammo = current_ammo;
    auto tries = 0;
    const auto max_tries = ammo_capacity * 2;
    while (current_ammo < ammo_capacity && tries < max_tries) {
        calendar::turn += 1_minutes;
        vehicle_funcs::try_autoload_turret(*veh, turret_part);
        current_ammo = gun.ammo_remaining();
        if (current_ammo > last_ammo) {
            last_ammo = current_ammo;
            tries = 0;
        } else {
            ++tries;
        }
    }
    REQUIRE(gun.ammo_remaining() == ammo_capacity);
}

TEST_CASE("vehicle_turret_iff_protects_followers_in_line_of_fire", "[vehicle][turret][npc][iff]") {
    clear_all_state();
    build_test_map(ter_id("t_dirt"));
    map& here = get_map();
    set_time(calendar::turn_zero + 12_hours);

    const auto shooter_pos = tripoint_bub_ms(60, 60, 0);
    avatar& shooter = get_avatar();
    shooter.setpos(shooter_pos);
    shooter.set_body();

    const auto follower_pos = shooter_pos + point(3, 0);
    npc& follower = spawn_npc(follower_pos, "thug");
    follower.set_fac(faction_id("your_followers"));
    follower.set_attitude(NPCATT_FOLLOW);
    REQUIRE(follower.is_player_ally());
    REQUIRE(shooter.attitude_to(follower) == Attitude::A_FRIENDLY);

    const auto hostile_pos = shooter_pos + point(8, 0);
    monster& hostile = spawn_test_monster("mon_zombie_tough", hostile_pos);
    update_player_visibility_cache(here, shooter_pos);
    REQUIRE(shooter.sees(hostile));

    const auto target = creature_functions::
        auto_find_hostile_target(shooter, {.range = 20, .trail = false, .area = 0});
    REQUIRE_FALSE(target.has_value());
    CHECK(target.error() == 1);
}

TEST_CASE("vehicle_turret_iff_allows_clear_shots", "[vehicle][turret][npc][iff]") {
    clear_all_state();
    build_test_map(ter_id("t_dirt"));
    map& here = get_map();
    set_time(calendar::turn_zero + 12_hours);

    const auto shooter_pos = tripoint_bub_ms(60, 60, 0);
    avatar& shooter = get_avatar();
    shooter.setpos(shooter_pos);
    shooter.set_body();

    const auto follower_pos = shooter_pos + point(0, 5);
    npc& follower = spawn_npc(follower_pos, "thug");
    follower.set_fac(faction_id("your_followers"));
    follower.set_attitude(NPCATT_FOLLOW);
    REQUIRE(follower.is_player_ally());

    const auto hostile_pos = shooter_pos + point(8, 0);
    monster& hostile = spawn_test_monster("mon_zombie_tough", hostile_pos);
    update_player_visibility_cache(here, shooter_pos);
    REQUIRE(shooter.sees(hostile));

    const auto target = creature_functions::
        auto_find_hostile_target(shooter, {.range = 20, .trail = false, .area = 0});
    REQUIRE(target.has_value());
    CHECK(&target->get() == &hostile);
}

// CDF: P3-3-8d (D13). Fixture for the two autoloader guard tests: a turret whose gun has an
// unrestricted MAGAZINE_WELL (TEST_DATA p3_autoload_turret), an autoloader, a cargo box and power.
namespace
{
struct p3_autoload_fixture {
    vehicle *veh = nullptr;
    int turret_index = -1;
    int cargo_index = -1;

    p3_autoload_fixture() {
        veh = get_map().add_vehicle(vproto_id("none"), tripoint_bub_ms(65, 65, 0), 270_degrees, 0, 0);
        REQUIRE(veh);
        turret_index =
            veh->install_part(tripoint_mnt_veh::zero(), vpart_id("p3_autoload_turret"), true);
        REQUIRE(turret_index >= 0);
        REQUIRE(
            veh->install_part(tripoint_mnt_veh::zero(), vpart_id("turret_autoloader"), true) >= 0);
        cargo_index = veh->install_part(tripoint_mnt_veh::zero(), vpart_id("box"), true);
        REQUIRE(cargo_index >= 0);
        REQUIRE(veh->install_part(tripoint_mnt_veh::zero(), vpart_id("storage_battery"), true) >= 0);
        veh->charge_battery(10000);
        REQUIRE_FALSE(gun().magazine_integral());
        REQUIRE(gun().magazine_current() == nullptr);
    }

    auto gun() -> item & { return veh->part(turret_index).get_base(); }

    // The first call only starts a cycle; later calls finish it once the interval has passed.
    auto run_autoloader() -> void {
        for (int i = 0; i < 20; ++i) {
            calendar::turn += 1_minutes;
            vehicle_funcs::try_autoload_turret(*veh, veh->part(turret_index));
        }
    }

    auto cargo_count() -> int {
        return static_cast<int>(veh->part(cargo_index).get_items().size());
    }
};
} // namespace

TEST_CASE("autoloader_ignores_a_pocket_gun_in_cargo", "[vehicle][turret][autoload][p3]") {
    clear_all_state();
    p3_autoload_fixture f;

    // NEGATIVE fixture: a loaded gun with an integral MAGAZINE pocket (is_magazine() is true) and
    // nothing else in the cargo hold. The turret well has no item restriction.
    detached_ptr<item> cargo_gun = item::spawn(itype_id("test_multimag_gun_integral_ammo"));
    cargo_gun->ammo_set(itype_id("9mm"), 5);
    REQUIRE(cargo_gun->is_gun());
    REQUIRE(cargo_gun->is_magazine());
    REQUIRE(cargo_gun->ammo_remaining() > 0);
    REQUIRE(!f.veh->add_item(f.cargo_index, std::move(cargo_gun)));
    REQUIRE(f.cargo_count() == 1);

    f.run_autoloader();

    CHECK(f.gun().magazine_current() == nullptr);
    REQUIRE(f.cargo_count() == 1);
    CHECK(f.veh->part(f.cargo_index).get_items().front()->typeId() ==
          itype_id("test_multimag_gun_integral_ammo"));
}

TEST_CASE("autoloader_still_takes_a_real_magazine", "[vehicle][turret][autoload][p3]") {
    clear_all_state();
    p3_autoload_fixture f;

    // Positive control: a loaded glockmag (a real magazine, not a gun or a tool) is taken.
    detached_ptr<item> mag = item::spawn(itype_id("glockmag"));
    mag->ammo_set(itype_id("9mm"), 5);
    REQUIRE(mag->is_magazine());
    REQUIRE_FALSE(mag->is_gun());
    REQUIRE_FALSE(mag->is_tool());
    REQUIRE(mag->ammo_remaining() == 5);
    REQUIRE(!f.veh->add_item(f.cargo_index, std::move(mag)));
    // preconditions the autoloader checks before it looks at the cargo
    REQUIRE_FALSE(f.gun().ammo_sufficient(nullptr));
    REQUIRE(f.gun().can_reload_with(itype_id("glockmag")));

    f.run_autoloader();

    REQUIRE(f.gun().magazine_current() != nullptr);
    CHECK(f.gun().magazine_current()->typeId() == itype_id("glockmag"));
    CHECK(f.gun().magazine_current()->ammo_remaining() == 5);
    CHECK(f.cargo_count() == 0);
}
