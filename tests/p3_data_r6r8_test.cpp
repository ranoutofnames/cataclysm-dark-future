#include "calendar.h"
#include "catch/catch.hpp"
#include "flag.h"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"
#include "json.h"
#include "pocket_data.h"
#include "pocket_type.h"
#include "type_id.h"
#include "units.h"
#include "itype_slots/islot_mod.h"

#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <vector>

// CDF: P3-7-R6R8. R8 (DDA-POCKET: helmet_nomad, robofac_armor_anchor, robofac_head_rig copy
// their DDA twin's pockets), R6 (ADAPTOR: the conversion mods still swap the well they act on),
// R9 (STORAGE: the magazine/well pocket is first, the carry pockets after) and the R7 close-out
// (the rows that wait on a named prerequisite stay unported).
//
// Batch input: tests/data/dda_staged/p3_batches/7-R6R8.json. `items` holds the 6 golden ids (a
// batch id must be a golden id, p3_golden_batches_test.cpp); the 16 KEEP and the LEAVE row are
// in `not_edited` because the golden (the port-target plan) does not carry them.

namespace {

const std::string R6R8_BATCH = "tests/data/dda_staged/p3_batches/7-R6R8.json";
const std::string CRT_TOOLARMOR = "data/mods/CRT_EXPANSION/items/crt_toolarmor.json";
const std::string UPS_GUNS = "data/json/items/gun/ups.json";

struct not_edited_row {
    std::string id;
    std::string file;
    std::vector<std::string> own_keys;
};

struct r6r8_batch {
    std::vector<not_edited_row> not_edited;
    std::vector<std::string> waiting;
};

auto read_batch() -> r6r8_batch {
    std::ifstream file(R6R8_BATCH, std::ios::binary);
    REQUIRE(file.good());
    JsonIn jsin(file);
    JsonObject root = jsin.get_object();
    root.allow_omitted_members();
    r6r8_batch out;
    JsonObject not_edited = root.get_object("not_edited");
    not_edited.allow_omitted_members();
    for (const JsonMember member : not_edited) {
        JsonObject row = member.get_object();
        row.allow_omitted_members();
        out.not_edited.push_back(
            {member.name(), row.get_string("file"), row.get_string_array("own_keys")});
    }
    out.waiting = root.get_string_array("waiting_not_in_batch");
    return out;
}

struct record_probe {
    bool found = false;
    bool pocket_data = false;
    bool max_charges = false;
    bool ups_charges = false;
};

auto probe_record(const std::string& file_name, const std::string& id) -> record_probe {
    std::ifstream file(file_name, std::ios::binary);
    REQUIRE(file.good());
    JsonIn jsin(file);
    JsonArray entries = jsin.get_array();
    record_probe out;
    for (JsonObject entry : entries) {
        entry.allow_omitted_members();
        if (!entry.has_string("id") || entry.get_string("id") != id) { continue; }
        out.found = true;
        out.pocket_data = entry.has_member("pocket_data");
        out.max_charges = entry.has_member("max_charges");
        out.ups_charges = entry.has_member("ups_charges");
    }
    return out;
}

auto pocket_count(const itype& type, pocket_type wanted) -> int {
    int count = 0;
    for (const pocket_data& pocket : type.pockets) { count += pocket.type == wanted ? 1 : 0; }
    return count;
}

auto well_of(const item& it) -> const item_pocket* { // *NOPAD*
    for (const item_pocket& pocket : it.contents.get_pockets()) {
        if (pocket.is_type(pocket_type::MAGAZINE_WELL)) { return &pocket; }
    }
    return nullptr;
}

auto restriction_of(const pocket_data& pocket) -> std::set<itype_id> {
    return std::set<itype_id>(pocket.item_id_restriction.begin(), pocket.item_id_restriction.end());
}

// The loaded pockets as the golden sees them: MOD, CORPSE and SOFTWARE pockets are synthesized
// by the loader and are not part of the golden (tests/cdf_mag_port_test.cpp golden_view).
auto golden_view(const itype& type) -> std::vector<const pocket_data*> {
    std::vector<const pocket_data*> out;
    for (const pocket_data& pocket : type.pockets) {
        if (pocket.type == pocket_type::MOD || pocket.type == pocket_type::CORPSE
            || pocket.type == pocket_type::SOFTWARE) {
            continue;
        }
        out.push_back(&pocket);
    }
    return out;
}

// The top-level keys of the one record `id` in `file`; nullopt when it is absent or duplicated.
auto own_record_keys(const std::string& file_name, const std::string& id)
    -> std::optional<std::set<std::string>> {
    std::ifstream file(file_name, std::ios::binary);
    REQUIRE(file.good());
    JsonIn jsin(file);
    JsonArray entries = jsin.get_array();
    std::set<std::string> keys;
    int records = 0;
    for (JsonObject entry : entries) {
        entry.allow_omitted_members();
        if (!entry.has_string("id") || entry.get_string("id") != id) { continue; }
        records++;
        for (const JsonMember member : entry) { keys.insert(member.name()); }
    }
    if (records != 1) { return std::nullopt; }
    return keys;
}

auto key_set(const std::vector<std::string>& listed) -> std::set<std::string> {
    return std::set<std::string>(listed.begin(), listed.end());
}

} // namespace

// S1: the three R8 rows load with their DDA twin's pockets; the explicit pockets they had stay
// in front of the appended magazine pocket.
TEST_CASE("p3 r8 samples", "[p3][data][R6R8]") {
    SECTION("helmet_nomad takes the GASFILTER_SM well") {
        REQUIRE(itype_id("helmet_nomad").is_valid());
        const itype& type = itype_id("helmet_nomad").obj();
        REQUIRE(type.pockets.size() == 1);
        const pocket_data& well = type.pockets.front();
        CHECK(well.type == pocket_type::MAGAZINE_WELL);
        CHECK(well.rigid);
        // CDF: DDA custom_headgear.json:48-56, the GASFILTER_SM well of the twin
        CHECK(well.get_flag_restrictions().count(flag_id("GASFILTER_SM")) == 1);
        CHECK(well.default_magazine == itype_id("gasfilter_sm"));
    }

    SECTION("robofac_head_rig keeps its ablative pocket in front of the battery well") {
        REQUIRE(itype_id("robofac_head_rig").is_valid());
        const itype& type = itype_id("robofac_head_rig").obj();
        REQUIRE(type.pockets.size() == 2);
        // CDF: the EXPLICIT pocket the record already had (ABLATIVE_HELMET) stays first
        CHECK(type.pockets[0].type == pocket_type::CONTAINER);
        CHECK(type.pockets[0].get_flag_restrictions().count(flag_id("ABLATIVE_HELMET")) == 1);
        CHECK(type.pockets[0].raw_volume_capacity == 5000_ml);
        const pocket_data& well = type.pockets[1];
        CHECK(well.type == pocket_type::MAGAZINE_WELL);
        CHECK(well.rigid);
        // CDF: DDA robofac_armor.json:303-338, the BATTERY_MEDIUM well of the twin
        CHECK(well.get_flag_restrictions().count(flag_id("BATTERY_MEDIUM")) == 1);
        CHECK(well.default_magazine == itype_id("medium_battery_cell"));
    }

    SECTION("robofac_armor_anchor keeps its 8 rig pockets in front of the battery well") {
        REQUIRE(itype_id("robofac_armor_anchor").is_valid());
        const itype& type = itype_id("robofac_armor_anchor").obj();
        REQUIRE(type.pockets.size() == 9);
        for (size_t i = 0; i < 8; i++) {
            INFO("pocket " << i);
            CHECK(type.pockets[i].type == pocket_type::CONTAINER);
        }
        // CDF: DDA robofac_armor.json:127-160, the rig's pockets, copied verbatim (EXPLICIT)
        CHECK(type.pockets[0].watertight);
        CHECK(type.pockets[0].raw_volume_capacity == 1500_ml);
        CHECK(type.pockets[2].raw_volume_capacity == 1000_ml);
        CHECK(type.pockets[4].get_flag_restrictions().count(flag_id("ABLATIVE_MANTLE")) == 1);
        CHECK(type.pockets[5].get_flag_restrictions().count(flag_id("ABLATIVE_SKIRT")) == 1);
        CHECK(type.pockets[6].holster);
        CHECK(type.pockets[7].item_id_restriction.count(itype_id("robofac_poncho_liner")) == 1);
        const pocket_data& well = type.pockets[8];
        CHECK(well.type == pocket_type::MAGAZINE_WELL);
        CHECK(well.rigid);
        CHECK(well.get_flag_restrictions().count(flag_id("BATTERY_MEDIUM")) == 1);
        CHECK(well.default_magazine == itype_id("medium_battery_cell"));
    }
}

// S2: the conversion/adaptor mods are unchanged and, installed, they swap the well restriction
// as src/item_contents.cpp:356-423 does (the adaptor's magazines for the ammo the item fires).
TEST_CASE("p3 r6 adaptors", "[p3][data][R6R8]") {
    struct adaptor_row {
        std::string id;
        std::string ammo;
        std::vector<itype_id> magazines;
    };
    // CDF: the `magazine_adaptor` of each mod, read from data/json/items/{gunmod,toolmod}.json
    const std::vector<adaptor_row> adaptors = {
        {.id = "retool_ar15_300blk",
         .ammo = "300blk",
         .magazines = {itype_id("stanag30"), itype_id("stanag50"), itype_id("survivor223mag")}},
        {.id = "arredondo_chute",
         .ammo = "shot",
         .magazines = {itype_id("shot_speedloader6"), itype_id("shot_speedloader8")}},
        {.id = "retool_hmr_9mm", .ammo = "9mm", .magazines = {itype_id("9mm_speedloader6")}},
        {.id = "magazine_battery_mod",
         .ammo = "battery",
         .magazines =
             {itype_id("medium_storage_battery"), itype_id("small_storage_battery"),
              itype_id("storage_battery"), itype_id("huge_atomic_battery_cell"),
              itype_id("battery_car"), itype_id("battery_motorbike"),
              itype_id("battery_motorbike_small")}},
    };

    SECTION("the adaptor rows are unchanged") {
        for (const adaptor_row& row : adaptors) {
            CAPTURE(row.id);
            REQUIRE(itype_id(row.id).is_valid());
            const auto& mod = itype_id(row.id).obj().mod;
            REQUIRE(mod);
            const auto found = mod->magazine_adaptor.find(ammotype(row.ammo));
            REQUIRE(found != mod->magazine_adaptor.end());
            CHECK(found->second == std::set<itype_id>(row.magazines.begin(), row.magazines.end()));
        }
    }

    SECTION("every KEEP and KEEP-REVIEW row keeps its own record keys") {
        const r6r8_batch batch = read_batch();
        for (const not_edited_row& row : batch.not_edited) {
            INFO("id " << row.id << " file " << row.file);
            const std::optional<std::set<std::string>> record = own_record_keys(row.file, row.id);
            REQUIRE(record.has_value());
            CHECK(*record == key_set(row.own_keys));
        }
        CHECK(batch.not_edited.size() == 17);
    }

    SECTION("retool_ar15_300blk swaps the ar15 well") {
        detached_ptr<item> gun = item::spawn(itype_id("ar15"));
        REQUIRE(gun->has_pocket_type(pocket_type::MOD));
        const item_pocket* before = well_of(*gun);
        REQUIRE(before != nullptr);
        // CDF: the ar15 well before the mod (data/json/items/gun/223.json:67-84)
        CHECK(before->get_data().item_id_restriction.count(itype_id("stanag5")) == 1);
        gun->put_in(item::spawn(itype_id("retool_ar15_300blk")));
        const item_pocket* after = well_of(*gun);
        REQUIRE(after != nullptr);
        CHECK(restriction_of(after->get_data())
              == std::set<itype_id>(
                  {itype_id("stanag30"), itype_id("stanag50"), itype_id("survivor223mag")}));
        CHECK(after->get_data().item_id_restriction.count(itype_id("stanag5")) == 0);
        CHECK(after->get_data().default_magazine == itype_id("stanag30"));
    }

    SECTION("arredondo_chute gives a pump shotgun a speedloader well") {
        detached_ptr<item> gun = item::spawn(itype_id("remington_870"));
        REQUIRE(gun->has_pocket_type(pocket_type::MOD));
        // CDF: the pump shotgun has an integral 5-shot magazine and no well (gun/shot.json:372)
        CHECK(well_of(*gun) == nullptr);
        REQUIRE(pocket_count(itype_id("remington_870").obj(), pocket_type::MAGAZINE) == 1);
        gun->put_in(item::spawn(itype_id("arredondo_chute")));
        const item_pocket* after = well_of(*gun);
        REQUIRE(after != nullptr);
        CHECK(
            restriction_of(after->get_data())
            == std::set<itype_id>({itype_id("shot_speedloader6"), itype_id("shot_speedloader8")}));
        // the integral magazine is still there
        int magazines = 0;
        for (const item_pocket& pocket : gun->contents.get_pockets()) {
            magazines += pocket.is_type(pocket_type::MAGAZINE) ? 1 : 0;
        }
        CHECK(magazines == 1);
    }

    SECTION("retool_hmr_9mm gives the handmade revolver a 9mm speedloader well") {
        detached_ptr<item> gun = item::spawn(itype_id("handmade_revolver"));
        REQUIRE(gun->has_pocket_type(pocket_type::MOD));
        CHECK(well_of(*gun) == nullptr);
        gun->put_in(item::spawn(itype_id("retool_hmr_9mm")));
        const item_pocket* after = well_of(*gun);
        REQUIRE(after != nullptr);
        CHECK(restriction_of(after->get_data())
              == std::set<itype_id>({itype_id("9mm_speedloader6")}));
    }

    SECTION("magazine_battery_mod swaps the battery tool's well") {
        detached_ptr<item> tool = item::spawn(itype_id("electric_lantern"));
        REQUIRE(tool->has_pocket_type(pocket_type::MOD));
        // CDF: BATT2 gave this tool an integral MAGAZINE pocket and no well
        CHECK(well_of(*tool) == nullptr);
        REQUIRE(pocket_count(itype_id("electric_lantern").obj(), pocket_type::MAGAZINE) == 1);
        tool->put_in(item::spawn(itype_id("magazine_battery_mod")), pocket_type::MOD);
        const item_pocket* after = well_of(*tool);
        REQUIRE(after != nullptr);
        CHECK(
            restriction_of(after->get_data())
            == std::set<itype_id>(
                {itype_id("medium_storage_battery"), itype_id("small_storage_battery"),
                 itype_id("storage_battery"), itype_id("huge_atomic_battery_cell"),
                 itype_id("battery_car"), itype_id("battery_motorbike"),
                 itype_id("battery_motorbike_small")}));
        // CDF: the mod's pocket_mods well replaces the base pocket (item_contents.cpp:341-349)
        int magazines = 0;
        for (const item_pocket& pocket : tool->contents.get_pockets()) {
            magazines += pocket.is_type(pocket_type::MAGAZINE) ? 1 : 0;
        }
        CHECK(magazines == 0);
        // CDF: the mod's pocket_mods well accepts any length (pocket_data.cpp:96-113)
        CHECK(after->get_data().max_item_length >= 2_km);
    }
}

// S3: the R9 storage rows carry the magazine/well pocket first and the carry pockets after.
TEST_CASE("p3 r9 storage order", "[p3][data][R6R8]") {
    // CDF: the 13 ids with `storage: true` in tests/data/dda_staged/p3_golden.json
    const std::vector<std::string> storage_ids = {
        "crt_em_vest",         "crt_em_vest_on",        "crt_solarpack",
        "exosuit_industrial",  "exosuit_industrial_on", "exosuit_military",
        "exosuit_military_on", "phase_immersion_suit",  "phase_immersion_suit_on",
        "rm13_armor",          "rm13_armor_on",         "sparkledogsuit",
        "sparkledogsuit_on",
    };

    SECTION("the ported rows put the magazine/well pocket first") {
        size_t checked = 0;
        for (const std::string& id : storage_ids) {
            CAPTURE(id);
            if (!itype_id(id).is_valid()) {
                // CDF: the three CRT_EXPANSION rows are mod items and not in the unit-test world;
                // the next section checks their JSON record, never silent
                CHECK(id.rfind("crt_", 0) == 0);
                continue;
            }
            const std::vector<const pocket_data*> pockets = golden_view(itype_id(id).obj());
            REQUIRE(pockets.size() >= 2);
            const pocket_type first = pockets.front()->type;
            CHECK((first == pocket_type::MAGAZINE || first == pocket_type::MAGAZINE_WELL));
            for (size_t i = 1; i < pockets.size(); i++) {
                INFO("pocket " << i);
                // CDF: RM13PACK1: a storage row may carry more than one well (rm13_armor(_on) gets the
                // battery buffer well after its pack well). The carry pockets still come last.
                CHECK((pockets[i]->type == pocket_type::CONTAINER ||
                       pockets[i]->type == pocket_type::MAGAZINE_WELL));
            }
            checked++;
        }
        CHECK(checked == 10);
    }

    SECTION("the ported CRT rows carry their own pocket_data") {
        // CDF: AUTO-262 moved crt_em_vest(_on) and crt_solarpack to the MODS stage part 2. The
        // guard flips with the data: a row that loses its pocket_data or regains max_charges is
        // red (the same two checks the deferred form had, in the ported state).
        for (const std::string& id : {"crt_em_vest", "crt_em_vest_on", "crt_solarpack"}) {
            CAPTURE(id);
            const record_probe record = probe_record(CRT_TOOLARMOR, id);
            REQUIRE(record.found);
            CHECK(record.pocket_data);
            CHECK_FALSE(record.max_charges);
        }
    }
}

// S4: the R7 close-out. The 18 rows that wait for ammotypes (7-R5) and the LEAVE row are still
// unported; an id that becomes ready turns this red with its name.
TEST_CASE("p3 r7 close-out", "[p3][data][R6R8]") {
    const r6r8_batch batch = read_batch();
    REQUIRE(batch.waiting.size() == 18);
    int checked = 0;
    for (const std::string& id : batch.waiting) {
        CAPTURE(id);
        if (!itype_id(id).is_valid()) {
            // an obsoletion row in the base data (the 7-R5 case prints the same skip)
            continue;
        }
        const itype& type = itype_id(id).obj();
        // CDF: still on `charges` until its ammotype exists (AMMOTYPE1)
        CHECK(pocket_count(type, pocket_type::MAGAZINE) == 0);
        CHECK(pocket_count(type, pocket_type::MAGAZINE_WELL) == 0);
        checked++;
    }
    CHECK(checked >= 15);

    // the LEAVE row: the Aftershock cartridges its twin's well names are absent from CDF
    // (afs_archeotech_cartridge, afs_bootleg_cartridge, afs_cartridge; tool report `notes`)
    const record_probe rifle = probe_record(UPS_GUNS, "laser_rifle_cheap");
    REQUIRE(rifle.found);
    CHECK_FALSE(rifle.pocket_data);
    CHECK(rifle.ups_charges);
}
