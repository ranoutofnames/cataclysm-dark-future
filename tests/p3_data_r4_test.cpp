#include "catch/catch.hpp"
#include "calendar.h"
#include "item.h"
#include "itype.h"
#include "json.h"
#include "pocket_data.h"

#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

// CDF: P3-7-R4a. Batch input: tests/data/dda_staged/p3_batches/7-R4a.json (64 rows of items/tool).
// The golden load of the same ids (pockets, order) is "p3 golden load batches".

namespace {

const std::string R4A_BATCH = "tests/data/dda_staged/p3_batches/7-R4a.json";

struct r4a_row {
    std::string id;
    bool has_max_charges = false;
    int max_charges = 0;
    std::vector<std::string> ammo;
};

auto read_r4a_batch() -> std::vector<r4a_row> {
    std::ifstream file(R4A_BATCH, std::ios::binary);
    JsonIn jsin(file);
    JsonObject root = jsin.get_object();
    root.allow_omitted_members();
    JsonObject items = root.get_object("items");
    items.allow_omitted_members();
    std::vector<r4a_row> out;
    for (const JsonMember member : items) {
        JsonObject row = member.get_object();
        row.allow_omitted_members();
        r4a_row r;
        r.id = member.name();
        JsonObject legacy = row.get_object("legacy");
        legacy.allow_omitted_members();
        if (legacy.has_int("max_charges")) {
            r.has_max_charges = true;
            r.max_charges = legacy.get_int("max_charges");
        }
        if (legacy.has_string("ammo")) {
            r.ammo.push_back(legacy.get_string("ammo"));
        } else if (legacy.has_array("ammo")) {
            for (const std::string s : legacy.get_array("ammo")) { r.ammo.push_back(s); }
        }
        out.push_back(r);
    }
    return out;
}

// the integral MAGAZINE pocket of a tool, as the loader (authored or synthesized) leaves it
auto integral_pocket(const std::string& id) -> const pocket_data& {
    const itype& t = itype_id(id).obj();
    // CDF: a legacy-synthesized tool (adv_UPS_off) also carries a MOD pocket; the integral MAGAZINE
    // pocket is the one that must be unique
    const pocket_data* found = nullptr;
    int mag_pockets = 0;
    for (const pocket_data& p : t.pockets) {
        if (p.type == pocket_type::MAGAZINE) {
            found = &p;
            ++mag_pockets;
        }
    }
    REQUIRE(mag_pockets == 1);
    return *found;
}

void check_tool_pocket(const std::string& id, const std::map<std::string, int>& expected) {
    CAPTURE(id);
    const pocket_data& p = integral_pocket(id);
    REQUIRE(p.ammo_restriction.size() == expected.size());
    for (const auto& [ammo, cap] : expected) {
        CAPTURE(ammo);
        REQUIRE(p.ammo_restriction.count(ammotype(ammo)) == 1);
        CHECK(p.ammo_restriction.at(ammotype(ammo)) == cap);
    }
}

} // namespace

TEST_CASE("p3 r4a charges literals", "[p3][data][R4]") {
    const std::vector<r4a_row> rows = read_r4a_batch();
    REQUIRE(rows.size() == 64);
    for (const r4a_row& r : rows) {
        CAPTURE(r.id);
        REQUIRE(itype_id(r.id).is_valid());
        const itype& t = itype_id(r.id).obj();
        REQUIRE(t.tool);
        if (!r.has_max_charges) {
            INFO("no legacy max_charges (inherited or derived only): " << r.id);
            continue;
        }
        // CDF: the number the item had before the port (batch `legacy.max_charges`),
        // now derived by P3-7-PRE from the pocket; adv_UPS_off (parked) must still be 5000
        CHECK(t.tool->max_charges == r.max_charges);
        for (const std::string& a : r.ammo) {
            CAPTURE(a);
            CHECK(t.tool->ammo_id.contains(ammotype(a)));
        }
    }
}

TEST_CASE("p3 r4a spawn is behaviour neutral", "[p3][data][R4]") {
    struct spawn_row {
        std::string id;
        int capacity;
        int charges;
        std::string ammo;
    };
    const std::vector<spawn_row> spawns = {
        // CDF: items/tool/*.json max_charges before the port
        {"lighter", 25, 10, "lighter_fluid"},
        {"laptop", 700, 500, "battery"},
        {"char_smoker", 200, 100, "charcoal"},
        {"sewing_machine", 800, 50, "thread"},
        {"wrist_comp_control", 600, 500, "battery"},
        {"elec_shears", 100, 100, "battery"},
        {"adv_UPS_off", 5000, 5000, "battery"},
    };
    for (const spawn_row& s : spawns) {
        CAPTURE(s.id);
        item it(s.id, calendar::turn, s.charges);
        CHECK(!it.ammo_types().empty());
        // CDF: the ammo type the tool took before the port (items/tool/*.json tool_ammo), exactly one
        CHECK(it.ammo_types() == std::set<ammotype>{ammotype(s.ammo)});
        REQUIRE(it.type->tool);
        CHECK(it.type->tool->ammo_id == std::set<ammotype>{ammotype(s.ammo)});
        CHECK(it.ammo_remaining() == s.charges);
        CHECK(it.ammo_capacity() == s.capacity);
    }
}

TEST_CASE("p3 r4a samples", "[p3][data][R4]") {
    // CDF: items/tool/electronics.json laptop max_charges 700 (DDA has 200)
    check_tool_pocket("laptop", {{"battery", 700}});
    // CDF: items/tool/cooking.json char_smoker max_charges 200 (DDA 2000)
    check_tool_pocket("char_smoker", {{"charcoal", 200}});
    // CDF: items/tool/fishing.json fish_trap max_charges 20 (DDA 5)
    check_tool_pocket("fish_trap", {{"fish_bait", 20}});
    // CDF: items/tool/fire.json lighter keeps lighter_fluid 25 (DDA says butane)
    check_tool_pocket("lighter", {{"lighter_fluid", 25}});
    // CDF: wrist_comp_budget 600, integral (DDA class shape: smart_watch_adv, 30)
    check_tool_pocket("wrist_comp_budget", {{"battery", 600}});
    // CDF: elec_shears 100, integral until the DDA cable-power port
    check_tool_pocket("elec_shears", {{"battery", 100}});
}

TEST_CASE("p3 r4a dropped ids", "[p3][data][R4]") {
    // CDF: DDA's `coal` ammotype does not exist in CDF; it is dropped from the pockets
    for (const std::string id : {"char_forge", "charcoal_cooker"}) {
        CAPTURE(id);
        const pocket_data& p = integral_pocket(id);
        CHECK(p.ammo_restriction.count(ammotype("coal")) == 0);
        CHECK(p.ammo_restriction.count(ammotype("charcoal")) == 1);
    }
    // CDF: spray_can keeps spray_paint; DDA's aerosol_paint is not a CDF ammotype
    const pocket_data& spray = integral_pocket("spray_can");
    CHECK(spray.ammo_restriction.count(ammotype("spray_paint")) == 1);
    CHECK(spray.ammo_restriction.count(ammotype("aerosol_paint")) == 0);
}

TEST_CASE("p3 r4a parked class-well row is unchanged", "[p3][data][R4]") {
    // CDF: items/tool/electronics.json adv_UPS_off max_charges 5000 at the base; the DDA well (1500) is parked
    const itype& t = itype_id("adv_UPS_off").obj();
    // CDF: a tool that takes ammo also gets the synthesized MOD pocket (item_factory), so the row is
    // "exactly one MAGAZINE pocket and no well", not "exactly one pocket"
    int mag_pockets = 0;
    for (const pocket_data& p : t.pockets) {
        CHECK(p.type != pocket_type::MAGAZINE_WELL);
        mag_pockets += p.type == pocket_type::MAGAZINE ? 1 : 0;
    }
    CHECK(mag_pockets == 1);
    check_tool_pocket("adv_UPS_off", {{"battery", 5000}});
    item it("adv_UPS_off");
    CHECK(it.ammo_remaining() == 5000);
}
