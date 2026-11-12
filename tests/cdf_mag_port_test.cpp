#include "catch/catch.hpp"
#include "json.h"

#include <fstream>
#include <set>
#include <sstream>
#include <string>

namespace {

const std::string P3_GOLDEN_FILENAME = "tests/data/dda_staged/p3_golden.json";
const std::string P3_GOLDEN_TARGET_IDS_FILENAME =
    "tests/data/dda_staged/p3_golden_target_ids.json";
// CDF: P3-7-MODS-T. The TEST_DATA rows the packet keeps on the legacy path (the 7-PRE derive
// fixtures; tests/data/dda_staged/p3_batches/_fixture_keep.json, `{ "ids": { id: reason } }`).
// The class cases below skip exactly these ids and print them, so the un-hidden cases stay green
// while the legacy fixtures keep their shape (tests/p3_data_modst_test.cpp S1 pins that shape).
const std::string P3_FIXTURE_KEEP_FILENAME =
    "tests/data/dda_staged/p3_batches/_fixture_keep.json";

} // namespace

TEST_CASE("p3 golden file has format 1, the DDA hash and the target items", "[p3][golden]") {
    std::ifstream file(P3_GOLDEN_FILENAME, std::ios::binary);
    JsonIn jsin(file);
    JsonObject golden = jsin.get_object();
    golden.allow_omitted_members();

    REQUIRE(golden.get_int("format") == 1);
    REQUIRE(golden.get_string("dda") == "918f396f370ea48fad924fb45a797386a2f6ddb4");
    JsonObject items = golden.get_object("items");
    items.allow_omitted_members();

    // The count is read from the file; what is pinned is the 1165 target ids (7-GOLD1 regen
    // target, with their class; BATT4 removed mess_kit, crt_mess_kit and voltmeter and APPL1
    // autoclave and hydraulic_press) and the three TEST_DATA ids the tree gained after it.
    std::ifstream ids_file(P3_GOLDEN_TARGET_IDS_FILENAME, std::ios::binary);
    JsonIn ids_in(ids_file);
    JsonObject ids = ids_in.get_object();
    ids.allow_omitted_members();
    size_t expected = 0;
    for (const char* group : {"target", "extra"}) {
        JsonObject listed = ids.get_object(group);
        for (const JsonMember member : listed) {
            expected++;
            INFO(group << " id " << member.name());
            REQUIRE(items.has_object(member.name()));
            JsonObject item = items.get_object(member.name());
            item.allow_omitted_members();
            CHECK(item.get_string("class") == listed.get_string(member.name()));
        }
    }
    CHECK(items.size() == expected);
}

TEST_CASE("p3 golden items have a known class and action", "[p3][golden]") {
    std::ifstream file(P3_GOLDEN_FILENAME, std::ios::binary);
    JsonIn jsin(file);
    JsonObject golden = jsin.get_object();
    JsonObject items = golden.get_object("items");

    const std::set<std::string> known_classes =
        {"MAG", "GUN-INT", "TOOL-INT", "WELL", "CHARGES", "ADAPTOR", "DDA-POCKET"};
    const std::set<std::string> known_actions =
        {"COPY-TWIN",
         "COPY-TWIN-KEEP-CDF-AMMO",
         "AUTHOR",
         "AUTHOR-KEEP-CDF-WELL",
         "AUTHOR-KEEP-CDF-INTEGRAL",
         "DROP-KEY",
         "HAND-FALLBACK",
         "KEEP-REVIEW"};

    for (const JsonMember member : items) {
        JsonObject item = member.get_object();
        item.allow_omitted_members();
        REQUIRE(known_classes.count(item.get_string("class")) == 1);
        REQUIRE(known_actions.count(item.get_string("action")) == 1);
    }
}

TEST_CASE("p3 golden: only ADAPTOR rows carry an adaptor block", "[p3][golden]") {
    std::ifstream file(P3_GOLDEN_FILENAME, std::ios::binary);
    JsonIn jsin(file);
    JsonObject golden = jsin.get_object();
    JsonObject items = golden.get_object("items");

    size_t adaptor_rows = 0;
    for (const JsonMember member : items) {
        JsonObject item = member.get_object();
        item.allow_omitted_members();
        const bool has_adaptor = item.has_member("adaptor");
        if (item.get_string("action") == "KEEP-REVIEW") {
            REQUIRE(item.get_string("class") == "ADAPTOR");
            REQUIRE(has_adaptor);
        } else {
            REQUIRE_FALSE(has_adaptor);
        }
        if (has_adaptor) { adaptor_rows++; }
    }
    REQUIRE(adaptor_rows == 4);
}

// ---------------------------------------------------------------------------------------------
// P3-7-R0: the consumer of p3_golden.json. Every golden id that is in the loaded world must load
// with exactly the golden pockets, compared index by index and only on the keys the golden entry
// carries (README: "only the keys that are present"). The cases are hidden ([.]) so the suite
// stays green until the data packet of a class ports its rows and un-hides its tag.
//
// Key map (golden pocket key -> pocket_data member):
//   pocket_type -> type, ammo_restriction -> ammo_restriction, flag_restriction ->
//   get_flag_restrictions(), item_restriction -> item_id_restriction, allowed_speedloaders,
//   default_magazine, id -> pocket_id, magazine_well, max_contains_volume -> raw_volume_capacity,
//   max_contains_weight, max_item_volume, max_item_length, moves, rigid, holster, watertight,
//   airtight, open_container, carry.
// ---------------------------------------------------------------------------------------------

#include "generic_factory.h"
#include "generic_readers.h"
#include "item_factory.h"
#include "itype.h"
#include "pocket_data.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <vector>

namespace {

struct golden_counts {
    size_t in_golden = 0;
    size_t loaded = 0;
    std::vector<std::string> skipped;
};

// CDF: P3-7-MODS-T: read `tests/data/dda_staged/p3_batches/_fixture_keep.json` once. A missing
// file is an empty list (no packet keeps a fixture).
auto fixture_keep_ids() -> const std::set<std::string>& {
    static std::set<std::string> ids;
    static bool read = false;
    if (!read) {
        read = true;
        std::ifstream file(P3_FIXTURE_KEEP_FILENAME, std::ios::binary);
        if (!file.good()) { return ids; }
        JsonIn jsin(file);
        JsonObject root = jsin.get_object();
        root.allow_omitted_members();
        if (root.has_object("ids")) {
            for (const JsonMember member : root.get_object("ids")) {
                member.get_string();
                ids.insert(member.name());
            }
        }
    }
    return ids;
}

// Loaded pockets as the golden sees them. The golden never lists MOD, CORPSE or SOFTWARE pockets
// (the loader synthesizes the MOD pocket of guns itself, per DDA), so those are dropped here;
// the relative order of the rest is unchanged.
// CDF: LINKUP1e also drops CABLE: the loader adds one to every item with the link_up action
// (src/item_factory.cpp add_cable_pocket, DDA item_factory.cpp:4144-4148) and the golden records
// the JSON pocket_data only. tests/p3_data_linkup1e_test.cpp pins that pocket on the 45 tools.
auto golden_view(const itype& type) -> std::vector<const pocket_data*> {
    std::vector<const pocket_data*> out;
    for (const pocket_data& pocket : type.pockets) {
        if (pocket.type == pocket_type::MOD || pocket.type == pocket_type::CORPSE ||
            pocket.type == pocket_type::SOFTWARE || pocket.type == pocket_type::CABLE) {
            continue;
        }
        out.push_back(&pocket);
    }
    return out;
}

template <typename A, typename B>
void diff_if(std::vector<std::string>& diffs, const std::string& key, const A& loaded,
             const B& expected) {
    if (!(loaded == expected)) { diffs.push_back(key); }
}

// Names the keys of one golden pocket that the loaded pocket does not match.
auto compare_pocket(const JsonObject& g, const pocket_data& p) -> std::vector<std::string> {
    std::vector<std::string> diffs;
    bool was_loaded = false;
    if (g.has_member("pocket_type")) {
        diff_if(diffs, "pocket_type", io::enum_to_string(p.type), g.get_string("pocket_type"));
    }
    if (g.has_member("ammo_restriction")) {
        std::map<ammotype, int> expected;
        optional(g, was_loaded, "ammo_restriction", expected);
        diff_if(diffs, "ammo_restriction", p.ammo_restriction, expected);
    }
    if (g.has_member("flag_restriction")) {
        pocket_data::FlagsSetType expected;
        optional(g, was_loaded, "flag_restriction", expected);
        diff_if(diffs, "flag_restriction", p.get_flag_restrictions(), expected);
    }
    if (g.has_member("item_restriction")) {
        cata::flat_set<itype_id> expected;
        optional(g, was_loaded, "item_restriction", expected);
        diff_if(diffs, "item_restriction", p.item_id_restriction, expected);
    }
    if (g.has_member("allowed_speedloaders")) {
        cata::flat_set<itype_id> expected;
        optional(g, was_loaded, "allowed_speedloaders", expected);
        diff_if(diffs, "allowed_speedloaders", p.allowed_speedloaders, expected);
    }
    if (g.has_member("default_magazine")) {
        itype_id expected;
        optional(g, was_loaded, "default_magazine", expected);
        diff_if(diffs, "default_magazine", p.default_magazine, expected);
    }
    if (g.has_member("id")) { diff_if(diffs, "id", p.pocket_id, g.get_string("id")); }
    if (g.has_member("magazine_well")) {
        units::volume expected = 0_ml;
        optional(g, was_loaded, "magazine_well", expected, volume_reader(), 0_ml);
        diff_if(diffs, "magazine_well", p.magazine_well, expected);
    }
    if (g.has_member("max_contains_volume")) {
        units::volume expected = 0_ml;
        optional(g, was_loaded, "max_contains_volume", expected, volume_reader(), 0_ml);
        diff_if(diffs, "max_contains_volume", p.raw_volume_capacity, expected);
    }
    if (g.has_member("max_contains_weight")) {
        units::mass expected = 0_gram;
        optional(g, was_loaded, "max_contains_weight", expected, mass_reader(), 0_gram);
        diff_if(diffs, "max_contains_weight", p.max_contains_weight, expected);
    }
    if (g.has_member("max_item_volume")) {
        units::volume expected = 0_ml;
        optional(g, was_loaded, "max_item_volume", expected, volume_reader(), 0_ml);
        const bool same = p.max_item_volume && *p.max_item_volume == expected;
        if (!same) { diffs.push_back("max_item_volume"); }
    }
    if (g.has_member("max_item_length")) {
        units::length expected = 0_mm;
        optional(g, was_loaded, "max_item_length", expected);
        diff_if(diffs, "max_item_length", p.max_item_length, expected);
    }
    if (g.has_member("moves")) { diff_if(diffs, "moves", p.moves, g.get_int("moves")); }
    if (g.has_member("rigid")) { diff_if(diffs, "rigid", p.rigid, g.get_bool("rigid")); }
    if (g.has_member("holster")) { diff_if(diffs, "holster", p.holster, g.get_bool("holster")); }
    if (g.has_member("watertight")) {
        diff_if(diffs, "watertight", p.watertight, g.get_bool("watertight"));
    }
    if (g.has_member("airtight")) {
        diff_if(diffs, "airtight", p.airtight, g.get_bool("airtight"));
    }
    if (g.has_member("open_container")) {
        diff_if(diffs, "open_container", p.open_container, g.get_bool("open_container"));
    }
    if (g.has_member("carry")) { diff_if(diffs, "carry", p.carry, g.get_bool("carry")); }
    return diffs;
}

// Compares one golden item (its `pockets` array) with the loaded item of the same id. Counts the
// id and appends one line per mismatch to `failures`. Ids absent from the loaded world (mod items)
// are counted and skipped. Shared by the golden walk and by the in-memory mutation cases of
// tests/p3_batch_generic_test.cpp.
void compare_item(const std::string& name, JsonArray gold_pockets, bool order_only,
                  golden_counts& counts, std::vector<std::string>& failures) {
    counts.in_golden++;
    const itype_id id(name);
    // ids absent from the loaded world (mod items) are counted and skipped
    if (!id.is_valid()) {
        counts.skipped.push_back(name);
        return;
    }
    counts.loaded++;
    const std::vector<const pocket_data*> loaded = golden_view(id.obj());
    if (loaded.size() != gold_pockets.size()) {
        failures.push_back(name + ": " + std::to_string(loaded.size()) +
                           " pockets loaded, golden has " + std::to_string(gold_pockets.size()));
        return;
    }
    size_t index = 0;
    for (JsonObject gold : gold_pockets) {
        gold.allow_omitted_members();
        if (order_only) {
            if (io::enum_to_string(loaded[index]->type) != gold.get_string("pocket_type")) {
                failures.push_back(name + ": pocket " + std::to_string(index) + " is " +
                                   io::enum_to_string(loaded[index]->type) + ", golden " +
                                   gold.get_string("pocket_type"));
            }
        } else {
            for (const std::string& key : compare_pocket(gold, *loaded[index])) {
                failures.push_back(name + ": pocket " + std::to_string(index) + " " + key);
            }
        }
        index++;
    }
}

// Walks the golden. `cls` empty means every class. `order_only` compares the pocket count and
// the pocket_type sequence of items with two or more golden pockets and nothing else.
// `only_ids`, when not null, restricts the walk to those ids (7-GOLD1 batches).
// `skip`, when not null, leaves out the fixture-keep ids of P3-7-MODS-T and counts them.
auto compare_golden(const std::string& cls, bool order_only, golden_counts& counts,
                    const std::set<std::string>* only_ids = nullptr,
                    const std::set<std::string>* skip = nullptr) -> std::vector<std::string> {
    std::ifstream file(P3_GOLDEN_FILENAME, std::ios::binary);
    JsonIn jsin(file);
    JsonObject golden = jsin.get_object();
    golden.allow_omitted_members();
    JsonObject items = golden.get_object("items");
    std::vector<std::string> failures;

    for (const JsonMember member : items) {
        JsonObject item = member.get_object();
        item.allow_omitted_members();
        if (!cls.empty() && item.get_string("class") != cls) { continue; }
        if (only_ids != nullptr && only_ids->count(member.name()) == 0) { continue; }
        if (skip != nullptr && skip->count(member.name()) != 0) { continue; }
        JsonArray gold_pockets = item.get_array("pockets");
        if (order_only && gold_pockets.size() < 2) { continue; }
        compare_item(member.name(), gold_pockets, order_only, counts, failures);
    }
    return failures;
}

void report_failures(const std::vector<std::string>& failures) {
    std::string head;
    for (size_t i = 0; i < failures.size() && i < 15; i++) { head += failures[i] + "\n"; }
    INFO(failures.size() << " mismatches, first:\n" << head);
    CHECK(failures.empty());
}

// CDF: P3-7-MODS-T: the fixture-keep ids of `cls` (the class case skips and prints them).
auto class_fixture_keep(const std::string& cls) -> std::vector<std::string> {
    const std::set<std::string>& skip = fixture_keep_ids();
    std::vector<std::string> out;
    if (skip.empty()) { return out; }
    std::ifstream file(P3_GOLDEN_FILENAME, std::ios::binary);
    JsonIn jsin(file);
    JsonObject golden = jsin.get_object();
    golden.allow_omitted_members();
    JsonObject items = golden.get_object("items");
    for (const JsonMember member : items) {
        JsonObject item = member.get_object();
        item.allow_omitted_members();
        if (item.get_string("class") != cls) { continue; }
        if (skip.count(member.name()) != 0) { out.push_back(member.name()); }
    }
    return out;
}

void check_class(const std::string& cls) {
    golden_counts counts;
    const std::set<std::string>& skip = fixture_keep_ids();
    const std::vector<std::string> kept = class_fixture_keep(cls);
    const std::vector<std::string> failures =
        compare_golden(cls, false, counts, nullptr, &skip);
    std::cout << "p3 golden " << cls << ": golden=" << counts.in_golden
              << " loaded=" << counts.loaded << " skipped=" << counts.skipped.size()
              << " fixture-kept=" << kept.size() << std::endl;
    for (const std::string& id : kept) { std::cout << "  fixture-keep " << id << "\n"; }
    report_failures(failures);
}

} // namespace

// Used by tests/p3_golden_batches_test.cpp: compares only `ids` against the golden.
auto p3_golden_compare_ids(const std::set<std::string>& ids, size_t& in_golden, size_t& loaded,
                           std::vector<std::string>& skipped) -> std::vector<std::string> {
    golden_counts counts;
    std::vector<std::string> failures = compare_golden("", false, counts, &ids);
    in_golden = counts.in_golden;
    loaded = counts.loaded;
    skipped = counts.skipped;
    return failures;
}

// Used by tests/p3_batch_generic_test.cpp: pocket-type order of the multi-pocket golden items among
// `ids` (in_golden counts those items only).
auto p3_golden_order_ids(const std::set<std::string>& ids, size_t& in_golden, size_t& loaded,
                         std::vector<std::string>& skipped) -> std::vector<std::string> {
    golden_counts counts;
    std::vector<std::string> failures = compare_golden("", true, counts, &ids);
    in_golden = counts.in_golden;
    loaded = counts.loaded;
    skipped = counts.skipped;
    return failures;
}

// Used by tests/p3_batch_generic_test.cpp: compares the loaded item `id` with a golden `pockets`
// array given as JSON text (a possibly mutated in-memory copy of the golden item). Returns one
// line per mismatch; `was_loaded` is false when the id is absent from the loaded world.
auto p3_golden_compare_pockets(const std::string& id, const std::string& pockets_json,
                               bool& was_loaded) -> std::vector<std::string> {
    std::istringstream stream(pockets_json);
    JsonIn jsin(stream);
    JsonArray pockets = jsin.get_array();
    golden_counts counts;
    std::vector<std::string> failures;
    compare_item(id, pockets, false, counts, failures);
    was_loaded = counts.loaded == 1;
    return failures;
}

// CDF: P3-7-MODS-T un-hides R1, R4, R5, R6 and R8: the TEST_DATA rows of those classes are ported
// and the kept legacy fixtures are skipped by name (_fixture_keep.json). P3-MODSFIX un-hides R2
// (u_shotgun's integral MAGAZINE is rigid, the fixture inherits it). P3-7ENDA un-hides R3: the
// staged golden WELL rows were refreshed against the merged well packets (BATT1, R3B, R3C, LINKUP1c).
TEST_CASE("p3 golden load R1 MAG", "[p3][golden][load][R1]") { check_class("MAG"); }

TEST_CASE("p3 golden load R2 GUN-INT", "[p3][golden][load][R2]") { check_class("GUN-INT"); }

TEST_CASE("p3 golden load R3 WELL", "[p3][golden][load][R3]") { check_class("WELL"); }

TEST_CASE("p3 golden load R4 TOOL-INT", "[p3][golden][load][R4]") { check_class("TOOL-INT"); }

TEST_CASE("p3 golden load R5 CHARGES", "[p3][golden][load][R5]") { check_class("CHARGES"); }

TEST_CASE("p3 golden load R6 ADAPTOR", "[p3][golden][load][R6]") { check_class("ADAPTOR"); }

TEST_CASE("p3 golden load R8 DDA-POCKET", "[p3][golden][load][R8]") {
    check_class("DDA-POCKET");
}

TEST_CASE("p3 golden pocket order", "[.][p3][golden][load][order]") {
    golden_counts counts;
    const std::vector<std::string> failures = compare_golden("", true, counts);
    std::cout << "p3 golden order: multi-pocket golden=" << counts.in_golden
              << " loaded=" << counts.loaded << " skipped=" << counts.skipped.size() << std::endl;
    report_failures(failures);
}

TEST_CASE("p3 golden skip counts", "[.][p3][golden][skips]") {
    // diagnostic, always passes: makes a silent skip visible
    for (const std::string cls :
         {"MAG", "GUN-INT", "WELL", "TOOL-INT", "CHARGES", "ADAPTOR", "DDA-POCKET"}) {
        golden_counts counts;
        compare_golden(cls, false, counts);
        std::cout << "p3 golden skips " << cls << ": in golden " << counts.in_golden << ", loaded "
                  << counts.loaded << ", skipped " << counts.skipped.size() << std::endl;
        for (const std::string& id : counts.skipped) { std::cout << "  skipped " << id << "\n"; }
    }
    SUCCEED();
}
