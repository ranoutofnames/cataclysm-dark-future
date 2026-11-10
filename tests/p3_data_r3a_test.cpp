#include "calendar.h"
#include "catch/catch.hpp"
#include "debug.h"
#include "item.h"
#include "itype.h"
#include "json.h"
#include "pocket_data.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"

#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

// CDF: P3-7-R3a. Batch inputs: tests/data/dda_staged/p3_batches/7-R3a.json and neg-7-R3a.json
// (the generic cases B1-B5 are in tests/p3_batch_generic_test.cpp; the golden load is
// "p3 golden load batches"). This file pins the loaded MAGAZINE_WELL of the 155 guns and 3
// gunmods of the packet.
//
// The unported tree still answers most value samples: src/item_factory.cpp:1155-1200 synthesizes
// an equivalent well from the legacy `magazines` / `magazine_well` keys. S1, S2 and S3 therefore
// also pin what the port itself changes (the well volume of a legacy well the synthesizer treats
// as rigid; the legacy `magazines` table that reload still falls back to; the default magazine,
// which the loader takes from the first `item_restriction` id, src/pocket_data.cpp:74-78). Those
// checks are red on the unported data and green after the port; the plain value checks pin the
// values the port must keep.

namespace {

const std::string R3A_BATCH = "tests/data/dda_staged/p3_batches/7-R3a.json";

struct r3a_row {
    std::string id;
    std::string file;
    bool has_well = false;
    bool has_default = false;
    std::string default_magazine;
    int well_ml = 0;
    std::vector<std::string> restriction;
};

auto parse_ml(const std::string& text) -> int {
    const size_t split = text.find(' ');
    REQUIRE(split != std::string::npos);
    const int number = std::stoi(text.substr(0, split));
    const std::string unit = text.substr(split + 1);
    if (unit == "ml") { return number; }
    if (unit == "L") { return number * 1000; }
    FAIL("unexpected volume string: " << text);
    return 0;
}

auto read_r3a_batch() -> std::vector<r3a_row> {
    std::ifstream file(R3A_BATCH, std::ios::binary);
    REQUIRE(file.good());
    JsonIn jsin(file);
    JsonObject root = jsin.get_object();
    root.allow_omitted_members();
    JsonObject items = root.get_object("items");
    items.allow_omitted_members();
    std::vector<r3a_row> out;
    for (const JsonMember member : items) {
        JsonObject row = member.get_object();
        row.allow_omitted_members();
        r3a_row r;
        r.id = member.name();
        r.file = row.get_string("file");
        for (JsonObject pocket : row.get_array("pockets")) {
            pocket.allow_omitted_members();
            if (pocket.get_string("pocket_type") != "MAGAZINE_WELL") { continue; }
            r.has_well = true;
            if (pocket.has_string("magazine_well")) {
                r.well_ml = parse_ml(pocket.get_string("magazine_well"));
            }
            if (pocket.has_string("default_magazine")) {
                r.has_default = true;
                r.default_magazine = pocket.get_string("default_magazine");
            }
            for (const std::string& id : pocket.get_string_array("item_restriction")) {
                r.restriction.push_back(id);
            }
        }
        out.push_back(r);
    }
    return out;
}

// The one MAGAZINE_WELL pocket of a loaded item (the loader can add a MOD pocket too).
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

auto has_id(const pocket_data& pocket, const std::string& id) -> bool {
    return pocket.item_id_restriction.count(itype_id(id)) == 1;
}

// A default or built-in conversion mod answers magazine_default()/magazine_compatible() from its
// own adaptor (guard G, src/item.cpp:9554-9590), not from the item's well.
auto has_conversion_mod(const item& it) -> bool {
    if (it.type->gun == nullptr) { return false; }
    const auto is_conversion = [](const itype_id& mod_id) {
        const itype& mod = mod_id.obj();
        return mod.mod && (!mod.mod->ammo_modifier.empty() || !mod.mod->magazine_adaptor.empty());
    };
    for (const itype_id& id : it.type->gun->built_in_mods) {
        if (is_conversion(id)) { return true; }
    }
    for (const itype_id& id : it.type->gun->default_mods) {
        if (is_conversion(id)) { return true; }
    }
    return false;
}

} // namespace

TEST_CASE("p3 r3a samples", "[p3][data][R3a][r3a]") {
    // CDF: data/json/items/gun/9mm.json glock_19 `magazines` / `magazine_well`; the golden keeps
    // the CDF-only kriss_vector_9mm_mag and drops the DDA ids glockmag_10 / glock17_10.
    const pocket_data& glock = well_pocket("glock_19");
    CHECK(glock.magazine_well == 250_ml);
    CHECK(glock.item_id_restriction.size() == 7);
    for (const std::string& id : {"glockmag", "glockbigmag", "glock17_17", "glock17_22",
                                  "glock_drum_50rd", "glock_drum_100rd", "kriss_vector_9mm_mag"}) {
        CAPTURE(id);
        CHECK(has_id(glock, id));
    }
    CHECK_FALSE(has_id(glock, "glockmag_10"));
    CHECK_FALSE(has_id(glock, "glock17_10"));

    // CDF: data/json/items/gun/223.json m4a1: 14 legacy magazine ids, well 150 ml, first id stanag30
    const pocket_data& m4 = well_pocket("m4a1");
    CHECK(m4.magazine_well == 150_ml);
    CHECK(m4.item_id_restriction.size() == 14);
    CHECK(m4.default_magazine == itype_id("stanag30"));
    CHECK(has_id(m4, "stanag30"));
    CHECK(has_id(m4, "survivor223mag"));

    // CDF: data/json/items/gun/45.json TDI keeps the CDF-only glock21drum40rd; DDA's
    // glock_21mag_10 is not a CDF item and is dropped. The golden well is 500 ml; the legacy
    // synthesizer leaves it 0 because the well is not smaller than the largest magazine
    // (src/item_factory.cpp:1193-1200), so the volume check is red on the unported tree.
    const pocket_data& tdi = well_pocket("TDI");
    CHECK(tdi.magazine_well == 500_ml);
    CHECK(has_id(tdi, "glock21drum40rd"));
    CHECK_FALSE(has_id(tdi, "glock_21mag_10"));
}

TEST_CASE("p3 r3a reload", "[p3][data][R3a][r3a]") {
    clear_all_state();
    item m4("m4a1", calendar::turn);
    item stanag("stanag30", calendar::turn);
    item glockmag("glockmag", calendar::turn);
    item glock("glock_19", calendar::turn);
    item kriss("kriss_vector_9mm_mag", calendar::turn);

    // CDF: the legacy `magazines` table is what the port removes; the live well must answer alone
    // (src/item.cpp:9560-9590 still falls back to it). Red before the port.
    CHECK(m4.type->magazines.empty());
    CHECK(glock.type->magazines.empty());

    CHECK(m4.can_contain(stanag).success());
    CHECK_FALSE(m4.can_contain(glockmag).success());
    CHECK(glock.can_contain(kriss).success());
    // CDF: a magazine of another ammotype is refused by both
    CHECK_FALSE(glock.can_contain(stanag).success());
}

TEST_CASE("p3 r3a default magazine fits", "[p3][data][R3a][r3a]") {
    clear_all_state();
    size_t with_default = 0;
    size_t without_default = 0;
    for (const r3a_row& row : read_r3a_batch()) {
        CAPTURE(row.id);
        REQUIRE(itype_id(row.id).is_valid());
        REQUIRE_FALSE(row.restriction.empty());
        // CDF: the loader takes the well default from the first `item_restriction` id
        // (src/pocket_data.cpp:74-78); the target lists the intended default first on every row.
        const std::string target_default = row.restriction.front();
        if (row.has_default) {
            with_default++;
        } else {
            without_default++;
        }
        item it(row.id, calendar::turn);
        const pocket_data& pocket = well_pocket(row.id);
        // CDF: the default is in the well (Stop condition: a default outside it)
        CHECK(has_id(pocket, target_default));
        if (!it.is_gun() || has_conversion_mod(it)) {
            // CDF: the 3 retool guns install a conversion kit whose adaptor answers
            // magazine_default() (src/item.cpp:9554-9590); the pocket default is checked above
            continue;
        }
        CHECK(it.magazine_default() == itype_id(target_default));
        const itype_id default_ammo = it.ammo_default();
        REQUIRE_FALSE(default_ammo.is_null());
        // CDF: the default magazine spawns in the well without a debugmsg
        const std::string captured = capture_debugmsg_during([&]() {
            it.ammo_set(default_ammo);
        });
        CHECK(captured.empty());
        REQUIRE(it.magazine_current() != nullptr);
        CHECK(it.magazine_current()->typeId() == itype_id(target_default));
    }
    std::cout << "p3 r3a default magazine: rows " << with_default + without_default
              << " with default " << with_default << " without " << without_default << std::endl;
    // CDF: the target's own counts (76 AUTHOR rows carry a default; the other 82 do not)
    CHECK(with_default == 76);
    CHECK(without_default == 82);
}

TEST_CASE("p3 r3a spawn all", "[p3][data][R3a][r3a]") {
    clear_all_state();
    const std::vector<r3a_row> rows = read_r3a_batch();
    REQUIRE(rows.size() == 158);
    size_t spawned = 0;
    for (const r3a_row& row : rows) {
        CAPTURE(row.id);
        REQUIRE(itype_id(row.id).is_valid());
        item it(row.id, calendar::turn);
        // CDF: CDF gunmods carry `gun_data`, so is_gun() is true for all 158 (the 3 gunmods too)
        CHECK(it.is_gun());
        REQUIRE(row.has_well);
        const pocket_data& pocket = well_pocket(row.id);
        // CDF: the golden `magazine_well` volume; 0 for the nine rows the golden leaves without it
        CHECK(pocket.magazine_well == units::from_milliliter(row.well_ml));
        spawned++;
    }
    std::cout << "p3 r3a spawn all: ids " << spawned << std::endl;
    CHECK(spawned == 158);
}

// CDF: ammo_set's "default magazine too small" fallback must read the live well (magazine_compatible),
// not the legacy `magazines` map the port removes. Red before the src/item.cpp edit: these guns
// debugmsg "doesn't have a magazine for ..." (found by the item group checks of the 7-R3a port).
TEST_CASE("p3 r3a ammo_set fallback uses the well", "[p3][data][R3a][r3a][ammo_set]") {
    clear_all_state();
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"j22", "22_cphp"}, {"hi_power_9mm", "9mmP2"}, {"walther_ppq_40", "40sw"}, {"hk417_13", "308"}};
    for (const auto& c : cases) {
        CAPTURE(c.first, c.second);
        item gun(c.first, calendar::turn);
        REQUIRE(gun.type->magazines.empty());
        const std::string dmsg = capture_debugmsg_during([&]() { gun.ammo_set(itype_id(c.second), 1000); });
        CHECK(dmsg.empty());
        CHECK(gun.magazine_current() != nullptr);
    }
}
