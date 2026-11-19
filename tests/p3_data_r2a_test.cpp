#include "calendar.h"
#include "catch/catch.hpp"
#include "debug.h"
#include "item.h"
#include "itype.h"
#include "json.h"
#include "pocket_data.h"
#include "state_helpers.h"
#include "type_id.h"
#include "itype_slots/islot_gun.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

// CDF: P3-7-R2a. Batch inputs: tests/data/dda_staged/p3_batches/7-R2a.json and neg-7-R2a.json
// (generic cases B1-B5 in tests/p3_batch_generic_test.cpp; golden load "p3 golden load batches").
// This file pins the loaded MAGAZINE pocket of the 135 integral-magazine guns of items/gun/.
// Unported, src/item_factory.cpp synthesizes an equivalent pocket from the legacy clip_size and
// magazines keys; S1-S3 therefore also pin what the port itself changes (the explicit pocket
// replaces the synthesized one: no legacy magazines table, speedloaders on the pocket itself).

namespace {

const std::string R2A_BATCH = "tests/data/dda_staged/p3_batches/7-R2a.json";

struct r2a_row {
    std::string id;
    std::map<std::string, int> ammo; // CDF: the golden ammo_restriction of the MAGAZINE pocket
    std::vector<std::string> speedloaders;
    bool rigid = false;
};

auto read_r2a_batch() -> std::vector<r2a_row> {
    std::ifstream file(R2A_BATCH, std::ios::binary);
    REQUIRE(file.good());
    JsonIn jsin(file);
    JsonObject root = jsin.get_object();
    root.allow_omitted_members();
    JsonObject items = root.get_object("items");
    items.allow_omitted_members();
    std::vector<r2a_row> out;
    for (const JsonMember member : items) {
        JsonObject row = member.get_object();
        row.allow_omitted_members();
        r2a_row r;
        r.id = member.name();
        for (JsonObject pocket : row.get_array("pockets")) {
            pocket.allow_omitted_members();
            if (pocket.get_string("pocket_type") != "MAGAZINE") { continue; }
            r.rigid = pocket.has_bool("rigid") && pocket.get_bool("rigid");
            JsonObject ammo = pocket.get_object("ammo_restriction");
            ammo.allow_omitted_members();
            for (const JsonMember am : ammo) { r.ammo[am.name()] = am.get_int(); }
            if (pocket.has_array("allowed_speedloaders")) {
                for (const std::string& s : pocket.get_string_array("allowed_speedloaders")) {
                    r.speedloaders.push_back(s);
                }
            }
        }
        out.push_back(r);
    }
    return out;
}

// The one MAGAZINE pocket of a loaded item (the loader can add a MOD pocket too).
auto mag_pocket(const std::string& id) -> const pocket_data& {
    const itype& t = itype_id(id).obj();
    const pocket_data* found = nullptr;
    int count = 0;
    for (const pocket_data& pocket : t.pockets) {
        if (pocket.type == pocket_type::MAGAZINE) {
            found = &pocket;
            count++;
        }
    }
    INFO("item " << id);
    REQUIRE(count == 1);
    return *found;
}

auto cap_of(const pocket_data& p, const std::string& ammo) -> int {
    const auto it = p.ammo_restriction.find(ammotype(ammo));
    return it == p.ammo_restriction.end() ? -1 : it->second;
}

} // namespace

TEST_CASE("p3 r2a samples", "[p3][data][R2a][r2a]") {
    clear_all_state();
    // CDF: data/json/items/gun/38.json sw_619: 7 shots (clip_size 7), .38 and .357;
    // the legacy magazines table [38 -> 38_speedloader] moves to allowed_speedloaders.
    const itype& sw = itype_id("sw_619").obj();
    const pocket_data& swp = mag_pocket("sw_619");
    CHECK(swp.rigid);
    CHECK(swp.ammo_restriction.size() == 2);
    CHECK(cap_of(swp, "357mag") == 7);
    CHECK(cap_of(swp, "38") == 7);
    CHECK(swp.allowed_speedloaders.count(itype_id("38_speedloader")) == 1);
    CHECK(sw.magazines.empty());
    CHECK(sw.gun->clip == 7);

    // CDF: data/json/items/gun/4570.json 1895sbl holds 7 (DDA twin 6): the CDF number stays
    const pocket_data& sbl = mag_pocket("1895sbl");
    CHECK(sbl.rigid);
    CHECK(cap_of(sbl, "4570") == 7);

    // CDF: data/json/items/gun/bio.json bio_shotgun_gun clip_size 8 (DDA twin 1): CDF stays
    const pocket_data& bio = mag_pocket("bio_shotgun_gun");
    CHECK(cap_of(bio, "shot") == 8);

    // CDF: data/json/items/gun/762R.json mosin44: 5 x 762R, clip 762R_clip
    const pocket_data& mosin = mag_pocket("mosin44");
    CHECK(cap_of(mosin, "762R") == 5);
    CHECK(mosin.allowed_speedloaders.count(itype_id("762R_clip")) == 1);
}

TEST_CASE("p3 r2a reload", "[p3][data][R2a][r2a]") {
    clear_all_state();
    item sw("sw_619", calendar::turn);
    // CDF: the legacy magazines table is what the port removes (red before the port)
    CHECK(sw.type->magazines.empty());
    item loose38("38_special", calendar::turn, 6);
    item loose9("9mm", calendar::turn, 6);
    CHECK(sw.can_reload_with(loose38, false));
    CHECK_FALSE(sw.can_reload_with(loose9, false));

    item sl("38_speedloader", calendar::turn);
    sl.put_in(item("38_special", calendar::turn, 6), pocket_type::MAGAZINE);
    REQUIRE(sl.ammo_remaining() == 6);
    CHECK(sw.can_reload_with(sl, false));
    // CDF: a speedloader the pocket does not list is refused
    item sl6("38_speedloader6", calendar::turn);
    sl6.put_in(item("38_special", calendar::turn, 6), pocket_type::MAGAZINE);
    CHECK_FALSE(sw.can_reload_with(sl6, false));

    item mosin("mosin44", calendar::turn);
    CHECK(mosin.type->magazines.empty());
    item clip("762R_clip", calendar::turn);
    clip.put_in(item("762_54R", calendar::turn, 5), pocket_type::MAGAZINE);
    REQUIRE(clip.ammo_remaining() == 5);
    CHECK(mosin.can_reload_with(clip, false));
    CHECK_FALSE(mosin.can_reload_with(loose9, false));
}

TEST_CASE("p3 r2a clip agrees", "[p3][data][R2a][r2a]") {
    clear_all_state();
    size_t checked = 0;
    for (const r2a_row& row : read_r2a_batch()) {
        CAPTURE(row.id);
        REQUIRE(itype_id(row.id).is_valid());
        const itype& t = itype_id(row.id).obj();
        const pocket_data& pocket = mag_pocket(row.id);
        // CDF: the loaded pocket equals the golden ammo_restriction, cap by cap
        CHECK(pocket.ammo_restriction.size() == row.ammo.size());
        for (const auto& a : row.ammo) {
            CAPTURE(a.first);
            CHECK(cap_of(pocket, a.first) == a.second);
        }
        // CDF: rigid as the golden has it (5 rows carry none)
        CHECK(pocket.rigid == row.rigid);
        CHECK(pocket.allowed_speedloaders.size() == row.speedloaders.size());
        for (const std::string& s : row.speedloaders) {
            CHECK(pocket.allowed_speedloaders.count(itype_id(s)) == 1);
        }
        // CDF: the legacy magazines table is gone for every id (reload uses the pocket)
        CHECK(t.magazines.empty());
        // CDF: gun->clip, where the gun sets it, agrees with a single-ammo pocket
        if (t.gun && row.ammo.size() == 1 && t.gun->clip > 0) {
            CHECK(t.gun->clip == row.ammo.begin()->second);
        }
        checked++;
    }
    CHECK(checked == 135);
}

TEST_CASE("p3 r2a spawn all", "[p3][data][R2a][r2a]") {
    clear_all_state();
    size_t spawned = 0;
    for (const r2a_row& row : read_r2a_batch()) {
        CAPTURE(row.id);
        REQUIRE(itype_id(row.id).is_valid());
        const std::string dmsg = capture_debugmsg_during([&]() {
            item it(row.id, calendar::turn);
            const pocket_data& pocket = mag_pocket(row.id);
            int total = 0;
            for (const auto& a : pocket.ammo_restriction) { total = std::max(total, a.second); }
            int want = 0;
            for (const auto& a : row.ammo) { want = std::max(want, a.second); }
            CHECK(total == want);
        });
        CHECK(dmsg.empty());
        spawned++;
    }
    std::cout << "p3 r2a spawn all: ids " << spawned << std::endl;
    CHECK(spawned == 135);
}
