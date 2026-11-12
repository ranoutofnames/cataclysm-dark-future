// CDF: REACTORPACK1. The minireactor vehicle part takes its fuel in a removable plutonium fuel
// pack seated in a MAGAZINE_WELL (item: data/json/items/vehicle/utilities.json), so the pack is a
// unit the player seats, swaps and removes with its charge intact.  The part still reports the
// vehicle fuel id plut_cell while a pack is seated (src/vehicle/vehicle_part.cpp ammo_current).
// Golden pin: tests/data/dda_staged/p3_golden.json entry "minireactor" (class WELL, rule R3,
// AUTHOR-KEEP-CDF-WELL; cite by id, the line moves with later packets).
#include "../src/map/field_type.h"
#include "../src/map/map.h"
#include "../src/vehicle/vehicle_part.h"
#include "ammo.h"
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "debug.h"
#include "flag.h"
#include "game.h"
#include "game_constants.h"
#include "item.h"
#include "item_group.h"
#include "itype.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "pocket_data.h"
#include "pocket_type.h"
#include "recipe.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"
#include "vehicle/veh_type.h"
#include "vehicle/vehicle.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

const itype_id itype_plut_cell("plut_cell");
const itype_id itype_plut_cell_charge("plut_cell_charge");
const itype_id itype_plut_pack_small("plut_pack_small");
const itype_id itype_plut_pack_large("plut_pack_large");
const itype_id itype_plut_pack_reactor("plut_pack_reactor");
const itype_id itype_heavy_cell("heavy_atomic_battery_cell");
const itype_id itype_minireactor("minireactor");
const itype_id itype_rm13_battery("rm13_battery");
const itype_id itype_light_cell("light_atomic_battery_cell");
const ammotype ammo_plutonium("plutonium");
const vproto_id vproto_reactor_test("reactor_test");
const item_group_id group_microlab_plutonium("microlab_storage_plutonium_special");

// The minireactor part of reactor_test (data/json/vehicles/test.json).
auto reactor_part(vehicle* veh) -> vehicle_part& {
    REQUIRE(veh != nullptr);
    REQUIRE(!veh->reactors.empty());
    return veh->part(veh->reactors.front());
}

// A whole cell stack lying in the open (not the reactor's own fuel pack).
auto loose_cell_stack(avatar& you) -> item& {
    const std::vector<item*> found = you.items_with([](const item& it) {
        return it.typeId() == itype_plut_cell &&
               (it.parent_item() == nullptr || !it.parent_item()->is_tool());
    });
    REQUIRE(!found.empty());
    return *found.front();
}

// Give a pack to the avatar and seat it in the reactor's well.
auto seat_pack(avatar& you, vehicle_part& reactor, const itype_id& pack_id, int charges) -> void {
    detached_ptr<item> pack = item::spawn(pack_id);
    if (charges > 0) {
        pack->ammo_set(itype_plut_cell_charge, charges);
    }
    item& held = you.i_add(std::move(pack));
    REQUIRE(reactor.insert_fuel_pack(you, held));
}

} // namespace

// K1
TEST_CASE("minireactor_takes_a_pack_in_a_well", "[vehicle][reactor][plut][reactorpack1][p3]") {
    clear_all_state();

    // CDF: data/json/items/vehicle/utilities.json minireactor after REACTORPACK1: one MAGAZINE_WELL
    // (rigid, item_restriction the three packs, default plut_pack_reactor), no integral MAGAZINE,
    // no max_charges (the TOOL keeps its spawn_unloaded behaviour, no initial_charges).
    const itype* minireactor = item::spawn_temporary(itype_minireactor)->type;

    int magazines = 0;
    int wells = 0;
    for (const pocket_data& pocket : minireactor->pockets) {
        if (pocket.type == pocket_type::MAGAZINE) {
            ++magazines;
        } else if (pocket.type == pocket_type::MAGAZINE_WELL) {
            ++wells;
            CHECK(pocket.rigid);
            CHECK(pocket.default_magazine == itype_plut_pack_reactor);
            const std::set<itype_id> got(pocket.item_id_restriction.begin(),
                                         pocket.item_id_restriction.end());
            const std::set<itype_id> expected = {itype_plut_pack_small, itype_plut_pack_large,
                                                 itype_plut_pack_reactor};
            CHECK(got == expected);
        }
    }
    CHECK(wells == 1);
    CHECK(magazines == 0);

    REQUIRE(minireactor->tool != nullptr);
    CHECK(minireactor->tool->max_charges == 0);

    // CDF: a crafted or found reactor item is unloaded: no pack, nothing to burn
    detached_ptr<item> spawned = item::spawn(itype_minireactor);
    CHECK(spawned->magazine_current() == nullptr);
    CHECK(spawned->ammo_remaining() == 0);
}

// K2
TEST_CASE("plut_pack_reactor_matches_the_heavy_cell",
          "[vehicle][reactor][plut][reactorpack1][p3]") {
    clear_all_state();

    // CDF: data/json/items/plutonium_packs.json plut_pack_reactor: capacity 10000 is the
    // minireactor's own capacity (utilities.json before the packet) and 20 cells of 500
    // (game_constants.h:80 PLUTONIUM_CHARGES).
    REQUIRE(itype_plut_pack_reactor.is_valid());
    const itype* pack = &*itype_plut_pack_reactor;
    REQUIRE(pack->magazine != nullptr);
    CHECK(pack->magazine->capacity == 10000);
    CHECK(pack->magazine->capacity == 20 * PLUTONIUM_CHARGES);
    CHECK(pack->magazine->default_ammo == itype_plut_cell_charge);

    // CDF: body numbers copied from heavy_atomic_battery_cell (data/json/items/battery.json)
    const itype* heavy = &*itype_heavy_cell;
    CHECK(pack->weight == heavy->weight);
    CHECK(pack->volume == heavy->volume);

    // CDF: flags of the PLUT1 packs (NO_SALVAGE, LEAK_DAM, RADIOACTIVE)
    item* spawned = item::spawn_temporary(itype_plut_pack_reactor);
    CHECK(spawned->has_flag(flag_RADIOACTIVE));
    CHECK(spawned->has_flag(flag_LEAK_DAM));
    CHECK(spawned->has_flag(flag_NO_SALVAGE));
}

// K3
TEST_CASE("spawned_reactor_holds_a_pack", "[vehicle][reactor][plut][reactorpack1][p3]") {
    clear_all_state();
    build_test_map(ter_id("t_pavement"));
    map& here = get_map();

    SECTION("a mint vehicle seats the default full pack") {
        // veh_fuel_mult 100 fills through vehicle_part::ammo_set (vehicle.cpp:835-837)
        vehicle* veh_ptr =
            here.add_vehicle(vproto_reactor_test, tripoint_bub_ms(10, 10, 0), 0_degrees, 100, 0);
        vehicle_part& reactor = reactor_part(veh_ptr);

        item* pack = reactor.get_base().magazine_current();
        REQUIRE(pack != nullptr);
        CHECK(pack->typeId() == itype_plut_pack_reactor);
        CHECK(pack->ammo_remaining() == 10000);
        // the pack stores PLUT1's charge item, not the cell
        CHECK(pack->ammo_current() == itype_plut_cell_charge);

        // CDF: power.json minireactor fuel_type plut_cell; the part reports it while loaded
        CHECK(reactor.ammo_current() == itype_plut_cell);
        CHECK(reactor.ammo_capacity() == 10000);
        CHECK(reactor.ammo_remaining() == 10000);

        // CDF: vehicle.cpp:7241-7249 fuels_left() keys on part.ammo_current()
        const std::map<itype_id, int> fuels = veh_ptr->fuels_left();
        REQUIRE(fuels.count(itype_plut_cell) == 1);
        CHECK(fuels.at(itype_plut_cell) == 10000);
        CHECK(fuels.count(itype_plut_cell_charge) == 0);
    }

    SECTION("an empty spawn has no pack") {
        vehicle* veh_ptr =
            here.add_vehicle(vproto_reactor_test, tripoint_bub_ms(10, 10, 0), 0_degrees, 0, 0);
        vehicle_part& reactor = reactor_part(veh_ptr);
        CHECK(reactor.get_base().magazine_current() == nullptr);
        CHECK(reactor.ammo_remaining() == 0);
    }
}

// K4
TEST_CASE("empty_well_reads_capacity_and_takes_packs",
          "[vehicle][reactor][plut][reactorpack1][p3]") {
    clear_all_state();
    build_test_map(ter_id("t_pavement"));
    map& here = get_map();
    avatar& you = get_avatar();
    clear_avatar();

    vehicle* veh_ptr =
        here.add_vehicle(vproto_reactor_test, tripoint_bub_ms(10, 10, 0), 0_degrees, 100, 0);
    vehicle_part& reactor = reactor_part(veh_ptr);
    detached_ptr<item> removed = reactor.remove_fuel_pack();
    REQUIRE(removed);
    REQUIRE(reactor.get_base().magazine_current() == nullptr);

    // CDF: ammo_capacity(true) of a well tool answers the default magazine's capacity
    // (src/item.cpp:9422-9443); the reactor keeps its 10000 with no pack
    CHECK(reactor.ammo_capacity() == 10000);
    // DDA vehicle_part.cpp:455-457: a reactor with no item is always refillable
    CHECK(reactor.can_reload());

    detached_ptr<item> small = item::spawn(itype_plut_pack_small);
    detached_ptr<item> large = item::spawn(itype_plut_pack_large);
    detached_ptr<item> heavy = item::spawn(itype_plut_pack_reactor);
    CHECK(reactor.can_reload(&*small));
    CHECK(reactor.can_reload(&*large));
    CHECK(reactor.can_reload(&*heavy));

    detached_ptr<item> light = item::spawn(itype_light_cell);
    detached_ptr<item> rm13 = item::spawn(itype_rm13_battery);
    CHECK_FALSE(reactor.can_reload(&*light));
    CHECK_FALSE(reactor.can_reload(&*rm13));

    // CDF: loose cells refill only into a seated pack (open point 3, AUTO-240)
    detached_ptr<item> cells = item::spawn(itype_plut_cell, calendar::turn, 1);
    CHECK_FALSE(reactor.can_reload(&*cells));
}

// K5
TEST_CASE("a_full_pack_can_be_swapped", "[vehicle][reactor][plut][reactorpack1][p3]") {
    clear_all_state();
    build_test_map(ter_id("t_pavement"));
    map& here = get_map();
    avatar& you = get_avatar();
    clear_avatar();

    vehicle* veh_ptr =
        here.add_vehicle(vproto_reactor_test, tripoint_bub_ms(10, 10, 0), 0_degrees, 100, 0);
    vehicle_part& reactor = reactor_part(veh_ptr);
    REQUIRE(reactor.ammo_remaining() == 10000);

    detached_ptr<item> small = item::spawn(itype_plut_pack_small);
    small->ammo_set(itype_plut_cell_charge, 700);
    item& held = you.i_add(std::move(small));
    REQUIRE(reactor.can_reload(&held));

    // CDF: reload_targeted refuses a swap while the seated magazine is full
    // (src/item_pocket.cpp:2021-2028), so insert_fuel_pack ejects it by hand first
    REQUIRE(reactor.insert_fuel_pack(you, held));

    CHECK(reactor.ammo_remaining() == 700);
    item* pack = reactor.get_base().magazine_current();
    REQUIRE(pack != nullptr);
    CHECK(pack->typeId() == itype_plut_pack_small);

    // the old pack comes back to the carrier whole, charge intact
    const std::vector<item*> old = you.items_with([](const item& it) {
        return it.typeId() == itype_plut_pack_reactor;
    });
    REQUIRE(old.size() == 1);
    CHECK(old.front()->ammo_remaining() == 10000);
}

// K6
TEST_CASE("cells_load_into_the_seated_pack", "[vehicle][reactor][plut][reactorpack1][p3]") {
    clear_all_state();
    build_test_map(ter_id("t_pavement"));
    map& here = get_map();
    avatar& you = get_avatar();
    clear_avatar();

    vehicle* veh_ptr =
        here.add_vehicle(vproto_reactor_test, tripoint_bub_ms(10, 10, 0), 0_degrees, 0, 0);
    vehicle_part& reactor = reactor_part(veh_ptr);

    SECTION("three cells give 1500 charges from a partly filled pack") {
        seat_pack(you, reactor, itype_plut_pack_reactor, 1000);
        REQUIRE(reactor.ammo_remaining() == 1000);

        // CDF: game_constants.h:80 PLUTONIUM_CHARGES 500; 3 * 500 = 1500
        you.i_add(item::spawn(itype_plut_cell, calendar::turn, 3));
        REQUIRE(you.charges_of(itype_plut_cell) == 3);

        // the call case 'f' makes for cells into a seated pack (veh_interact.cpp)
        item& pack = *reactor.get_base().magazine_current();
        REQUIRE(pack.reload(you, loose_cell_stack(you), 3));

        CHECK(reactor.ammo_remaining() == 2500);
        CHECK(you.charges_of(itype_plut_cell) == 0);
    }

    SECTION("25 cells cap at the pack's 10000 charges and leave 5") {
        seat_pack(you, reactor, itype_plut_pack_reactor, 0);
        REQUIRE(reactor.ammo_remaining() == 0);

        // CDF: capacity 10000 / 500 = 20 whole cells
        you.i_add(item::spawn(itype_plut_cell, calendar::turn, 25));
        REQUIRE(you.charges_of(itype_plut_cell) == 25);

        item& pack = *reactor.get_base().magazine_current();
        REQUIRE(pack.reload(you, loose_cell_stack(you), 25));

        CHECK(reactor.ammo_remaining() == 10000);
        CHECK(you.charges_of(itype_plut_cell) == 5);
    }
}

// K7
TEST_CASE("removing_the_pack_hands_it_over_whole",
          "[vehicle][reactor][plut][reactorpack1][p3]") {
    clear_all_state();
    build_test_map(ter_id("t_pavement"));
    map& here = get_map();
    avatar& you = get_avatar();
    clear_avatar();

    vehicle* veh_ptr =
        here.add_vehicle(vproto_reactor_test, tripoint_bub_ms(10, 10, 0), 0_degrees, 0, 0);
    vehicle_part& reactor = reactor_part(veh_ptr);

    // 3 cells + 250 charges: a partial charge the whole-cell unload path would round away
    seat_pack(you, reactor, itype_plut_pack_reactor, 1750);
    REQUIRE(reactor.ammo_remaining() == 1750);

    detached_ptr<item> pack = reactor.remove_fuel_pack();
    REQUIRE(pack);
    CHECK(pack->typeId() == itype_plut_pack_reactor);
    CHECK(pack->ammo_remaining() == 1750);

    CHECK(reactor.ammo_remaining() == 0);
    CHECK(reactor.ammo_capacity() == 10000);
    CHECK(reactor.get_base().magazine_current() == nullptr);
    const std::map<itype_id, int> fuels = veh_ptr->fuels_left();
    CHECK(fuels.count(itype_plut_cell) == 0);
}

// K8
TEST_CASE("the_burn_draws_from_the_pack", "[vehicle][reactor][plut][reactorpack1][p3]") {
    clear_all_state();
    build_test_map(ter_id("t_pavement"));
    map& here = get_map();
    avatar& you = get_avatar();
    clear_avatar();

    // the shape of tests/vehicle_power_test.cpp:46-68 with a seated pack of one charge
    vehicle* veh_ptr =
        here.add_vehicle(vproto_reactor_test, tripoint_bub_ms(10, 10, 0), 0_degrees, 0, 0);
    vehicle_part& reactor = reactor_part(veh_ptr);
    reactor.enabled = true;
    seat_pack(you, reactor, itype_plut_pack_reactor, 1);
    REQUIRE(reactor.ammo_remaining() == 1);
    veh_ptr->discharge_battery(veh_ptr->fuel_left(itype_id("battery")));

    veh_ptr->power_parts();
    // CDF: vehicle.cpp:5287-5301, power 100 = 100 battery units per fuel unit
    CHECK(veh_ptr->fuel_left(itype_id("battery")) == 100);
    CHECK(reactor.ammo_remaining() == 0);
    // an emptied pack stays seated
    item* pack = reactor.get_base().magazine_current();
    REQUIRE(pack != nullptr);
    CHECK(pack->typeId() == itype_plut_pack_reactor);

    // CDF: vehicle.cpp:5305-5311 switches every reactor off when no reactor has fuel
    veh_ptr->power_parts();
    CHECK_FALSE(reactor.enabled);
}

// K9
TEST_CASE("a_damaged_pack_reactor_leaks_slurry",
          "[vehicle][reactor][plut][reactorpack1][p3]") {
    clear_all_state();
    build_test_map(ter_id("t_pavement"));
    map& here = get_map();
    avatar& you = get_avatar();
    clear_avatar();

    vehicle* veh_ptr =
        here.add_vehicle(vproto_reactor_test, tripoint_bub_ms(10, 10, 0), 0_degrees, 0, 0);
    vehicle_part& reactor = reactor_part(veh_ptr);
    seat_pack(you, reactor, itype_plut_pack_reactor, 1000);

    // CDF: vehicle_part.cpp is_leaking() is health <= 50%; vehicle.cpp:520-531 set_hp
    veh_ptr->set_hp(reactor, reactor.info().durability / 4);
    REQUIRE(reactor.health_percent() <= 0.5);
    REQUIRE(reactor.is_leaking());

    const tripoint_bub_ms pos = veh_ptr->bub_part_location(reactor);
    veh_ptr->slow_leak();

    // CDF: vehicle.cpp:5774-5793 with health 0.25 computes
    // qty = int( (0.5 - 0.25)^2 * 1000 / 10 ) = 6 charges, and consumes
    // qty * PLUTONIUM_CHARGES / 10 = 300 charges from the pack
    CHECK(1000 - reactor.ammo_remaining() == 6 * (PLUTONIUM_CHARGES / 10));

    // the slurry is a liquid and spills into a field; no cell or charge item lands
    bool items_on_ground = false;
    for (const item* it : here.i_at(pos)) {
        if (it->typeId() == itype_plut_cell || it->typeId() == itype_plut_cell_charge) {
            items_on_ground = true;
        }
    }
    CHECK_FALSE(items_on_ground);
    const field_type_id fd_plutonium_slurry("fd_plutonium_slurry");
    CHECK(here.get_field_intensity(pos, fd_plutonium_slurry) == 1);

    // the pack survives with the rest of its charge
    CHECK(reactor.ammo_remaining() == 700);
}

// K10
TEST_CASE("the_reactor_pack_is_obtainable", "[vehicle][reactor][plut][reactorpack1][p3]") {
    clear_all_state();

    // CDF: data/json/recipes/electronic/cdf_plut_pack_reactor.json: 20 cells = 10000 charges
    const recipe_id recipe_pack("plut_pack_reactor");
    REQUIRE(recipe_pack.is_valid());
    const recipe& r = recipe_pack.obj();

    int cells = 0;
    for (const std::vector<item_comp>& options : r.simple_requirements().get_components()) {
        for (const item_comp& comp : options) {
            if (comp.type == itype_plut_cell) {
                cells = std::max(cells, comp.count);
            }
        }
    }
    CHECK(cells == 20);
    CHECK(r.create_result()->ammo_capacity(ammo_plutonium) == 20 * PLUTONIUM_CHARGES);

    // CDF: the loot reactor (data/json/itemgroups/science_and_tech.json:513) never debugmsgs and
    // is dressed with no pack or with a pack of at most the default fill
    for (int i = 0; i < 200; i++) {
        std::vector<detached_ptr<item>> spawned;
        const std::string msg = capture_debugmsg_during([&spawned]() {
            spawned = item_group::items_from(group_microlab_plutonium);
        });
        CAPTURE(i, msg);
        CHECK(msg.empty());
        for (const detached_ptr<item>& it : spawned) {
            if (it->typeId() != itype_minireactor) {
                continue;
            }
            const item* pack = it->magazine_current();
            if (pack != nullptr) {
                CHECK(pack->typeId() == itype_plut_pack_reactor);
                CHECK(pack->ammo_remaining() <= 10000);
            }
        }
    }
}
