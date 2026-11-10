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

// CDF: P3-7-R4a2. The 14 rows whose capacity moved to DDA (AUTO-239 Q-DRAIN, AUTO-233 #16): the
// 13 integral ones carry the DDA twin number; adv_UPS_off is a DDA well. Their charges literals are
// covered by the "p3 r4a2" cases instead of the before-port batch literals.
const std::map<std::string, int> R4A2_NUMBERS = {
    {"camera_pro", 100},             // CDF: 200 before 7-R4a2
    {"eink_tablet_pc", 85},          // CDF: 200
    {"elec_jackhammer", 7920},       // CDF: 10000
    {"laptop", 200},                 // CDF: 700
    {"laptop_screen_lit", 200},      // CDF: 700 (copy-from laptop)
    {"mp3", 50},                     // CDF: 200
    {"mp3_on", 50},                  // CDF: 200
    {"portable_game", 48},           // CDF: 200
    {"smart_phone", 56},             // CDF: 200
    {"smart_phone_flashlight", 56},  // CDF: 200
    {"laptop_music", 200},           // CDF: 700, DDA number of the base item laptop
    {"eink_tablet_pc_music", 85},    // CDF: 200, base item eink_tablet_pc
    {"smart_phone_music", 56},       // CDF: 200, base item smart_phone
};

const std::vector<std::string> WRIST_ROWS = {
    "wrist_comp_budget", "wrist_comp_budget_music", "wrist_comp_budget_flashlight",
    "wrist_comp_control", "wrist_comp_control_music", "wrist_comp_control_flashlight",
};

// the battery number of the integral MAGAZINE entry of a batch row's `pockets`
auto batch_integral_number(const std::string& id) -> int {
    std::ifstream file(R4A_BATCH, std::ios::binary);
    JsonIn jsin(file);
    JsonObject root = jsin.get_object();
    root.allow_omitted_members();
    JsonObject items = root.get_object("items");
    items.allow_omitted_members();
    JsonObject row = items.get_object(id);
    row.allow_omitted_members();
    for (JsonObject p : row.get_array("pockets")) {
        p.allow_omitted_members();
        if (p.get_string("pocket_type") == "MAGAZINE") {
            return p.get_object("ammo_restriction").get_int("battery");
        }
    }
    return -1;
}

// which of `keys` the own record of `id` in a data file carries itself
auto own_keys_present(const std::string& file_name, const std::string& id,
                      const std::vector<std::string>& keys) -> std::vector<std::string> {
    std::ifstream file(file_name, std::ios::binary);
    JsonIn jsin(file);
    std::vector<std::string> found;
    bool seen = false;
    for (JsonObject jo : jsin.get_array()) {
        jo.allow_omitted_members();
        if (!jo.has_string("id") || jo.get_string("id") != id || jo.has_string("abstract")) {
            continue;
        }
        seen = true;
        for (const std::string& k : keys) {
            if (jo.has_member(k)) { found.push_back(k); }
        }
    }
    REQUIRE(seen);
    return found;
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
        if (R4A2_NUMBERS.count(r.id) == 1 || r.id == "adv_UPS_off") {
            // CDF: 7-R4a2 moved these 14 rows to the DDA number / well: "p3 r4a2" cases
            continue;
        }
        if (!r.has_max_charges) {
            INFO("no legacy max_charges (inherited or derived only): " << r.id);
            continue;
        }
        // CDF: the number the item had before the port (batch `legacy.max_charges`),
        // now derived by P3-7-PRE from the pocket (the other 50 rows)
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
        {"laptop", 200, 200, "battery"},   // CDF: 700 before 7-R4a2 (now the DDA twin number)
        {"char_smoker", 200, 100, "charcoal"},
        {"sewing_machine", 800, 50, "thread"},
        {"wrist_comp_control", 600, 500, "battery"},
        {"elec_shears", 100, 100, "battery"},
        // CDF: adv_UPS_off is a DDA well since 7-R4a2 (1500 from the default cell, was integral 5000):
        // see "p3 r4a2 adv_UPS_off has the DDA well"
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
    // CDF: items/tool/electronics.json laptop max_charges 700 before 7-R4a2, now the DDA number 200
    check_tool_pocket("laptop", {{"battery", 200}});
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

TEST_CASE("p3 r4a2 adv_UPS_off has the DDA well", "[p3][data][R4]") {
    // DDA UPS_off (data/json/items/tool/electronics.json:1046): BATTERY_HEAVY well, default
    // heavy_plus_battery_cell. CDF: the cell is CDF's 1500 (DDA 503), no rescale (AUTO-236)
    const itype& t = itype_id("adv_UPS_off").obj();
    int wells = 0;
    int integral = 0;
    for (const pocket_data& p : t.pockets) {
        if (p.type == pocket_type::MAGAZINE_WELL) {
            ++wells;
            CHECK(p.default_magazine == itype_id("heavy_plus_battery_cell"));
            CHECK(p.get_flag_restrictions().size() == 1);
            CHECK(p.get_flag_restrictions().count(flag_id("BATTERY_HEAVY")) == 1);
        }
        // CDF: a tool also gets a MOD pocket; only the integral MAGAZINE pocket must be gone
        integral += p.type == pocket_type::MAGAZINE ? 1 : 0;
    }
    CHECK(wells == 1);
    CHECK(integral == 0);
    CHECK(t.has_flag(flag_id("IS_UPS")));
    CHECK(!t.has_flag(flag_id("NO_RELOAD")));
    CHECK(!t.has_flag(flag_id("NO_UNLOAD")));
    CHECK(t.has_flag(flag_id("LEAK_DAM")));
    CHECK(t.has_flag(flag_id("RADIOACTIVE")));
    item it("adv_UPS_off", calendar::turn, 1500);
    // CDF: an unloaded well tool reports the capacity of its default cell (heavy_plus 1500)
    CHECK(it.ammo_capacity(true) == 1500);
    CHECK(it.ammo_remaining() == 1500);
    // CDF: the ammo type comes from the default cell (items/battery.json), not from a tool `ammo` key
    REQUIRE(itype_id("heavy_plus_battery_cell").obj().magazine);
    CHECK(itype_id("heavy_plus_battery_cell").obj().magazine->type ==
          std::set<ammotype>{ammotype("battery")});
    // CDF: the BN keys that made the capacity (electronics.json before 7-R4a2: ammo, max 5000)
    CHECK(own_keys_present("data/json/items/tool/electronics.json", "adv_UPS_off",
                           {"ammo", "tool_ammo", "max_charges", "initial_charges"}).empty());
}

TEST_CASE("p3 r4a2 twin integral numbers", "[p3][data][R4]") {
    for (const auto& [id, n] : R4A2_NUMBERS) {
        CAPTURE(id, n);
        check_tool_pocket(id, {{"battery", n}});
        CHECK(batch_integral_number(id) == n);
        CHECK(integral_pocket(id).rigid);
        item it(id, calendar::turn, n);
        CHECK(it.ammo_capacity() == n);
    }
}

TEST_CASE("p3 r4a2 initial charges fit", "[p3][data][R4]") {
    const std::vector<r4a_row> rows = read_r4a_batch();
    int checked = 0;
    for (const r4a_row& r : rows) {
        if (R4A2_NUMBERS.count(r.id) == 0) {
            continue;
        }
        ++checked;
        CAPTURE(r.id);
        const itype& t = itype_id(r.id).obj();
        REQUIRE(t.tool);
        const int cap = R4A2_NUMBERS.at(r.id);
        // CDF: the effective default charges (own or inherited initial_charges) fit the integral
        // capacity; elec_jackhammer keeps 5000 <= 7920
        CHECK(t.tool->def_charges <= cap);
        item spawn(r.id, calendar::turn);
        CHECK(spawn.ammo_remaining() <= spawn.ammo_capacity());
        CHECK(spawn.ammo_capacity() == cap);
        // a spawn with the before-port initial_charges of the batch (500 / 160 / ...) never
        // reports more than the capacity
        if (r.has_max_charges) {
            item legacy(r.id, calendar::turn, r.max_charges);
            CHECK(legacy.ammo_remaining() <= legacy.ammo_capacity());
        }
    }
    CHECK(checked == 13);
    // CDF: the before-port literals stay in the batch (laptop max_charges 700)
    for (const r4a_row& r : rows) {
        if (r.id == "laptop") {
            CHECK(r.has_max_charges);
            CHECK(r.max_charges == 700);
        }
    }
    // CDF: the six parents spawn with their old fill ratio of the new number (160/200, 500/700 before
    // 7-R4a2): electronics.json initial_charges 80 / 68 / 143 / 40 / 38 / 45
    const std::map<std::string, int> filled = {{"camera_pro", 80}, {"eink_tablet_pc", 68},
        {"laptop", 143}, {"mp3", 40}, {"portable_game", 38}, {"smart_phone", 45}};
    for (const auto& [id, charges] : filled) {
        CAPTURE(id, charges);
        REQUIRE(itype_id(id).obj().tool);
        CHECK(itype_id(id).obj().tool->def_charges == charges);
        CHECK(item(id, calendar::turn).ammo_remaining() == charges);
    }
    // elec_jackhammer keeps 5000 (<= 7920)
    CHECK(itype_id("elec_jackhammer").obj().tool->def_charges == 5000);
}

TEST_CASE("p3 r4a2 wrist rows stay 600", "[p3][data][R4]") {
    // AUTO-239 Q-WRIST: CDF keeps 600 (PL-48's alternative 30 is vetoed); the DDA class shape stays
    for (const std::string& id : WRIST_ROWS) {
        CAPTURE(id);
        check_tool_pocket(id, {{"battery", 600}});
        CHECK(batch_integral_number(id) == 600);
        CHECK(integral_pocket(id).rigid);
    }
}

TEST_CASE("p3 r4a2 out-of-packet rows and cells unchanged", "[p3][data][R4]") {
    // CDF: the 10 G1 rows are 7-R4b's: integral battery number as at the base
    check_tool_pocket("scouter", {{"battery", 200}});
    check_tool_pocket("scouter_nv_on", {{"battery", 200}});
    check_tool_pocket("mil_scouter", {{"battery", 200}});
    check_tool_pocket("mil_scouter_nv_on", {{"battery", 200}});
    for (const std::string id : {"exosuit_survivor", "exosuit_survivor_on", "exosuit_military",
                                 "exosuit_military_on", "exosuit_industrial",
                                 "exosuit_industrial_on"}) {
        check_tool_pocket(id, {{"battery", 500}});
    }
    // CDF: laptop_boris (copy-from laptop) is 7-R4b's: its own record has no pocket here
    CHECK(own_keys_present("data/json/npcs/refugee_center/surface_refugees/NPC_Boris_Borichenko.json",
                           "laptop_boris", {"pocket_data", "max_charges"}).empty());
    // CDF: the 11 BATT2 rows keep their battery well until BATT2
    for (const std::string id : {"camera", "cell_phone", "cell_phone_flashlight", "vibrator",
                                 "electric_lighter", "electric_lantern", "electric_lantern_on",
                                 "reading_light", "reading_light_on", "smart_lamp",
                                 "smart_lamp_on"}) {
        CAPTURE(id);
        int wells = 0;
        int integral = 0;
        for (const pocket_data& p : itype_id(id).obj().pockets) {
            wells += p.type == pocket_type::MAGAZINE_WELL ? 1 : 0;
            integral += p.type == pocket_type::MAGAZINE ? 1 : 0;
        }
        CHECK(wells == 1);
        CHECK(integral == 0);
    }
    // CDF: fake_firestarter keeps max_charges 10000 until 7-R4b; elec_shears waits for the cable port
    REQUIRE(itype_id("fake_firestarter").obj().tool);
    CHECK(itype_id("fake_firestarter").obj().tool->max_charges == 10000);
    check_tool_pocket("elec_shears", {{"battery", 100}});
    // AUTO-236: CDF cell sizes, no rescale (DDA 16 / 259 / 503)
    const std::map<std::string, int> cells = {{"light_battery_cell", 100},
        {"medium_battery_cell", 500}, {"heavy_battery_cell", 1000},
        {"heavy_plus_battery_cell", 1500}};
    for (const auto& [id, cap] : cells) {
        CAPTURE(id);
        REQUIRE(itype_id(id).obj().magazine);
        CHECK(itype_id(id).obj().magazine->capacity == cap);
    }
}
