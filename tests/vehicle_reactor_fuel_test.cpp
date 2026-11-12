// CDF: MINIREACTOR1. Pins the minireactor's plutonium fuel path in the vehicle code.
// REACTORPACK1 (ruled edit of the MINIREACTOR1 amendments): the item no longer keeps fuel in an
// integral MAGAZINE pocket; it takes a removable plutonium fuel pack.  R1, R3 and R4 stay as
// written (they hold through the ammo_current() normalisation) and are the regression guard;
// R2 (loose cells into an empty part), R5 (the item in hand) and R6 (the integral-item shape pin)
// are replaced by tests/vehicle_reactor_pack_test.cpp K6, K4/K7 and K1.
// DDA reference: data/mods/aftershock_exoplanet/vehicles/part_items.json:101.
// CDF pin: tests/data/dda_staged/p3_golden.json entry "minireactor" (now class WELL, rule R3,
// AUTHOR-KEEP-CDF-WELL, one MAGAZINE_WELL; cite by id, the line moves with later packets).
#include "../src/map/field_type.h"
#include "../src/map/map.h"
#include "../src/vehicle/vehicle_part.h"
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
const vproto_id vproto_reactor_test("reactor_test");

// The minireactor part of reactor_test (data/json/vehicles/test.json:155-163).
auto reactor_part(vehicle* veh) -> vehicle_part& {
    REQUIRE(veh != nullptr);
    REQUIRE(!veh->reactors.empty());
    return veh->part(veh->reactors.front());
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

// R2 is gone: CDF: REACTORPACK1 (ruled edit of the MINIREACTOR1 amendments).  Loose cells no
// longer go into an empty reactor; they fill the seated pack (vehicle_reactor_pack_test.cpp K6).

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

// R5 and R6 are gone: CDF: REACTORPACK1 (ruled edits of the MINIREACTOR1 amendments).  The item
// in hand is no longer an integral magazine and unloads as a pack; the well-shaped item, the pack
// handover and the cells-into-pack path are pinned by vehicle_reactor_pack_test.cpp K1, K7 and K6.
