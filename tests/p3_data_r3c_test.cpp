#include "calendar.h"
#include "catch/catch.hpp"
#include "debug.h"
#include "item.h"
#include "itype.h"
#include "json.h"
#include "pocket_data.h"
#include "state_helpers.h"
#include "type_id.h"

#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

// CDF: P3-7-R3c (RE-CUT, orchestration 2026-11-11). Batch inputs:
// tests/data/dda_staged/p3_batches/7-R3c.json and neg-7-R3c.json (the generic cases B1-B5 are in
// tests/p3_batch_generic_test.cpp; the golden load is "p3 golden load batches").
//
// The 93-row v2 target was cut at c7bf62a582. 71 of its ids are other packets' work now (61
// battery wells: BATT1 / BATT2 / BATT4 hold, 2 storage rows: STOR5, 8 ids retired by MIG1) and are
// not edited here. The 22 rows left are 16 filter/cartridge wells (mask_*, rebreather*,
// helmet_scavenger), game_watch (a native battery well BATT1's test also asserts) and 5 obsolete
// guns whose well only this packet adds (rifle_308, smg_40, smg_45, smg_9mm, surv_carbine_223).
//
// The unported tree still answers most value samples: src/item_factory.cpp:1187-1231 synthesizes
// an equivalent well from the legacy `magazines` key, so the plain value checks also pass before
// the port. S2 pins what the port itself changes (the legacy `magazines` table is gone from the
// loaded type); S1, S3 and S4 pin the values and acceptance the port must keep.

namespace {

const std::string R3C_BATCH = "tests/data/dda_staged/p3_batches/7-R3c.json";

struct r3c_row {
    std::string id;
    std::string file;
    bool has_well = false;
    bool has_default = false;
    std::string default_magazine;
    std::vector<std::string> flags;
    std::vector<std::string> ids;
};

auto read_r3c_batch() -> std::vector<r3c_row> {
    std::ifstream file(R3C_BATCH, std::ios::binary);
    REQUIRE(file.good());
    JsonIn jsin(file);
    JsonObject root = jsin.get_object();
    root.allow_omitted_members();
    JsonObject items = root.get_object("items");
    items.allow_omitted_members();
    std::vector<r3c_row> out;
    for (const JsonMember member : items) {
        JsonObject row = member.get_object();
        row.allow_omitted_members();
        r3c_row r;
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
            for (const std::string& f : pocket.get_string_array("flag_restriction")) {
                r.flags.push_back(f);
            }
            for (const std::string& id : pocket.get_string_array("item_restriction")) {
                r.ids.push_back(id);
            }
        }
        out.push_back(r);
    }
    return out;
}

auto wells(const std::string& id) -> std::vector<const pocket_data*> {
    std::vector<const pocket_data*> found;
    for (const pocket_data& pocket : itype_id(id).obj().pockets) {
        if (pocket.type == pocket_type::MAGAZINE_WELL) { found.push_back(&pocket); }
    }
    return found;
}

// The one MAGAZINE_WELL pocket of a loaded item (the loader adds a MOD pocket too).
auto well_pocket(const std::string& id) -> const pocket_data& {
    const std::vector<const pocket_data*> found = wells(id);
    INFO("item " << id);
    REQUIRE(found.size() == 1);
    return *found.front();
}

auto has_id(const pocket_data& pocket, const std::string& id) -> bool {
    return pocket.item_id_restriction.count(itype_id(id)) == 1;
}

auto has_flag(const pocket_data& pocket, const std::string& flag) -> bool {
    return pocket.get_flag_restrictions().count(flag_id(flag)) == 1;
}

auto flag_names(const pocket_data& pocket) -> std::set<std::string> {
    std::set<std::string> names;
    for (const flag_id& f : pocket.get_flag_restrictions()) { names.insert(f.str()); }
    return names;
}

auto id_names(const pocket_data& pocket) -> std::set<std::string> {
    std::set<std::string> names;
    for (const itype_id& id : pocket.item_id_restriction) { names.insert(id.str()); }
    return names;
}

auto accept(const std::string& item_id, const std::string& magazine_id) -> bool {
    item it(item_id, calendar::turn);
    return it.can_contain(item(magazine_id, calendar::turn)).success();
}

} // namespace

TEST_CASE("p3 r3c samples", "[p3][data][R3c]") {
    clear_all_state();
    // CDF: data/json/items/tool_armor.json game_watch, `magazines` battery ->
    // light_minus_battery_cell (the three light_minus cells by flag); the DDA twin well is
    // BATTERY_ULTRA_LIGHT.
    {
        const pocket_data& pocket = well_pocket("game_watch");
        CHECK(pocket.rigid);
        CHECK(flag_names(pocket) == std::set<std::string>{"BATTERY_ULTRA_LIGHT"});
        CHECK(pocket.default_magazine == itype_id("light_minus_battery_cell"));
    }
    // CDF: tool_armor.json mask_filter, `magazines` gasfilter_s -> gasfilter_sm; DDA flag
    // GASFILTER_SM.
    {
        const pocket_data& pocket = well_pocket("mask_filter");
        CHECK(pocket.rigid);
        CHECK(flag_names(pocket) == std::set<std::string>{"GASFILTER_SM"});
        CHECK(pocket.default_magazine == itype_id("gasfilter_sm"));
    }
    // CDF: mask_halfsurvivor has no DDA twin (AUTHOR): the well is authored from CDF's own
    // `magazines` gasfilter_m -> gasfilter_med, written as an item_restriction in the golden order.
    {
        const pocket_data& pocket = well_pocket("mask_halfsurvivor");
        CHECK(pocket.rigid);
        CHECK(id_names(pocket) == std::set<std::string>{"gasfilter_med"});
        CHECK(pocket.default_magazine == itype_id("gasfilter_med"));
    }
    // CDF: obsoletion/items.json rifle_308 (AUTHOR, no twin), `magazines` 308 -> the three G3 mags,
    // default from the first listed id (src/pocket_data.cpp:74-78).
    {
        const pocket_data& pocket = well_pocket("rifle_308");
        CHECK(id_names(pocket) == std::set<std::string>{"g3_makeshiftmag", "g3mag", "g3bigmag"});
        CHECK(pocket.default_magazine == itype_id("g3_makeshiftmag"));
    }
    // CDF: smg_9mm (COPY-TWIN) takes the DDA twin's list order, so survivor9mm_mag is the default.
    {
        const pocket_data& pocket = well_pocket("smg_9mm");
        CHECK(id_names(pocket) == std::set<std::string>{"survivor9mm_mag", "stenmag"});
        CHECK(pocket.default_magazine == itype_id("survivor9mm_mag"));
    }
}

TEST_CASE("p3 r3c pocket shape", "[p3][data][R3c]") {
    clear_all_state();
    const std::vector<r3c_row> rows = read_r3c_batch();
    REQUIRE(rows.size() == 22);
    size_t checked = 0;
    for (const r3c_row& row : rows) {
        CAPTURE(row.id);
        REQUIRE(itype_id(row.id).is_valid());
        REQUIRE(row.has_well);
        const itype& type = itype_id(row.id).obj();
        // CDF: the well is the item's first pocket; the MOD pocket the loader adds follows it
        REQUIRE_FALSE(type.pockets.empty());
        CHECK(type.pockets.front().type == pocket_type::MAGAZINE_WELL);
        const pocket_data& pocket = well_pocket(row.id);
        // CDF: the loaded well is the batch row's golden pocket, id by id
        CHECK(flag_names(pocket) == std::set<std::string>(row.flags.begin(), row.flags.end()));
        CHECK(id_names(pocket) == std::set<std::string>(row.ids.begin(), row.ids.end()));
        if (row.has_default) { CHECK(pocket.default_magazine == itype_id(row.default_magazine)); }
        // CDF: the port removes the legacy `magazines` table; the live well must answer alone
        // (src/item.cpp:9554-9590 still falls back to it). Red before the port on all 22 rows.
        CHECK(type.magazines.empty());
        checked++;
    }
    std::cout << "p3 r3c pocket shape: ids " << checked << std::endl;
    CHECK(checked == 22);
}

TEST_CASE("p3 r3c well acceptance", "[p3][data][R3c]") {
    clear_all_state();
    // CDF: CDF's filter cells are accepted (the DDA flag covers them: GASFILTER_SM / _MED,
    // REBREATHER_CART); a cell of the other size is refused.
    CHECK(accept("mask_filter", "gasfilter_sm"));
    CHECK_FALSE(accept("mask_filter", "gasfilter_med"));
    CHECK_FALSE(accept("mask_filter", "light_minus_battery_cell"));
    CHECK(accept("mask_gas", "gasfilter_med"));
    CHECK_FALSE(accept("mask_gas", "gasfilter_sm"));
    CHECK(accept("rebreather", "rebreather_cartridge_o2"));
    CHECK_FALSE(accept("rebreather", "gasfilter_med"));
    // CDF: a CDF-only choice is kept: game_watch's legacy `magazines` accepted the light_minus
    // cell, its atomic and its disposable variant, all three carry BATTERY_ULTRA_LIGHT.
    CHECK(accept("game_watch", "light_minus_battery_cell"));
    CHECK(accept("game_watch", "light_minus_atomic_battery_cell"));
    CHECK(accept("game_watch", "light_minus_disposable_cell"));
    CHECK_FALSE(accept("game_watch", "light_battery_cell"));
    CHECK_FALSE(accept("game_watch", "medium_battery_cell"));
    // CDF: the authored well of the obsolete guns keeps the exact CDF `magazines` lists.
    CHECK(accept("rifle_308", "g3mag"));
    CHECK(accept("rifle_308", "g3bigmag"));
    CHECK_FALSE(accept("rifle_308", "stanag30"));
    CHECK(accept("smg_45", "mac10mag"));
    CHECK_FALSE(accept("smg_45", "smg_40_mag"));
    CHECK(accept("smg_9mm", "stenmag"));
    CHECK_FALSE(accept("smg_9mm", "survivor223mag"));
    CHECK(accept("surv_carbine_223", "stanag150"));
    CHECK_FALSE(accept("surv_carbine_223", "survivor9mm_mag"));
}

TEST_CASE("p3 r3c spawn all", "[p3][data][R3c]") {
    clear_all_state();
    const std::vector<r3c_row> rows = read_r3c_batch();
    REQUIRE(rows.size() == 22);
    size_t spawned = 0;
    size_t armor = 0;
    size_t guns = 0;
    for (const r3c_row& row : rows) {
        CAPTURE(row.id);
        REQUIRE(itype_id(row.id).is_valid());
        const std::string captured = capture_debugmsg_during([&]() {
            item it(row.id, calendar::turn);
            CHECK(wells(row.id).size() == 1);
        });
        INFO("debugmsg: " << captured);
        CHECK(captured.empty());
        item it(row.id, calendar::turn);
        if (it.is_gun()) {
            guns++;
            // CDF: the 5 obsolete guns have no pocket but the well (a lone MAGAZINE_WELL)
            CHECK_FALSE(it.is_armor());
        } else {
            armor++;
            // CDF: the worn rows still wear: the explicit well must not drop the armor portions
            CHECK(it.is_armor());
        }
        spawned++;
    }
    std::cout << "p3 r3c spawn all: ids " << spawned << std::endl;
    // CDF: the re-cut batch: 17 worn rows (16 masks/rebreathers + game_watch) and 5 guns
    CHECK(armor == 17);
    CHECK(guns == 5);
    CHECK(spawned == 22);
}
