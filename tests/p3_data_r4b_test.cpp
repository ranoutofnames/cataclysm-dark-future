#include "catch/catch.hpp"
#include "calendar.h"
#include "item.h"
#include "itype.h"
#include "json.h"
#include "pocket_data.h"
#include "itype_slots/islot_tool.h"
#include "itype_slots/islot_magazine.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// CDF: P3-7-R4b. The 10 class-well rows (4 scouters, 6 exosuits) take the DDA class well of their
// analog (goggles_nv, robofac_resonance_suit): MAGAZINE_WELL, flag_restriction and default_magazine
// from the analog, NO_RELOAD/NO_UNLOAD dropped. The cells keep CDF sizes (AUTO-236: light 100,
// heavy 1000). laptop_boris inherits the DDA integral number 200 from laptop. fake_firestarter
// loses its battery pocket (DDA twin has none, USES_BIONIC_POWER).

namespace {

struct well_row {
    std::set<std::string> flags;
    std::string cell;
    int capacity;
    std::vector<pocket_type> shape; // non-MOD pockets in order
};

const std::set<std::string> LIGHT = {"BATTERY_LIGHT", "BATTERY_ULTRA_LIGHT"};
const std::set<std::string> HEAVY = {"BATTERY_HEAVY"};
using pt = pocket_type;

// CDF: capacity = the default cell's CDF `capacity` (light 100, heavy 1000); shapes as the golden
const std::map<std::string, well_row> WELLS = {
    {"scouter", {LIGHT, "light_battery_cell", 100, {pt::MAGAZINE_WELL}}},
    {"scouter_nv_on", {LIGHT, "light_battery_cell", 100, {pt::MAGAZINE_WELL}}},
    {"mil_scouter", {LIGHT, "light_battery_cell", 100, {pt::MAGAZINE_WELL}}},
    {"mil_scouter_nv_on", {LIGHT, "light_battery_cell", 100, {pt::MAGAZINE_WELL}}},
    {"exosuit_survivor", {HEAVY, "heavy_battery_cell", 1000, {pt::MAGAZINE_WELL}}},
    {"exosuit_survivor_on", {HEAVY, "heavy_battery_cell", 1000, {pt::MAGAZINE_WELL}}},
    {"exosuit_military", {HEAVY, "heavy_battery_cell", 1000, {pt::MAGAZINE_WELL, pt::CONTAINER}}},
    {"exosuit_military_on", {HEAVY, "heavy_battery_cell", 1000, {pt::MAGAZINE_WELL, pt::CONTAINER}}},
    {"exosuit_industrial", {HEAVY, "heavy_battery_cell", 1000,
                            {pt::MAGAZINE_WELL, pt::CONTAINER, pt::CONTAINER}}},
    {"exosuit_industrial_on", {HEAVY, "heavy_battery_cell", 1000,
                               {pt::MAGAZINE_WELL, pt::CONTAINER, pt::CONTAINER}}},
};

auto count_type(const std::string& id, pocket_type t) -> int {
    int n = 0;
    for (const pocket_data& p : itype_id(id).obj().pockets) { n += p.type == t ? 1 : 0; }
    return n;
}

auto containers(const std::string& id) -> std::vector<const pocket_data*> {
    std::vector<const pocket_data*> c;
    for (const pocket_data& p : itype_id(id).obj().pockets) {
        if (p.type == pocket_type::CONTAINER) { c.push_back(&p); }
    }
    return c;
}

} // namespace

TEST_CASE("p3 r4b snapshot", "[.][r4bsnap]") {
    for (const auto& [id, w] : WELLS) {
        const itype& t = itype_id(id).obj();
        std::ostringstream os;
        for (const pocket_data& p : t.pockets) { os << static_cast<int>(p.type) << " "; }
        item it(id, calendar::turn);
        WARN(id << " pockets: " << os.str() << " cap=" << it.ammo_capacity(true)
             << " remaining=" << it.ammo_remaining()
             << " NO_RELOAD=" << t.has_flag(flag_id("NO_RELOAD"))
             << " NO_UNLOAD=" << t.has_flag(flag_id("NO_UNLOAD")));
    }
}

TEST_CASE("p3 r4b 10 class wells", "[p3][data][R4b]") {
    for (const auto& [id, w] : WELLS) {
        CAPTURE(id);
        const itype& t = itype_id(id).obj();
        // CDF: one well, no integral MAGAZINE pocket
        CHECK(count_type(id, pocket_type::MAGAZINE_WELL) == 1);
        CHECK(count_type(id, pocket_type::MAGAZINE) == 0);
        // CDF: the well is pocket 0 of the non-MOD pockets, the storage pockets follow (rule R9)
        std::vector<pocket_type> shape;
        for (const pocket_data& p : t.pockets) {
            if (p.type != pocket_type::MOD) { shape.push_back(p.type); }
        }
        CHECK(shape == w.shape);
        for (const pocket_data& p : t.pockets) {
            if (p.type != pocket_type::MAGAZINE_WELL) { continue; }
            CHECK(p.default_magazine == itype_id(w.cell));
            CHECK(p.get_flag_restrictions().size() == w.flags.size());
            for (const std::string& f : w.flags) {
                CHECK(p.get_flag_restrictions().count(flag_id(f)) == 1);
            }
        }
        // DDA wells carry no NO_RELOAD / NO_UNLOAD: the cell must be swappable
        CHECK(!t.has_flag(flag_id("NO_RELOAD")));
        CHECK(!t.has_flag(flag_id("NO_UNLOAD")));
        // CDF: the capacity is the default cell's CDF capacity (AUTO-236), not DDA's 16 / 259
        REQUIRE(itype_id(w.cell).obj().magazine);
        CHECK(itype_id(w.cell).obj().magazine->capacity == w.capacity);
        item it(id, calendar::turn);
        CHECK(it.ammo_capacity(true) == w.capacity);
        // CDF: the BN integral store keys are gone
        REQUIRE(t.tool);
        CHECK(t.tool->max_charges == 0);
    }
}

TEST_CASE("p3 r4b storage pockets follow the well", "[p3][data][R4b]") {
    // CDF: golden storage pockets (tests/data/dda_staged/p3_golden.json): military 7 L carry,
    // industrial 15 L carry and a 10 L holster
    for (const std::string id : {"exosuit_military", "exosuit_military_on"}) {
        CAPTURE(id);
        const auto c = containers(id);
        REQUIRE(c.size() == 1);
        CHECK(c[0]->raw_volume_capacity == 7_liter);
        CHECK(!c[0]->holster);
    }
    for (const std::string id : {"exosuit_industrial", "exosuit_industrial_on"}) {
        CAPTURE(id);
        const auto c = containers(id);
        REQUIRE(c.size() == 2);
        CHECK(c[0]->raw_volume_capacity == 15_liter);
        CHECK(!c[0]->holster);
        CHECK(c[1]->raw_volume_capacity == 10_liter);
        CHECK(c[1]->holster);
    }
}

TEST_CASE("p3 r4b laptop_boris and fake_firestarter", "[p3][data][R4b]") {
    // CDF: laptop_boris copies laptop, which is DDA's integral 200 since 7-R4a2
    CHECK(count_type("laptop_boris", pocket_type::MAGAZINE_WELL) == 0);
    REQUIRE(count_type("laptop_boris", pocket_type::MAGAZINE) == 1);
    for (const pocket_data& p : itype_id("laptop_boris").obj().pockets) {
        if (p.type != pocket_type::MAGAZINE) { continue; }
        REQUIRE(p.ammo_restriction.count(ammotype("battery")) == 1);
        CHECK(p.ammo_restriction.at(ammotype("battery")) == 200);
    }
    item boris("laptop_boris", calendar::turn);
    CHECK(boris.ammo_capacity() == 200);
    CHECK(boris.ammo_remaining() <= 200);
    REQUIRE(itype_id("laptop_boris").obj().tool);
    CHECK(itype_id("laptop_boris").obj().tool->def_charges <= 200);
    // CDF: DDA's fake_firestarter has no pocket: its battery pocket is gone (bionic power runs it)
    const itype& f = itype_id("fake_firestarter").obj();
    CHECK(count_type("fake_firestarter", pocket_type::MAGAZINE) == 0);
    CHECK(count_type("fake_firestarter", pocket_type::MAGAZINE_WELL) == 0);
    REQUIRE(f.tool);
    CHECK(f.tool->max_charges == 0);
    CHECK(f.has_flag(flag_id("USES_BIONIC_POWER")));
}

TEST_CASE("p3 r4b no spawn charges above capacity", "[p3][data][R4b]") {
    // CDF: one-line spawn entries of the 10 wells stay within the capacity of the default cell
    const std::regex entry(R"re(\{[^{}]*"item"\s*:\s*"([a-z_]+)"[^{}]*\})re");
    const std::regex number(R"re("charges(?:-max|-min)?"\s*:\s*(\d+))re");
    int seen = 0;
    for (const auto& de : std::filesystem::recursive_directory_iterator("data/json")) {
        if (!de.is_regular_file() || de.path().extension() != ".json") { continue; }
        std::ifstream file(de.path(), std::ios::binary);
        std::string line;
        while (std::getline(file, line)) {
            for (auto it = std::sregex_iterator(line.begin(), line.end(), entry);
                 it != std::sregex_iterator(); ++it) {
                const std::string id = (*it)[1].str();
                const auto w = WELLS.find(id);
                if (w == WELLS.end()) { continue; }
                ++seen;
                const std::string text = it->str();
                CAPTURE(de.path().string(), text);
                for (auto n = std::sregex_iterator(text.begin(), text.end(), number);
                     n != std::sregex_iterator(); ++n) {
                    CHECK(std::stoi((*n)[1].str()) <= w->second.capacity);
                }
            }
        }
    }
    CHECK(seen > 10);
}
