#include "catch/catch.hpp"
#include "itype.h"
#include "json.h"
#include "pocket_data.h"
#include "type_id.h"

#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

// CDF: P3-7-MODS-T: the TEST_DATA (unit-test world) rows of the 7-MODS-T packet.
//
// The 21 converted rows are in tests/data/dda_staged/p3_batches/7-MODS-T.json `items` and are
// compared with the staged golden by the generic cases (B1-B5, G2). The 26 KEEP rows and the 3
// legacy-path fixtures are in `not_edited` (their ids are not golden ids, and a batch id must be a
// golden id), and the fixtures are named again in `_fixture_keep.json`, which the class cases in
// tests/cdf_mag_port_test.cpp skip and print. These cases pin the parts the generic cases cannot
// see: the fixtures still have their legacy shape (S1), the converted rows are really converted
// (S2), the un-hidden class cases are not vacuous (S3), and the rows we did not edit still have
// exactly their own key set (S4, the generic B4 only reads `items`).
//
// Mutations the cases kill: converting a fixture (S1); putting `capacity` back on a cz75 magazine
// or `magazines` back on a converted row (S2); skipping every id of a class (S3); editing a KEEP
// row, e.g. the 7-R6R8 `flags` re-pin (S4).

namespace {

const std::string MODST_BATCH_PATH = "tests/data/dda_staged/p3_batches/7-MODS-T.json";
const std::string MODST_FIXTURE_KEEP_PATH =
    "tests/data/dda_staged/p3_batches/_fixture_keep.json";
const std::string MODST_GOLDEN_PATH = "tests/data/dda_staged/p3_golden.json";

// CDF: the un-hidden class cases (tests/cdf_mag_port_test.cpp): R1, R4, R5, R6 and R8. R2 and R3
// stay hidden (rows outside this packet, REPORT `## Blocked`).
const std::vector<std::string> MODST_UNHIDDEN_CLASSES = {
    "MAG", "TOOL-INT", "CHARGES", "ADAPTOR", "DDA-POCKET",
};

struct modst_row {
    std::string file;
    std::string action;
    std::vector<std::string> dropped_keys;
    std::vector<std::string> own_keys;
    bool has_own_keys = false;
    bool already_converted = false;
};

struct modst_batch {
    std::map<std::string, modst_row> items;
    std::map<std::string, modst_row> not_edited;
    std::map<std::string, std::string> fixture_keep;
};

auto read_row(const JsonMember& member) -> modst_row {
    JsonObject row = member.get_object();
    row.allow_omitted_members();
    modst_row out;
    out.file = row.has_string("file") ? row.get_string("file") : "";
    out.action = row.has_string("action") ? row.get_string("action") : "";
    if (row.has_array("dropped_keys")) {
        for (const std::string& key : row.get_string_array("dropped_keys")) {
            out.dropped_keys.push_back(key);
        }
    }
    if (row.has_array("own_keys")) {
        out.has_own_keys = true;
        for (const std::string& key : row.get_string_array("own_keys")) {
            out.own_keys.push_back(key);
        }
    }
    out.already_converted = row.has_string("already_converted");
    return out;
}

auto read_modst_batch() -> const modst_batch& {
    static modst_batch batch;
    static bool read = false;
    if (!read) {
        read = true;
        std::ifstream file(MODST_BATCH_PATH, std::ios::binary);
        REQUIRE(file.good());
        JsonIn jsin(file);
        JsonObject root = jsin.get_object();
        root.allow_omitted_members();
        REQUIRE(root.get_string("batch") == "7-MODS-T");
        for (const char* member : {"items", "not_edited"}) {
            if (!root.has_object(member)) { continue; }
            auto& into = std::string(member) == "items" ? batch.items : batch.not_edited;
            for (const JsonMember row : root.get_object(member)) {
                into.emplace(row.name(), read_row(row));
            }
        }
        std::ifstream keep_file(MODST_FIXTURE_KEEP_PATH, std::ios::binary);
        REQUIRE(keep_file.good());
        JsonIn keep_jsin(keep_file);
        JsonObject keep_root = keep_jsin.get_object();
        keep_root.allow_omitted_members();
        JsonObject ids = keep_root.get_object("ids");
        ids.allow_omitted_members();
        for (const JsonMember member : ids) {
            batch.fixture_keep.emplace(member.name(), member.get_string());
        }
    }
    return batch;
}

// The top-level keys of every record of a data file, by its `id`.
using record_keys = std::map<std::string, std::vector<std::set<std::string>>>;

auto file_records(const std::string& path) -> const record_keys& {
    static std::map<std::string, record_keys> cache;
    const auto found = cache.find(path);
    if (found != cache.end()) { return found->second; }
    record_keys records;
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.good());
    JsonIn jsin(file);
    JsonArray entries = jsin.get_array();
    for (JsonObject entry : entries) {
        entry.allow_omitted_members();
        std::set<std::string> keys;
        for (const JsonMember member : entry) { keys.insert(member.name()); }
        if (entry.has_string("id")) { records[entry.get_string("id")].push_back(keys); }
    }
    return cache.emplace(path, records).first->second;
}

auto own_record(const std::string& path, const std::string& id) -> const std::set<std::string>* {
    const record_keys& records = file_records(path);
    const auto found = records.find(id);
    if (found == records.end() || found->second.size() != 1) { return nullptr; }
    return &found->second.front();
}

auto first_backticked(const std::string& text) -> std::string {
    const size_t open = text.find('`');
    if (open == std::string::npos) { return {}; }
    const size_t close = text.find('`', open + 1);
    return close == std::string::npos ? std::string() : text.substr(open + 1, close - open - 1);
}

auto pocket_type_count(const itype& type, pocket_type wanted) -> size_t {
    size_t n = 0;
    for (const pocket_data& pocket : type.pockets) { n += pocket.type == wanted ? 1 : 0; }
    return n;
}

// Golden ids of `cls`, minus the fixture-keep ids (the class cases' input).
auto class_ids(const std::string& cls, const std::set<std::string>& skip) -> std::set<std::string> {
    std::ifstream file(MODST_GOLDEN_PATH, std::ios::binary);
    REQUIRE(file.good());
    JsonIn jsin(file);
    JsonObject golden = jsin.get_object();
    golden.allow_omitted_members();
    JsonObject items = golden.get_object("items");
    items.allow_omitted_members();
    std::set<std::string> out;
    for (const JsonMember member : items) {
        JsonObject item = member.get_object();
        item.allow_omitted_members();
        if (item.get_string("class") != cls) { continue; }
        if (skip.count(member.name()) != 0) { continue; }
        out.insert(member.name());
    }
    return out;
}

// The class of a golden id (empty when the id is not in the golden).
auto golden_class(const std::string& id) -> std::string {
    std::ifstream file(MODST_GOLDEN_PATH, std::ios::binary);
    REQUIRE(file.good());
    JsonIn jsin(file);
    JsonObject golden = jsin.get_object();
    golden.allow_omitted_members();
    JsonObject items = golden.get_object("items");
    items.allow_omitted_members();
    for (const JsonMember member : items) {
        if (member.name() != id) { continue; }
        JsonObject item = member.get_object();
        item.allow_omitted_members();
        return item.get_string("class");
    }
    return {};
}

} // namespace

// Defined in cdf_mag_port_test.cpp: compares `ids` against the staged golden.
auto p3_golden_compare_ids(const std::set<std::string>& ids, size_t& in_golden, size_t& loaded,
                           std::vector<std::string>& skipped) -> std::vector<std::string>;

// S1: the kept legacy fixtures still have the legacy key shape named in their reason, and every one
// of them is listed with a reason and kept out of the batch `items`.
TEST_CASE("p3 modst fixtures kept", "[p3][data][MODST]") {
    const modst_batch& batch = read_modst_batch();
    REQUIRE(batch.fixture_keep.size() == 3);
    const std::set<std::string> expected = {"p3pre_mag", "p3pre_mag_legacy", "p3pre_tool_legacy"};
    std::set<std::string> seen;
    size_t checked = 0;
    for (const auto& [id, reason] : batch.fixture_keep) {
        CAPTURE(id, reason);
        seen.insert(id);
        // never a batch id: a batch id must match the golden, a kept fixture does not
        CHECK(batch.items.count(id) == 0);
        INFO("not listed in the batch `not_edited`");
        const auto row = batch.not_edited.find(id);
        REQUIRE(row != batch.not_edited.end());
        REQUIRE(!reason.empty());
        const std::string key = first_backticked(reason);
        INFO("the reason names the key `" << key << "`");
        REQUIRE(!key.empty());
        const std::set<std::string>* own = own_record(row->second.file, id);
        REQUIRE(own != nullptr);
        // the own record still carries the legacy key the reason names (not converted)
        CHECK(own->count(key) == 1);
        checked++;
    }
    CHECK(seen == expected);
    CHECK(checked == 3);
}

// S2: every `items` row is converted: no legacy magazines/magazine_well table on its own record,
// its declared dropped keys are gone, its pocket comes from the record or its copy-from parent, and
// the loaded item has a magazine pocket at all.
TEST_CASE("p3 modst converted rows", "[p3][data][MODST]") {
    const modst_batch& batch = read_modst_batch();
    REQUIRE(batch.items.size() == 21);
    size_t checked = 0;
    for (const auto& [id, row] : batch.items) {
        CAPTURE(id, row.file);
        const std::set<std::string>* own = own_record(row.file, id);
        REQUIRE(own != nullptr);
        for (const std::string& key : row.dropped_keys) {
            INFO("dropped key " << key);
            CHECK(own->count(key) == 0);
        }
        if (!row.already_converted) {
            // CDF: a row a merged packet converted keeps no legacy magazine table of its own
            CHECK(own->count("magazines") == 0);
            CHECK(own->count("magazine_well") == 0);
        }
        // the pocket is authored on the row or inherited from its copy-from parent
        INFO("no pocket_data on the row and no copy-from");
        CHECK((own->count("pocket_data") == 1 || own->count("copy-from") == 1));
        const itype_id type_id(id);
        REQUIRE(type_id.is_valid());
        const size_t magazines = pocket_type_count(type_id.obj(), pocket_type::MAGAZINE);
        const size_t wells = pocket_type_count(type_id.obj(), pocket_type::MAGAZINE_WELL);
        INFO("magazine pockets " << magazines << " wells " << wells);
        CHECK(magazines + wells > 0);
        checked++;
    }
    CHECK(checked == 21);
    // CDF: the cz75 pair dropped `capacity`; the loader derives 10/16 from the pocket
    REQUIRE(itype_id("cz75mag_10rd").obj().magazine);
    CHECK(itype_id("cz75mag_10rd").obj().magazine->capacity == 10);
    REQUIRE(itype_id("cz75mag_16rd").obj().magazine);
    CHECK(itype_id("cz75mag_16rd").obj().magazine->capacity == 16);
}

// S3: the un-hidden class cases are not vacuous: every class still has golden ids after the
// fixture-keep skip, all of them load, and their pockets still match the golden.
TEST_CASE("p3 modst class cases not vacuous", "[p3][data][MODST]") {
    const modst_batch& batch = read_modst_batch();
    std::set<std::string> skip;
    for (const auto& [id, reason] : batch.fixture_keep) { skip.insert(id); }
    size_t checked = 0;
    for (const std::string& cls : MODST_UNHIDDEN_CLASSES) {
        CAPTURE(cls);
        const std::set<std::string> ids = class_ids(cls, skip);
        INFO("class " << cls << " has golden ids after the fixture-keep skip");
        CHECK(ids.size() > 0);
        size_t in_golden = 0;
        size_t loaded = 0;
        std::vector<std::string> skipped;
        const std::vector<std::string> failures =
            p3_golden_compare_ids(ids, in_golden, loaded, skipped);
        std::cout << "modst class " << cls << ": ids " << ids.size() << " loaded " << loaded
                  << " skipped " << skipped.size() << " fixture-keep";
        for (const std::string& id : skip) {
            if (golden_class(id) == cls) { std::cout << " " << id; }
        }
        std::cout << std::endl;
        INFO("the class case loaded nothing");
        CHECK(loaded > 0);
        CHECK(in_golden == ids.size());
        std::string head;
        for (size_t i = 0; i < failures.size() && i < 15; i++) { head += failures[i] + "\n"; }
        INFO(failures.size() << " mismatches, first:\n" << head);
        CHECK(failures.empty());
        checked++;
    }
    CHECK(checked == MODST_UNHIDDEN_CLASSES.size());
}

// S4: the rows this packet does not edit keep exactly their own key set (the generic B4 reads only
// `items`, and a KEEP row is not a golden id).
TEST_CASE("p3 modst rows not edited", "[p3][data][MODST]") {
    const modst_batch& batch = read_modst_batch();
    REQUIRE(batch.not_edited.size() == 29);
    size_t checked = 0;
    for (const auto& [id, row] : batch.not_edited) {
        CAPTURE(id, row.file);
        REQUIRE(row.has_own_keys);
        const std::set<std::string>* own = own_record(row.file, id);
        REQUIRE(own != nullptr);
        const std::set<std::string> expected(row.own_keys.begin(), row.own_keys.end());
        std::string missing;
        std::string extra;
        for (const std::string& key : expected) {
            if (own->count(key) == 0) { missing += " " + key; }
        }
        for (const std::string& key : *own) {
            if (expected.count(key) == 0) { extra += " " + key; }
        }
        INFO("missing:" << missing << " extra:" << extra);
        CHECK(*own == expected);
        checked++;
    }
    CHECK(checked == 29);
}
