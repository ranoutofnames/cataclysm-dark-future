#include "catch/catch.hpp"
#include "calendar.h"
#include "item.h"
#include "itype.h"
#include "json.h"
#include "pocket_data.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// CDF: P3-BATT2. The 11 W2 tools (camera, cell_phone(_flashlight), vibrator, electric_lighter,
// electric_lantern(_on), reading_light(_on), smart_lamp(_on)) leave the synthesized battery well and
// take the integral MAGAZINE pocket of their DDA twin, at the DDA number (PL-48 `dda_int_cap`,
// AUTO-239; DDA data/json/items/tool/{electronics,fire,lighting}.json). Cells keep CDF sizes.

namespace {

// CDF: the DDA twin's `ammo_restriction { battery: N }`; the CDF default cell was 150/150/100/50/50/150/100
const std::map<std::string, int> BATT2_NUMBERS = {
    {"camera", 50},
    {"cell_phone", 17},
    {"cell_phone_flashlight", 17},
    {"vibrator", 58},
    {"electric_lighter", 30},
    {"electric_lantern", 58},
    {"electric_lantern_on", 58},
    {"reading_light", 13},
    {"reading_light_on", 13},
    {"smart_lamp", 33},
    {"smart_lamp_on", 33},
};

auto pocket_types(const std::string& id) -> std::string {
    std::map<std::string, int> n;
    for (const pocket_data& p : itype_id(id).obj().pockets) {
        n[std::to_string(static_cast<int>(p.type))]++;
    }
    std::ostringstream os;
    for (const auto& [t, c] : n) { os << "type" << t << "x" << c << " "; }
    return os.str();
}

auto count_type(const std::string& id, pocket_type t) -> int {
    int n = 0;
    for (const pocket_data& p : itype_id(id).obj().pockets) { n += p.type == t ? 1 : 0; }
    return n;
}

auto integral_pocket(const std::string& id) -> const pocket_data& {
    const pocket_data* found = nullptr;
    int n = 0;
    for (const pocket_data& p : itype_id(id).obj().pockets) {
        if (p.type == pocket_type::MAGAZINE) { found = &p; ++n; }
    }
    REQUIRE(n == 1);
    return *found;
}

} // namespace

TEST_CASE("p3 batt2 snapshot", "[.][batt2snap]") {
    for (const auto& [id, n] : BATT2_NUMBERS) {
        const itype& t = itype_id(id).obj();
        item it(id, calendar::turn);
        WARN(id << " pockets: " << pocket_types(id) << " def_charges=" << (t.tool ? t.tool->def_charges : -1)
             << " cap=" << it.ammo_capacity() << " remaining=" << it.ammo_remaining()
             << " flags NO_RELOAD=" << t.has_flag(flag_id("NO_RELOAD"))
             << " NO_UNLOAD=" << t.has_flag(flag_id("NO_UNLOAD")));
    }
}

TEST_CASE("p3 batt2 integral pocket numbers", "[p3][data][BATT2]") {
    for (const auto& [id, n] : BATT2_NUMBERS) {
        CAPTURE(id, n);
        // CDF: the well is gone, one integral MAGAZINE pocket at the DDA number
        CHECK(count_type(id, pocket_type::MAGAZINE_WELL) == 0);
        REQUIRE(count_type(id, pocket_type::MAGAZINE) == 1);
        const pocket_data& p = integral_pocket(id);
        CHECK(p.rigid);
        REQUIRE(p.ammo_restriction.size() == 1);
        REQUIRE(p.ammo_restriction.count(ammotype("battery")) == 1);
        CHECK(p.ammo_restriction.at(ammotype("battery")) == n);
        item it(id, calendar::turn, n);
        CHECK(it.ammo_capacity() == n);
        CHECK(it.ammo_capacity(true) == n);
    }
}

TEST_CASE("p3 batt2 pocket list completeness", "[p3][data][BATT2]") {
    // CDF: the pocket list is the before-port list with the well replaced by the integral pocket;
    // the MOD pocket stays (before-snapshot: MAGAZINE_WELL x1 + MOD x1). CDF: LINKUP1e adds the
    // link_up action to these 11 tools, and the loader adds the CABLE pocket beside the MOD one
    // (src/item_factory.cpp add_cable_pocket, DDA item_factory.cpp:4144-4148).
    for (const auto& [id, n] : BATT2_NUMBERS) {
        CAPTURE(id, pocket_types(id));
        CHECK(count_type(id, pocket_type::MOD) == 1);
        CHECK(count_type(id, pocket_type::CABLE) == 1);
        CHECK(itype_id(id).obj().pockets.size() == 3);
    }
}

TEST_CASE("p3 batt2 spawn fits and flags", "[p3][data][BATT2]") {
    for (const auto& [id, n] : BATT2_NUMBERS) {
        CAPTURE(id, n);
        const itype& t = itype_id(id).obj();
        REQUIRE(t.tool);
        CHECK(t.tool->def_charges <= n);
        item spawn(id, calendar::turn);
        CHECK(spawn.ammo_remaining() <= spawn.ammo_capacity());
        CHECK(spawn.ammo_capacity() == n);
        // CDF: DDA integral shape: a tool that cannot swap cells (NO_RELOAD, NO_UNLOAD as DDA)
        CHECK(t.has_flag(flag_id("NO_RELOAD")));
        CHECK(t.has_flag(flag_id("NO_UNLOAD")));
        // CDF: a legacy before-port charge count (the 50-150 of the old cells) clamps to the capacity
        item legacy(id, calendar::turn, 150);
        CHECK(legacy.ammo_remaining() <= legacy.ammo_capacity());
    }
}

TEST_CASE("p3 batt2 no spawn charges above capacity and no cell inside", "[p3][data][BATT2]") {
    // CDF: scans the one-line entries of data/json (itemgroups, mapgen, npcs, professions) that spawn
    // one of the 11 tools: `charges`/`charges-max` stay within the new capacity, and none carries a
    // cell (`contents`, `ammo-item`, `container-item`, `magazine`)
    const std::regex entry(R"re(\{[^{}]*"(?:item|id)"\s*:\s*"([a-z_]+)"[^{}]*\})re");
    const std::regex number(R"re("charges(?:-max|-min)?"\s*:\s*(\d+))re");
    const std::regex cell(R"re("(?:contents|ammo-item|container-item|magazine|magazine-item)")re");
    int seen = 0;
    for (const auto& de : std::filesystem::recursive_directory_iterator("data")) {
        if (!de.is_regular_file() || de.path().extension() != ".json") { continue; }
        std::ifstream file(de.path(), std::ios::binary);
        std::string line;
        while (std::getline(file, line)) {
            for (auto it = std::sregex_iterator(line.begin(), line.end(), entry);
                 it != std::sregex_iterator(); ++it) {
                const std::string text = it->str();
                const std::string id = (*it)[1].str();
                const auto w = BATT2_NUMBERS.find(id);
                if (w == BATT2_NUMBERS.end() || text.find("\"type\"") != std::string::npos) { continue; }
                ++seen;
                CAPTURE(de.path().string(), text);
                CHECK(!std::regex_search(text, cell));
                for (auto n = std::sregex_iterator(text.begin(), text.end(), number);
                     n != std::sregex_iterator(); ++n) {
                    CHECK(std::stoi((*n)[1].str()) <= w->second);
                }
            }
        }
    }
    // CDF: the scan sees the spawns (itemgroups of base data hold dozens)
    CHECK(seen > 20);
    // the cells keep their CDF sizes (AUTO-236)
    REQUIRE(itype_id("light_battery_cell").obj().magazine);
    CHECK(itype_id("light_battery_cell").obj().magazine->capacity == 100);
}
