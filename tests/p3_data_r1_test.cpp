#include "calendar.h"
#include "catch/catch.hpp"
#include "item.h"
#include "item_factory.h"
#include "itype.h"
#include "json.h"
#include "pocket_data.h"
#include "state_helpers.h"
#include "type_id.h"
#include "itype_slots/islot_magazine.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

// CDF: P3-7-R1a and P3-7-R1b. Batch inputs: tests/data/dda_staged/p3_batches/7-R1a.json and 7-R1b.json.
// The golden load of the same ids (pockets, order) is "p3 golden load batches".

namespace {

const std::string R1A_BATCH = "tests/data/dda_staged/p3_batches/7-R1a.json";
const std::string R1B_BATCH = "tests/data/dda_staged/p3_batches/7-R1b.json";

struct r1a_row {
    std::string id;
    bool has_capacity = false;
    int capacity = 0;
    std::vector<std::string> ammo_types;
};

auto read_batch(const std::string& path) -> std::vector<r1a_row> {
    std::ifstream file(path, std::ios::binary);
    JsonIn jsin(file);
    JsonObject root = jsin.get_object();
    root.allow_omitted_members();
    JsonObject items = root.get_object("items");
    items.allow_omitted_members();
    std::vector<r1a_row> out;
    for (const JsonMember member : items) {
        JsonObject row = member.get_object();
        row.allow_omitted_members();
        r1a_row r;
        r.id = member.name();
        JsonObject legacy = row.get_object("legacy");
        legacy.allow_omitted_members();
        if (legacy.has_int("capacity")) {
            r.has_capacity = true;
            r.capacity = legacy.get_int("capacity");
        }
        if (legacy.has_string("ammo_type")) {
            r.ammo_types.push_back(legacy.get_string("ammo_type"));
        } else if (legacy.has_array("ammo_type")) {
            for (const std::string s : legacy.get_array("ammo_type")) { r.ammo_types.push_back(s); }
        }
        out.push_back(r);
    }
    return out;
}

auto only_pocket(const std::string& id) -> const pocket_data& {
    const itype& t = itype_id(id).obj();
    REQUIRE(t.pockets.size() == 1);
    REQUIRE(t.pockets.front().type == pocket_type::MAGAZINE);
    return t.pockets.front();
}

void check_pocket(const std::string& id, const std::map<std::string, int>& expected, bool rigid) {
    CAPTURE(id);
    const pocket_data& p = only_pocket(id);
    CHECK(p.rigid == rigid);
    REQUIRE(p.ammo_restriction.size() == expected.size());
    for (const auto& [ammo, cap] : expected) {
        CAPTURE(ammo);
        REQUIRE(p.ammo_restriction.count(ammotype(ammo)) == 1);
        CHECK(p.ammo_restriction.at(ammotype(ammo)) == cap);
    }
}

} // namespace

TEST_CASE("p3 r1a capacity literals", "[p3][data][R1]") {
    const std::vector<r1a_row> rows = read_batch(R1A_BATCH);
    REQUIRE(rows.size() == 206);
    for (const r1a_row& r : rows) {
        CAPTURE(r.id);
        REQUIRE(itype_id(r.id).is_valid());
        const itype& t = itype_id(r.id).obj();
        REQUIRE(t.magazine);
        const pocket_data& p = only_pocket(r.id);
        if (r.has_capacity) {
            // CDF: the number the item had before the port (batch `legacy.capacity`),
            // now derived by P3-7-PRE from the pocket
            CHECK(t.magazine->capacity == r.capacity);
            for (const std::string& a : r.ammo_types) {
                CAPTURE(a);
                CHECK(p.ammo_restriction.count(ammotype(a)) == 1);
            }
        } else {
            INFO("no legacy capacity (derived only): " << r.id);
            CHECK(t.magazine->capacity > 0);
        }
    }
}

TEST_CASE("p3 r1a samples", "[p3][data][R1]") {
    // CDF: data/json/items/magazine/50.json as50mag capacity 10 (DDA has 5)
    check_pocket("as50mag", {{"50", 10}}, true);
    // CDF: magazine/223.json belt223 capacity 200, not rigid (DDA has 500)
    check_pocket("belt223", {{"223", 200}}, false);
    // CDF: magazine/223.json stanag30 capacity 30, ammo 223 and 300blk
    check_pocket("stanag30", {{"223", 30}, {"300blk", 30}}, true);
    // CDF: magazine/40.json glock40mag keeps 40 and 357sig at 15 (CDF ammo set kept)
    check_pocket("glock40mag", {{"40", 15}, {"357sig", 15}}, true);
    // CDF: magazine/shot.json saiga30mag capacity 30 (DDA 20)
    check_pocket("saiga30mag", {{"shot", 30}}, true);
    // CDF: magazine/223.json ruger5 capacity 5
    check_pocket("ruger5", {{"223", 5}}, true);
}

// A loaded non-rigid magazine is bigger than its empty template (the rounds add volume above
// magazine_well); the m249 well must still take a full belt. Mutation: item_factory.cpp lifts the well cap
// only for a non-rigid magazine (`loose_mag`); without it the full belt is refused.
TEST_CASE("p3 r1a loaded belt fits the m249 well", "[p3][data][R1]") {
    clear_all_state();
    detached_ptr<item> gun = item::spawn(itype_id("m249"), calendar::turn);
    detached_ptr<item> belt = item::spawn(itype_id("belt223"), calendar::turn);
    belt->ammo_set(itype_id("223"), belt->ammo_capacity(ammotype("223")));
    REQUIRE(belt->ammo_remaining() == 200);
    CHECK(gun->can_contain(*belt).success());
}

// The same for every gun and tool: a full loose (non-rigid) magazine of any kind its MAGAZINE_WELL lists fits.
TEST_CASE("p3 r1a loaded loose magazines fit every well", "[p3][data][R1]") {
    clear_all_state();
    size_t checked = 0;
    std::vector<std::string> refused;
    for (const itype *t : item_controller->all()) {
        for (const pocket_data &pd : t->pockets) {
            if (pd.type != pocket_type::MAGAZINE_WELL) {
                continue;
            }
            for (const itype_id &mag : pd.item_id_restriction) {
                if (!mag.is_valid() || !mag->magazine) {
                    continue;
                }
                bool loose = false;
                for (const pocket_data &mp : mag->pockets) {
                    loose |= mp.type == pocket_type::MAGAZINE && !mp.rigid;
                }
                if (!loose) {
                    continue;
                }
                detached_ptr<item> gun = item::spawn(t->get_id(), calendar::turn);
                detached_ptr<item> m = item::spawn(mag, calendar::turn);
                if (m->ammo_types().empty()) {
                    continue;
                }
                m->ammo_set(m->ammo_default(), m->ammo_capacity(*m->ammo_types().begin()));
                checked++;
                if (!gun->can_contain(*m).success()) {
                    refused.push_back(t->get_id().str() + " <- " + mag.str());
                }
            }
        }
    }
    std::string all_refused;
    for (const std::string &r : refused) {
        all_refused += r + "; ";
    }
    INFO("refused: " << refused.size() << ": " << all_refused);
    CHECK(checked > 0);
    // R1c: the six stripper-clip rifles (m1903, sks, mosin91_30, mosin91_30_ebr, mosin44, mosin44_ebr) used to
    // refuse a loaded clip: their wells have no magazine_well volume, so the loose-magazine lift did not apply.
    // Mutation: restoring `&& obj.magazine_well > 0_ml` on the lift in item_factory.cpp brings the six back, and
    // dropping the lift for belts as well makes it twelve.
    CHECK(refused.empty());
}

TEST_CASE("p3 r1b capacity literals", "[p3][data][R1]") {
    const std::vector<r1a_row> rows = read_batch(R1B_BATCH);
    REQUIRE(rows.size() == 28);
    for (const r1a_row& r : rows) {
        CAPTURE(r.id);
        REQUIRE(itype_id(r.id).is_valid());
        const itype& t = itype_id(r.id).obj();
        REQUIRE(t.magazine);
        const pocket_data& p = only_pocket(r.id);
        REQUIRE(r.has_capacity);
        // CDF: the number the item had before the port (batch `legacy.capacity`),
        // now derived by P3-7-PRE from the pocket
        CHECK(t.magazine->capacity == r.capacity);
        for (const std::string& a : r.ammo_types) {
            CAPTURE(a);
            CHECK(p.ammo_restriction.count(ammotype(a)) == 1);
            // CDF: the authored pocket number itself is the legacy capacity (not only the derived field)
            if (r.ammo_types.size() == 1 && p.ammo_restriction.count(ammotype(a)) == 1) {
                CHECK(p.ammo_restriction.at(ammotype(a)) == r.capacity);
            }
        }
    }
    // CDF: literal capacities of the battery cells, from data/json/items/battery.json (DDA numbers in comments)
    const std::map<std::string, int> cells = {
        {"light_battery_cell", 100},       // DDA 16
        {"medium_battery_cell", 500},      // DDA 56
        {"heavy_battery_cell", 1000},      // DDA 259
        {"light_minus_battery_cell", 50},  // DDA 2
    };
    for (const auto& [id, cap] : cells) {
        CAPTURE(id);
        REQUIRE(itype_id(id).is_valid());
        const itype& t = itype_id(id).obj();
        REQUIRE(t.magazine);
        CHECK(t.magazine->capacity == cap);
        const pocket_data& p = only_pocket(id);
        REQUIRE(p.ammo_restriction.count(ammotype("battery")) == 1);
        CHECK(p.ammo_restriction.at(ammotype("battery")) == cap);
    }
}

TEST_CASE("p3 r1b battery samples", "[p3][data][R1]") {
    // CDF: data/json/items/battery.json light_battery_cell capacity 100 (DDA has 16)
    check_pocket("light_battery_cell", {{"battery", 100}}, true);
    // CDF: battery.json medium_battery_cell capacity 500 (DDA 56)
    check_pocket("medium_battery_cell", {{"battery", 500}}, true);
    // CDF: battery.json heavy_battery_cell capacity 1000 (DDA 259)
    check_pocket("heavy_battery_cell", {{"battery", 1000}}, true);
    // CDF: battery.json light_minus_battery_cell capacity 50 (DDA 2)
    check_pocket("light_minus_battery_cell", {{"battery", 50}}, true);
    // CDF: vehicle/battery.json battery_motorbike capacity 600 (DDA 450)
    check_pocket("battery_motorbike", {{"battery", 600}}, true);
    // CDF: vehicle/battery.json small_storage_battery capacity 500 (DDA 1000)
    check_pocket("small_storage_battery", {{"battery", 500}}, true);
    // CDF: vehicle/fuel_storage.json fuel_bunker keeps charcoal 2000, not rigid; DDA's coal is dropped
    check_pocket("fuel_bunker", {{"charcoal", 2000}}, false);
    CHECK(!itype_id("fuel_bunker").obj().pockets.front().ammo_restriction.count(ammotype("coal")));
}
