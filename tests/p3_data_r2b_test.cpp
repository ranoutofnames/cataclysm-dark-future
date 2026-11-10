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
#include <string>
#include <vector>

// CDF: P3-7-R2b. Batch inputs: tests/data/dda_staged/p3_batches/7-R2b.json and neg-7-R2b.json
// (the generic cases B1-B5 are in tests/p3_batch_generic_test.cpp; the golden load is
// "p3 golden load batches"). This file pins the MAGAZINE pocket of the 73 ranged weapons,
// gunmods and obsolete guns of the packet (class GUN-INT, rule R2).
//
// The unported tree still answers some samples from the legacy `magazines` table and the
// synthesized integral magazine (kSynthesizeIntegralMagazine, src/item_factory.cpp:878-925).
// S1-S4 therefore also pin what the port changes; the plain value checks pin the CDF numbers
// the port must keep.

namespace {

const std::string R2B_BATCH = "tests/data/dda_staged/p3_batches/7-R2b.json";

struct r2b_row {
    std::string id;
    std::string file;
    std::vector<std::string> clips;
    int clip_size = -1;
};

auto read_r2b_batch() -> std::vector<r2b_row> {
    std::ifstream file(R2B_BATCH, std::ios::binary);
    REQUIRE(file.good());
    JsonIn jsin(file);
    JsonObject root = jsin.get_object();
    root.allow_omitted_members();
    JsonObject items = root.get_object("items");
    items.allow_omitted_members();
    std::vector<r2b_row> out;
    for (const JsonMember member : items) {
        JsonObject row = member.get_object();
        row.allow_omitted_members();
        r2b_row r;
        const std::string name = member.name();
        r.id = name.substr(0, name.find('@'));
        r.file = row.get_string("file");
        JsonObject legacy = row.get_object("legacy");
        legacy.allow_omitted_members();
        if (legacy.has_int("clip_size")) {
            r.clip_size = legacy.get_int("clip_size");
        }
        out.push_back(r);
    }
    return out;
}

// The one MAGAZINE pocket of a loaded item.
auto magazine_pocket(const std::string& id) -> const pocket_data& {
    const itype& t = itype_id(id).obj();
    const pocket_data* found = nullptr;
    int magazines = 0;
    for (const pocket_data& pocket : t.pockets) {
        if (pocket.type == pocket_type::MAGAZINE) {
            found = &pocket;
            magazines++;
        }
    }
    INFO("item " << id);
    REQUIRE(magazines == 1);
    return *found;
}

auto cap_of(const pocket_data& pocket, const std::string& ammo) -> int {
    const auto it = pocket.ammo_restriction.find(ammotype(ammo));
    if (it == pocket.ammo_restriction.end()) { return -1; }
    return it->second;
}

} // namespace

TEST_CASE("p3 r2b samples", "[p3][data][R2b][r2b]") {
    // CDF: data/json/items/ranged/spearguns.json keeps the CDF fishspear cap 4 (DDA 1)
    CHECK(cap_of(magazine_pocket("airspeargun"), "fishspear") == 4);
    // CDF: data/json/items/ranged/spearguns.json keeps the CDF fishspear cap 2 (DDA 1)
    CHECK(cap_of(magazine_pocket("doublespeargun"), "fishspear") == 2);
    // CDF: data/json/obsoletion/items.json: BN `magazines` SPEEDLOADER entry becomes allowed_speedloaders
    const pocket_data& six = magazine_pocket("surv_six_shooter");
    CHECK(six.allowed_speedloaders.size() == 1);
    CHECK(six.allowed_speedloaders.count(itype_id("40_speedloader6")) == 1);
}

TEST_CASE("p3 r2b gunmod pockets", "[p3][data][R2b][r2b]") {
    // CDF: data/json/items/gunmod/underbarrel.json M6_shotgun carries its own integral magazine
    const pocket_data& m6 = magazine_pocket("M6_shotgun");
    CHECK(m6.rigid);
    CHECK(cap_of(m6, "410shot") == 1);
    // CDF: data/json/items/gunmod/underbarrel.json combination_gun_shotgun (COPY-TWIN, DDA shot 2)
    CHECK(cap_of(magazine_pocket("combination_gun_shotgun"), "shot") == 2);
}

TEST_CASE("p3 r2b clip agrees", "[p3][data][R2b][r2b]") {
    size_t checked = 0;
    for (const r2b_row& row : read_r2b_batch()) {
        CAPTURE(row.id);
        REQUIRE(itype_id(row.id).is_valid());
        const pocket_data& pocket = magazine_pocket(row.id);
        // CDF: one MAGAZINE pocket per id, and where the legacy clip_size is set the pocket cap
        // is that number (DDA keeps clip_size; the tool sets the twin's value only where it agrees)
        if (row.clip_size < 0) { continue; }
        // CDF: launcher_ballista keeps two ammo entries in its one MAGAZINE pocket, so the clip
        // number must be one of the pocket's caps, not the only one
        bool clip_is_a_cap = false;
        for (const auto& cap : pocket.ammo_restriction) {
            clip_is_a_cap = clip_is_a_cap || cap.second == row.clip_size;
        }
        CHECK(clip_is_a_cap);
        checked++;
    }
    std::cout << "p3 r2b clip agrees: checked " << checked << std::endl;
}

TEST_CASE("p3 r2b spawn all", "[p3][data][R2b][r2b]") {
    clear_all_state();
    const std::vector<r2b_row> rows = read_r2b_batch();
    REQUIRE(rows.size() == 73);
    size_t spawned = 0;
    for (const r2b_row& row : rows) {
        CAPTURE(row.id);
        REQUIRE(itype_id(row.id).is_valid());
        // CDF: the item spawns with its MAGAZINE pocket and no debugmsg
        std::string captured;
        captured = capture_debugmsg_during([&]() {
            item it(row.id, calendar::turn);
            CHECK(it.type->pockets.size() > 0);
        });
        CHECK(captured.empty());
        spawned++;
    }
    std::cout << "p3 r2b spawn all: ids " << spawned << std::endl;
    CHECK(spawned == 73);
}
