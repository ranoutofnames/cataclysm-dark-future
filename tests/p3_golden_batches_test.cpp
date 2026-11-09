#include "catch/catch.hpp"
#include "json.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

// Defined in cdf_mag_port_test.cpp: compares only `ids` against p3_golden.json.
auto p3_golden_compare_ids(const std::set<std::string>& ids, size_t& in_golden, size_t& loaded,
                           std::vector<std::string>& skipped) -> std::vector<std::string>;

namespace {

const std::string P3_GOLDEN_PATH = "tests/data/dda_staged/p3_golden.json";
const std::string P3_BATCH_DIR = "tests/data/dda_staged/p3_batches";

struct batch_file {
    std::string name;
    std::set<std::string> ids;
};

auto read_batches() -> std::vector<batch_file> {
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(P3_BATCH_DIR)) {
        // neg-<batch>.json (the negative mutation fixtures, tests/p3_batch_generic_test.cpp) have
        // no `items`
        if (entry.path().extension() == ".json" &&
            entry.path().filename().string().rfind("neg-", 0) != 0) {
            paths.push_back(entry.path());
        }
    }
    std::sort(paths.begin(), paths.end());
    std::vector<batch_file> out;
    for (const auto& path : paths) {
        std::ifstream file(path, std::ios::binary);
        JsonIn jsin(file);
        JsonObject root = jsin.get_object();
        root.allow_omitted_members();
        batch_file batch;
        batch.name = root.get_string("batch");
        JsonObject items = root.get_object("items");
        for (const JsonMember member : items) {
            member.get_object().allow_omitted_members();
            batch.ids.insert(member.name());
        }
        out.push_back(batch);
    }
    return out;
}

auto read_batch(const std::string& name) -> batch_file {
    for (const batch_file& batch : read_batches()) {
        if (batch.name == name) { return batch; }
    }
    FAIL("no batch file named " << name);
    return {};
}

auto golden_ids() -> std::set<std::string> {
    std::ifstream file(P3_GOLDEN_PATH, std::ios::binary);
    JsonIn jsin(file);
    JsonObject golden = jsin.get_object();
    golden.allow_omitted_members();
    JsonObject items = golden.get_object("items");
    items.allow_omitted_members();
    std::set<std::string> out;
    for (const JsonMember member : items) { out.insert(member.name()); }
    return out;
}

} // namespace

TEST_CASE("p3 golden load batches", "[p3][golden][batches]") {
    for (const batch_file& batch : read_batches()) {
        size_t in_golden = 0;
        size_t loaded = 0;
        std::vector<std::string> skipped;
        const std::vector<std::string> failures =
            p3_golden_compare_ids(batch.ids, in_golden, loaded, skipped);
        std::cout << "batch " << batch.name << ": ids " << batch.ids.size() << " loaded " << loaded
                  << " skipped " << skipped.size() << std::endl;
        for (const std::string& id : skipped) { std::cout << "  skipped " << id << "\n"; }
        std::string head;
        for (size_t i = 0; i < failures.size() && i < 15; i++) { head += failures[i] + "\n"; }
        INFO("batch " << batch.name << ": " << failures.size() << " mismatches, first:\n" << head);
        CHECK(failures.empty());
    }
}

TEST_CASE("p3 golden batch ids are golden ids", "[p3][golden][batches]") {
    const std::set<std::string> golden = golden_ids();
    for (const batch_file& batch : read_batches()) {
        for (const std::string& id : batch.ids) {
            INFO("batch " << batch.name << " id " << id);
            CHECK(golden.count(id) == 1);
        }
    }
}

TEST_CASE("p3 golden selftest batch", "[p3][golden][batches]") {
    const batch_file batch = read_batch("_selftest");
    REQUIRE(batch.ids.size() == 19);
    size_t in_golden = 0;
    size_t loaded = 0;
    std::vector<std::string> skipped;
    const std::vector<std::string> failures =
        p3_golden_compare_ids(batch.ids, in_golden, loaded, skipped);
    std::string head;
    for (size_t i = 0; i < failures.size() && i < 22; i++) { head += failures[i] + "\n"; }
    INFO(failures.size() << " mismatches, first:\n" << head);
    // a batch that loads nothing would pass G2 vacuously: pin the loaded count
    CHECK(in_golden == 19);
    CHECK(loaded == 19);
    CHECK(skipped.empty());
    CHECK(failures.empty());
}
