#include "catch/catch.hpp"
#include "calendar.h"
#include "item.h"
#include "itype.h"
#include "json.h"
#include "pocket_data.h"

#include <fstream>
#include <map>
#include <string>
#include <vector>

// CDF: P3-7-R5. Batch input: tests/data/dda_staged/p3_batches/7-R5.json (4 ids in `items`,
// 18 KEEP-CHARGES ids in `waiting_not_in_batch`). D1 is "p3 golden load batches" (7-GOLD1).

namespace {

const std::string R5_BATCH = "tests/data/dda_staged/p3_batches/7-R5.json";

struct r5_waiting {
    std::string id;
    bool has_max_charges = false;
    int max_charges = 0;
};

auto read_r5_waiting() -> std::vector<r5_waiting> {
    std::ifstream file(R5_BATCH, std::ios::binary);
    JsonIn jsin(file);
    JsonObject root = jsin.get_object();
    root.allow_omitted_members();
    JsonObject waiting = root.get_object("waiting_not_in_batch");
    waiting.allow_omitted_members();
    std::vector<r5_waiting> out;
    for (const JsonMember member : waiting) {
        JsonObject row = member.get_object();
        row.allow_omitted_members();
        r5_waiting w;
        w.id = member.name();
        JsonObject legacy = row.get_object("legacy");
        legacy.allow_omitted_members();
        if (legacy.has_int("max_charges")) {
            w.has_max_charges = true;
            w.max_charges = legacy.get_int("max_charges");
        }
        out.push_back(w);
    }
    return out;
}

} // namespace

TEST_CASE("p3 r5 charges literals", "[p3][data][R5]") {
    struct row {
        std::string id;
        std::string ammo;
        int max_charges;
    };
    const std::vector<row> rows = {
        // CDF: items/tool/med.json oxygen_tank max_charges 24, smoxygen_tank 12
        {"oxygen_tank", "oxygen", 24},
        {"smoxygen_tank", "oxygen", 12},
        // CDF: items/tool/science.json oxygen_cylinder max_charges 300
        {"oxygen_cylinder", "oxygen", 300},
        // CDF: items/tool/cooking.json char_purifier max_charges 150 (DDA has 180)
        {"char_purifier", "charcoal", 150},
    };
    for (const row& r : rows) {
        CAPTURE(r.id);
        REQUIRE(itype_id(r.id).is_valid());
        const itype& t = itype_id(r.id).obj();
        REQUIRE(t.tool);
        CHECK(t.tool->max_charges == r.max_charges);
        CHECK(t.tool->ammo_id.contains(ammotype(r.ammo)));
        // MOD1: every tool also has its MOD pocket, appended after the magazine pocket
        REQUIRE(t.pockets.size() == 2);
        REQUIRE(t.pockets.front().type == pocket_type::MAGAZINE);
        REQUIRE(t.pockets.front().ammo_restriction.count(ammotype(r.ammo)) == 1);
        CHECK(t.pockets.front().ammo_restriction.at(ammotype(r.ammo)) == r.max_charges);
        item it(r.id, calendar::turn, r.max_charges);
        CHECK(it.ammo_remaining() == r.max_charges);
        CHECK(it.ammo_capacity() == r.max_charges);
    }
}

TEST_CASE("p3 r5 waiting rows unchanged", "[p3][data][R5]") {
    const std::vector<r5_waiting> rows = read_r5_waiting();
    REQUIRE(rows.size() == 18);
    int checked = 0;
    for (const r5_waiting& w : rows) {
        CAPTURE(w.id);
        if (!itype_id(w.id).is_valid()) {
            INFO("not loaded in the base data (obsoletion row): " << w.id);
            continue;
        }
        const itype& t = itype_id(w.id).obj();
        for (const pocket_data& p : t.pockets) {
            // CDF: the row stays on `charges` until its ammotype exists (AMMOTYPE1)
            CHECK(p.type != pocket_type::MAGAZINE);
        }
        if (w.has_max_charges && t.tool) {
            // CDF: batch `legacy.max_charges` = the literal in items/tool/*.json at the base
            CHECK(t.tool->max_charges == w.max_charges);
        }
        checked++;
    }
    CHECK(checked > 0);
}
