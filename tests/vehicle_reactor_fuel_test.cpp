// CDF: MINIREACTOR1. Pins the minireactor's integral plutonium fuel path in the vehicle code.
// The minireactor item is a TOOL with one synthesized integral MAGAZINE pocket
// (item_factory add_integral_magazine_pocket), so item::is_magazine() is true and item::reload
// takes the is_magazine()/plutonium branch (src/item.cpp:10120-10159); type->magazine is null for
// a TOOL, so the pocket keeps plut_cell contents with the charges scaled (:10141-10145).
// DDA reference: data/mods/aftershock_exoplanet/vehicles/part_items.json:101.
// CDF pin: tests/data/dda_staged/p3_golden.json entry "minireactor" (TOOL-INT, one MAGAZINE pocket,
// plutonium 10000; cite by id, the line moves with later packets).
#include "../src/map/field_type.h"
#include "../src/map/map.h"
#include "../src/vehicle/vehicle_part.h"
#include "ammo.h"
#include "avatar.h"
#include "avatar_functions.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "game.h"
#include "game_constants.h"
#include "item.h"
#include "itype.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "pocket_data.h"
#include "pocket_type.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"
#include "vehicle/veh_type.h"
#include "vehicle/vehicle.h"

#include <map>
#include <vector>

namespace {

const itype_id itype_plut_cell("plut_cell");
const itype_id itype_plut_cell_charge("plut_cell_charge");
const itype_id itype_plut_slurry_dense("plut_slurry_dense");
const itype_id itype_minireactor("minireactor");
const ammotype ammo_plutonium("plutonium");
const vproto_id vproto_reactor_test("reactor_test");

// The minireactor part of reactor_test (data/json/vehicles/test.json:155-163).
auto reactor_part(vehicle* veh) -> vehicle_part& {
    REQUIRE(veh != nullptr);
    REQUIRE(!veh->reactors.empty());
    return veh->part(veh->reactors.front());
}

// A cell stack lying in the open (not the reactor's own plut_cell contents).
auto loose_cell_stack(avatar& you) -> item& {
    const std::vector<item*> found = you.items_with([](const item& it) {
        return it.typeId() == itype_plut_cell &&
               (it.parent_item() == nullptr || !it.parent_item()->is_tool());
    });
    REQUIRE(!found.empty());
    return *found.front();
}

// Whole cells carried loose (charges == cells for a count-by-charges cell stack).
auto loose_cells(avatar& you) -> int {
    int n = 0;
    for (const item* it : you.items_with([](const item& it) {
             return it.typeId() == itype_plut_cell &&
                    (it.parent_item() == nullptr || !it.parent_item()->is_tool());
         })) {
        n += it->charges;
    }
    return n;
}

} // namespace

// R1
TEST_CASE("a full minireactor part reports its plutonium capacity", "[vehicle][reactor][minireactor1]") {
    clear_all_state();
    build_test_map(ter_id("t_pavement"));
    map& here = get_map();

    // veh_fuel_mult 100, undamaged: vehicle.cpp:836-837 fills the reactor to ammo_capacity()
    vehicle* veh_ptr =
        here.add_vehicle(vproto_reactor_test, tripoint_bub_ms(10, 10, 0), 0_degrees, 100, 0);
    vehicle_part& reactor = reactor_part(veh_ptr);

    // CDF: data/json/vehicleparts/power.json:298-310 (fuel_type plut_cell) and
    // data/json/items/vehicle/utilities.json:19-35 (max_charges 10000)
    CHECK(reactor.ammo_current() == itype_plut_cell);
    CHECK(reactor.ammo_capacity() == 10000);
    CHECK(reactor.ammo_remaining() == 10000);
    CHECK(veh_ptr->fuel_left(itype_plut_cell) == 10000);
}

// R2
TEST_CASE("a minireactor part takes whole plutonium cells through item::reload",
          "[vehicle][reactor][minireactor1]") {
    clear_all_state();
    build_test_map(ter_id("t_pavement"));
    map& here = get_map();
    avatar& you = get_avatar();
    clear_avatar();

    vehicle* veh_ptr =
        here.add_vehicle(vproto_reactor_test, tripoint_bub_ms(10, 10, 0), 0_degrees, 0, 0);
    vehicle_part& reactor = reactor_part(veh_ptr);
    reactor.ammo_unset();
    REQUIRE(reactor.ammo_remaining() == 0);

    SECTION("three cells are 1500 charges and leave no cell behind") {
        // CDF: game_constants.h:80 PLUTONIUM_CHARGES 500; 3 * 500 = 1500
        you.i_add(item::spawn(itype_plut_cell, calendar::turn, 3));
        REQUIRE(you.charges_of(itype_plut_cell) == 3);

        // the call complete_vehicle makes for a fuel store (veh_interact.cpp:3183-3185)
        REQUIRE(reactor.get_base().reload(you, loose_cell_stack(you), 3));

        CHECK(reactor.ammo_remaining() == 1500);
        CHECK(loose_cells(you) == 0);
    }

    SECTION("25 cells cap at 10000 charges and leave 5 cells") {
        // CDF: capacity 10000 / 500 = 20 whole cells (utilities.json:34, game_constants.h:80)
        you.i_add(item::spawn(itype_plut_cell, calendar::turn, 25));
        REQUIRE(you.charges_of(itype_plut_cell) == 25);

        REQUIRE(reactor.get_base().reload(you, loose_cell_stack(you), 25));

        CHECK(reactor.ammo_remaining() == 10000);
        CHECK(loose_cells(you) == 5);
    }
}

// R3
TEST_CASE("a leaking minireactor spills slurry, not plutonium cells",
          "[vehicle][reactor][minireactor1]") {
    clear_all_state();
    build_test_map(ter_id("t_pavement"));
    map& here = get_map();

    vehicle* veh_ptr =
        here.add_vehicle(vproto_reactor_test, tripoint_bub_ms(10, 10, 0), 0_degrees, 0, 0);
    vehicle_part& reactor = reactor_part(veh_ptr);

    // CDF: vehicle_part.cpp:507-509 is_leaking() is health <= 50%; vehicle.cpp:520-531 set_hp
    veh_ptr->set_hp(reactor, reactor.info().durability / 4);
    REQUIRE(reactor.health_percent() <= 0.5);
    REQUIRE(reactor.is_leaking());

    reactor.ammo_set(itype_plut_cell, 1000);
    REQUIRE(reactor.ammo_remaining() == 1000);

    const tripoint_bub_ms pos = veh_ptr->bub_part_location(reactor);
    veh_ptr->slow_leak();

    // CDF: vehicle.cpp:5731 with health 0.25 computes
    // qty = int( (0.5 - 0.25)^2 * 1000 / 10 ) = 6 charges of plut_slurry_dense, and :5744-5747
    // consumes qty * PLUTONIUM_CHARGES / 10 = 300 charges (game_constants.h:80, 500 / 10 = 50).
    CHECK(1000 - reactor.ammo_remaining() == 6 * (PLUTONIUM_CHARGES / 10));

    // A plut_cell is not a liquid, so a cell leak would sit on this tile as an item.  Nothing
    // counted here may be a cell, and no raw slurry item lands either (see the field note below).
    bool cells_on_ground = false;
    for (const item* it : here.i_at(pos)) {
        if (it->typeId() == itype_plut_cell || it->typeId() == itype_plut_cell_charge) {
            cells_on_ground = true;
        }
        CHECK(it->typeId() != itype_plut_slurry_dense);
    }
    CHECK_FALSE(cells_on_ground);

    // CDF: plut_slurry_dense is a liquid whose spill_field is fd_plutonium_slurry
    // (ammo.json:236,241).  map::add_item_or_charges therefore does not leave an item:
    // item::on_drop (item.cpp:12382-12391) spills it into a field, exactly the behaviour
    // tests/map_test.cpp:610-631 pins for liquids.  The six charges are 6 * 250 ml / 10 =
    // 150 ml, which is one 1-litre spill tile (map_field.cpp:893-1008), i.e. intensity 1 here.
    const field_type_id fd_plutonium_slurry("fd_plutonium_slurry");
    CHECK(here.get_field_intensity(pos, fd_plutonium_slurry) == 1);
}

// R4
TEST_CASE("a loaded minireactor reports plut_cell as its vehicle fuel",
          "[vehicle][reactor][minireactor1]") {
    clear_all_state();
    build_test_map(ter_id("t_pavement"));
    map& here = get_map();

    vehicle* veh_ptr =
        here.add_vehicle(vproto_reactor_test, tripoint_bub_ms(10, 10, 0), 0_degrees, 0, 0);
    vehicle_part& reactor = reactor_part(veh_ptr);
    reactor.ammo_set(itype_plut_cell, 1500);
    REQUIRE(reactor.ammo_remaining() == 1500);

    // CDF: vehicle.cpp:7150-7158 fuels_left() keys on part.ammo_current(); power.json:310 fuel_type plut_cell
    const std::map<itype_id, int> fuels = veh_ptr->fuels_left();
    REQUIRE(fuels.count(itype_plut_cell) == 1);
    CHECK(fuels.at(itype_plut_cell) == 1500);
    // CDF: plut_cell_charge is PLUT1's pack charge item (data/json/items/plutonium_packs.json); the
    // integral reactor must never report it
    CHECK(fuels.count(itype_plut_cell_charge) == 0);
    // the vehicle fuel view agrees with fuel_left()
    CHECK(veh_ptr->fuel_left(itype_plut_cell) == fuels.at(itype_plut_cell));
}

// R5
TEST_CASE("unloading a minireactor in hand gives whole cells only",
          "[vehicle][reactor][minireactor1]") {
    clear_all_state();
    avatar& you = get_avatar();
    clear_avatar();

    item& reactor = you.i_add(item::spawn(itype_minireactor));

    SECTION("1500 charges give three whole cells") {
        reactor.ammo_set(itype_plut_cell, 1500);
        REQUIRE(reactor.ammo_remaining() == 1500);

        // CDF: avatar_functions.cpp:1051-1058, 1088-1092: qty = charges / PLUTONIUM_CHARGES
        REQUIRE(avatar_funcs::unload_item(you, reactor));

        CHECK(loose_cells(you) == 3);
        CHECK(reactor.ammo_remaining() == 0);
    }

    SECTION("a part-cell load keeps its remainder and gives the whole cells") {
        // Note: the brief named 1499 for the refusal, but avatar_functions.cpp:1052 computes
        // 1499 / 500 == 2, so that load yields two whole cells and keeps 499 charges.
        reactor.ammo_set(itype_plut_cell, 1499);
        REQUIRE(reactor.ammo_remaining() == 1499);

        REQUIRE(avatar_funcs::unload_item(you, reactor));

        CHECK(loose_cells(you) == 2);
        CHECK(reactor.ammo_remaining() == 499);
    }

    SECTION("a sub-cell load is refused and keeps its charges") {
        // This is the load that reaches the partial refusal (avatar_functions.cpp:1053-1058);
        // dropping that refusal gives zero cells back and a true return, so this case fails.
        reactor.ammo_set(itype_plut_cell, 499);
        REQUIRE(reactor.ammo_remaining() == 499);

        CHECK_FALSE(avatar_funcs::unload_item(you, reactor));

        CHECK(loose_cells(you) == 0);
        CHECK(reactor.ammo_remaining() == 499);
    }
}

// R6
TEST_CASE("the minireactor item stays an integral plutonium magazine",
          "[vehicle][reactor][minireactor1]") {
    clear_all_state();

    // CDF: p3_golden.json entry "minireactor" (TOOL-INT, rule R4, COPY-TWIN): one MAGAZINE
    // pocket, rigid, ammo_restriction plutonium 10000; utilities.json:33-34 ammo/max_charges
    const itype* minireactor = item::spawn_temporary(itype_minireactor)->type;

    int magazines = 0;
    int wells = 0;
    for (const pocket_data& pocket : minireactor->pockets) {
        if (pocket.type == pocket_type::MAGAZINE) {
            ++magazines;
            const auto restriction = pocket.ammo_restriction.find(ammo_plutonium);
            if (restriction != pocket.ammo_restriction.end()) {
                CHECK(restriction->second == 10000);
            }
            CHECK(pocket.rigid);
        } else if (pocket.type == pocket_type::MAGAZINE_WELL) {
            ++wells;
        }
    }

    CHECK(magazines == 1);
    CHECK(wells == 0);

    // CDF: the legacy key still exists at this base; P3-7 may drop it, then this line goes.
    REQUIRE(minireactor->tool != nullptr);
    CHECK(minireactor->tool->max_charges == 10000);
}
