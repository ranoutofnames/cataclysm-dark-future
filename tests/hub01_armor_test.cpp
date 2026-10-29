#include "ablative_coverage.h"
#include "avatar.h"
#include "bodypart.h"
#include "catch/catch.hpp"
#include "character.h"
#include "flag.h"
#include "item.h"
#include "item_contents.h"
#include "itype.h"
#include "material.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "subbodypart.h"
#include "type_id.h"
#include "units.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>
#include <vector>

// HUB01-GEAR part A. DDA Hub 01 modular armor (data/json/items/armor/robofac_armor.json at
// b4b62254ae2) as CDF data. Literals below are re-derived from data/json/items/armor/
// cdf_hub01_armor.json (the CDF copy), not from DDA tests (DDA has none for these ids).

namespace {

auto put_plate(item &carrier, const std::string &id) -> bool {
    return carrier.try_put_in(item::spawn(itype_id(id))).success();
}

auto plate_ids(item &carrier) -> std::vector<std::string> {
    std::vector<std::string> ids;
    for (const item *plate : ablative::plates(carrier)) {
        ids.push_back(plate->typeId().str());
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

auto refusal_says_taken(item &carrier, const std::string &id) -> bool {
    const ret_val<bool> res = carrier.try_put_in(item::spawn(itype_id(id)));
    return !res.success() && std::string(res.c_str()).find("taken") != std::string::npos;
}

const std::array<std::string, 5> sets = { "basic", "rubber", "nomex", "kevlar", "military" };
const std::array<std::string, 5> slots = { "mantle", "skirt", "vambraces", "greaves", "helmet" };

auto piece_id(const std::string &set, const std::string &slot) -> std::string {
    return "robofac_" + set + "_" + slot;
}

auto spawn_piece(const std::string &set, const std::string &slot) -> detached_ptr<item> {
    return item::spawn(itype_id(piece_id(set, slot)));
}

} // namespace

TEST_CASE("hub01_flags_and_materials_exist", "[armor][ablative][hub01]") {
    // CDF: DDA flags.json:49, 79, 85 and :1562; materials.json:18, 1341, 2059
    CHECK(flag_id("ABLATIVE_MANTLE").is_valid());
    CHECK(flag_id("ABLATIVE_SKIRT").is_valid());
    CHECK(flag_id("ABLATIVE_HELMET").is_valid());
    CHECK(flag_id("PORTAL_PROOF").is_valid());
    CHECK(material_id("qt_steel").is_valid());
    CHECK(material_id("kevlar_layered").is_valid());
    CHECK(material_id("thermo_resin").is_valid());
    // CDF: kevlar_layered is copy-from kevlar (bullet_resist 6, data/json/materials.json)
    CHECK(material_id("kevlar_layered")->bullet_resist() > 0);
    CHECK(material_id("qt_steel")->bash_resist() > 0);
}
