#include "calendar.h"
#include "catch/catch.hpp"
#include "debug.h"
#include "item.h"
#include "item_contents.h"
#include "itype.h"
#include "json.h"
#include "pocket_data.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// CDF: P3-7-R3b (re-cut). Batch inputs: tests/data/dda_staged/p3_batches/7-R3b.json and
// neg-7-R3b.json (the generic cases B1-B5 are in tests/p3_batch_generic_test.cpp; the golden load
// is "p3 golden load batches"). PL-43-b re-cut 2026-11-10: 93 BATT-owned rows were dropped
// (BATT1/BATT2 merged, BATT4/LINKUP1 hold); 8 rows remain: the 3 control laptops (dormant wells
// finished here), the 2 plastic jack o'lanterns (BATT1 CHECK-ONLY rows; this packet edits first)
// and the 3 acetylene tools (a tank well, not a magazine well).
//
// CDF: the unported tree still answers most value samples: src/item_factory.cpp:1155-1200
// synthesizes an equivalent well from the legacy `magazines` keys, and R4-4b already put a
// physical well on control_laptop and plastic_jack_o_lantern (with a `max_item_length` no DDA
// twin has). S1 therefore also pins what the port changes (control_laptop_music gets its own
// item_restriction list instead of inheriting the flag well); S3 pins the legacy `magazines` table
// that keeps the reload fallback alive. Those checks are red on the unported data and green after
// the port; the plain value checks pin the values the port must keep.

namespace {

const std::string R3B_BATCH = "tests/data/dda_staged/p3_batches/7-R3b.json";

struct r3b_row {
    std::string id;
    std::string file;
    bool has_well = false;
    bool has_default = false;
    std::string default_magazine;
};

auto read_r3b_batch() -> std::vector<r3b_row> {
    std::ifstream file(R3B_BATCH, std::ios::binary);
    REQUIRE(file.good());
    JsonIn jsin(file);
    JsonObject root = jsin.get_object();
    root.allow_omitted_members();
    JsonObject items = root.get_object("items");
    items.allow_omitted_members();
    std::vector<r3b_row> out;
    for (const JsonMember member : items) {
        JsonObject row = member.get_object();
        row.allow_omitted_members();
        r3b_row r;
        r.id = member.name();
        r.file = row.get_string("file");
        for (JsonObject pocket : row.get_array("pockets")) {
            pocket.allow_omitted_members();
            if (pocket.get_string("pocket_type") != "MAGAZINE_WELL") { continue; }
            r.has_well = true;
            if (pocket.has_string("default_magazine")) {
                r.has_default = true;
                r.default_magazine = pocket.get_string("default_magazine");
            }
        }
        out.push_back(r);
    }
    return out;
}

// The one MAGAZINE_WELL pocket of a loaded item (the loader adds a MOD pocket too).
auto well_pocket(const std::string& id) -> const pocket_data& {
    const itype& t = itype_id(id).obj();
    const pocket_data* found = nullptr;
    int wells = 0;
    for (const pocket_data& pocket : t.pockets) {
        if (pocket.type == pocket_type::MAGAZINE_WELL) {
            found = &pocket;
            wells++;
        }
    }
    INFO("item " << id);
    REQUIRE(wells == 1);
    return *found;
}

auto count_type(const std::string& id, pocket_type type) -> int {
    int n = 0;
    for (const pocket_data& pocket : itype_id(id).obj().pockets) {
        n += pocket.type == type ? 1 : 0;
    }
    return n;
}

auto has_id(const pocket_data& pocket, const std::string& id) -> bool {
    return pocket.item_id_restriction.count(itype_id(id)) == 1;
}

auto has_flag(const pocket_data& pocket, const std::string& flag) -> bool {
    return pocket.get_flag_restrictions().count(flag_id(flag)) == 1;
}

// A fresh tool with every magazine taken out, so the well is empty and can_contain tests the
// restriction only (a spawned tool carries its default magazine).
auto empty_tool(const std::string& id) -> item* {
    item* tool = item::spawn_temporary(itype_id(id), calendar::turn);
    tool->contents.clear_magazines();
    return tool;
}

auto accepts(const item& tool, const std::string& mag) -> bool {
    const item m(itype_id(mag), calendar::turn);
    return tool.can_contain(m).success();
}

} // namespace

TEST_CASE("p3 r3b samples", "[p3][data][R3b][r3b]") {
    // CDF: data/json/items/tool/electronics.json control_laptop: the DDA twin's flag well
    // (DDA data/mods/aftershock_exoplanet/items/tools.json: BATTERY_MEDIUM, medium_battery_cell);
    // the DDA twin's extra E_FILE_STORAGE pocket is not copied.
    const pocket_data& laptop = well_pocket("control_laptop");
    CHECK(laptop.get_flag_restrictions().size() == 1);
    CHECK(has_flag(laptop, "BATTERY_MEDIUM"));
    CHECK(laptop.default_magazine == itype_id("medium_battery_cell"));
    CHECK(laptop.item_id_restriction.empty());

    // CDF: control_laptop_music has no DDA twin (it is CDF-only), so the well is authored from
    // the legacy `magazines` list: the four CDF medium cells, first id = default.
    const pocket_data& music = well_pocket("control_laptop_music");
    CHECK(music.item_id_restriction.size() == 4);
    for (const std::string& id :
         {"medium_battery_cell", "medium_plus_battery_cell", "medium_atomic_battery_cell",
          "medium_disposable_cell"}) {
        CAPTURE(id);
        CHECK(has_id(music, id));
    }
    CHECK(music.default_magazine == itype_id("medium_battery_cell"));

    // CDF: items_holiday.json, DDA twin: BATTERY_LIGHT, light_battery_cell
    const pocket_data& lantern = well_pocket("plastic_jack_o_lantern");
    CHECK(lantern.get_flag_restrictions().size() == 1);
    CHECK(has_flag(lantern, "BATTERY_LIGHT"));
    CHECK(lantern.default_magazine == itype_id("light_battery_cell"));

    // CDF: workshop.json oxy_torch: DDA's three tanks minus `smallweldtank` (not a CDF item);
    // DDA lists weldtank first, so the loader default is weldtank.
    const pocket_data& torch = well_pocket("oxy_torch");
    CHECK(torch.item_id_restriction.size() == 2);
    CHECK(has_id(torch, "weldtank"));
    CHECK(has_id(torch, "tinyweldtank"));
    CHECK(torch.default_magazine == itype_id("weldtank"));

    // CDF: lighting.json oxylamp: DDA lists tinyweldtank first; oxylamp_on inherits the well.
    const pocket_data& lamp = well_pocket("oxylamp");
    CHECK(lamp.item_id_restriction.size() == 2);
    CHECK(has_id(lamp, "weldtank"));
    CHECK(has_id(lamp, "tinyweldtank"));
    CHECK(lamp.default_magazine == itype_id("tinyweldtank"));
    const pocket_data& lamp_on = well_pocket("oxylamp_on");
    CHECK(lamp_on.item_id_restriction.size() == 2);
    CHECK(lamp_on.default_magazine == itype_id("tinyweldtank"));

    // CDF: every explicit well states the 2 km length limit (tests/item_length_test.cpp "magazine
    // wells have an explicit length limit"); the key is CDF's, not a DDA plan key.
    for (const r3b_row& row : read_r3b_batch()) {
        CAPTURE(row.id);
        CHECK(well_pocket(row.id).max_item_length >= 2_km);
    }
}

TEST_CASE("p3 r3b battery rule", "[p3][data][R3b][r3b]") {
    // CDF: Q-R3-1 (keep CDF acceptance) on the rows that remain: the DDA flag well must accept
    // every cell the CDF `magazines` list accepted (battery.json: the four light cells carry
    // BATTERY_LIGHT, the four medium cells BATTERY_MEDIUM, AUTO-236 keeps the CDF capacities).
    const item* laptop = empty_tool("control_laptop");
    for (const std::string& id :
         {"medium_battery_cell", "medium_plus_battery_cell", "medium_atomic_battery_cell",
          "medium_disposable_cell"}) {
        CAPTURE(id);
        CHECK(accepts(*laptop, id));
    }
    CHECK_FALSE(accepts(*laptop, "light_battery_cell"));
    CHECK_FALSE(accepts(*laptop, "heavy_battery_cell"));

    const item* lantern = empty_tool("plastic_jack_o_lantern");
    for (const std::string& id :
         {"light_battery_cell", "light_plus_battery_cell", "light_atomic_battery_cell",
          "light_disposable_cell"}) {
        CAPTURE(id);
        CHECK(accepts(*lantern, id));
    }
    CHECK_FALSE(accepts(*lantern, "medium_battery_cell"));
    CHECK_FALSE(accepts(*lantern, "light_minus_battery_cell"));
}

TEST_CASE("p3 r3b acetylene tank wells", "[p3][data][R3b][r3b]") {
    // CDF: DDA's three acetylene tools carry a MAGAZINE_WELL for welding tanks, not a magazine
    // (the legacy CDF `magazines` table is what the port removes; src/item.cpp:9560-9590 still
    // falls back to it). Red before the port.
    for (const std::string& id : {"oxy_torch", "oxylamp", "oxylamp_on"}) {
        CAPTURE(id);
        CHECK(itype_id(id).obj().magazines.empty());
        CHECK(count_type(id, pocket_type::MAGAZINE_WELL) == 1);
        CHECK(count_type(id, pocket_type::MAGAZINE) == 0);
        const pocket_data& well = well_pocket(id);
        CHECK(has_id(well, "weldtank"));
        CHECK(has_id(well, "tinyweldtank"));
        // CDF: DDA's `smallweldtank` has no CDF item (the tool's dropped_ids)
        CHECK_FALSE(has_id(well, "smallweldtank"));
    }
    // the well answers the reload/compat path with the two CDF tanks
    const item* torch = empty_tool("oxy_torch");
    CHECK(accepts(*torch, "weldtank"));
    CHECK(accepts(*torch, "tinyweldtank"));
    CHECK_FALSE(accepts(*torch, "medium_battery_cell"));
}

TEST_CASE("p3 r3b spawn all", "[p3][data][R3b][r3b]") {
    clear_all_state();
    const std::vector<r3b_row> rows = read_r3b_batch();
    REQUIRE(rows.size() == 8);
    size_t with_default = 0;
    size_t without_default = 0;
    for (const r3b_row& row : rows) {
        CAPTURE(row.id);
        REQUIRE(itype_id(row.id).is_valid());
        REQUIRE(row.has_well);
        const item* it = nullptr;
        const std::string dmsg = capture_debugmsg_during([&]() {
            it = item::spawn_temporary(itype_id(row.id), calendar::turn);
        });
        CHECK(dmsg.empty());
        REQUIRE(it != nullptr);
        CHECK(it->typeId() == itype_id(row.id));
        const pocket_data& pocket = well_pocket(row.id);
        if (row.has_default) {
            with_default++;
            CHECK(pocket.default_magazine == itype_id(row.default_magazine));
            // CDF: the spawn path puts the well's default magazine in (src/item.cpp:392-399)
            REQUIRE_FALSE(it->magazines_current().empty());
            CHECK(it->magazines_current().front()->typeId() == itype_id(row.default_magazine));
            CHECK(it->ammo_remaining() > 0);
        } else {
            without_default++;
            // CDF: the acetylene tools spawn without a tank (no default_magazine, no charges)
            CHECK(it->magazines_current().empty());
        }
    }
    std::cout << "p3 r3b spawn all: ids " << rows.size() << " with default " << with_default
              << " without " << without_default << std::endl;
    // CDF: the 5 battery rows carry a default (3 medium, 2 light); the 3 tank rows do not
    CHECK(with_default == 5);
    CHECK(without_default == 3);
}
