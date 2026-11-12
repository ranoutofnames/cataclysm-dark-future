#include "catch/catch.hpp"
#include "json.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "flag.h"
#include "itype.h"
#include "pocket_data.h"
#include "type_id.h"

// P3-7-BATCH1: generic cases over every `tests/data/dda_staged/p3_batches/<batch>.json` and
// `neg-<batch>.json`. A 7-* data packet copies its two files there and edits data only; it writes
// no comparison code. Batch file format: tests/data/dda_staged/README.md, section p3_batches.
//
// Members a row lacks are skipped, counted and printed, never silent. B1, B2 and B5 fail when they
// checked nothing at all. B3 and B4 check nothing until a batch carries their input (an id with two
// or more golden pockets; a row with `own_keys`): they print `checked 0` and pass until then.

// Defined in cdf_mag_port_test.cpp.
auto p3_golden_order_ids(const std::set<std::string>& ids, size_t& in_golden, size_t& loaded,
                         std::vector<std::string>& skipped) -> std::vector<std::string>;
auto p3_golden_compare_pockets(const std::string& id, const std::string& pockets_json,
                               bool& was_loaded) -> std::vector<std::string>;

namespace {

const std::string BATCH_GOLDEN_PATH = "tests/data/dda_staged/p3_golden.json";
const std::string BATCH_DIR = "tests/data/dda_staged/p3_batches";
const std::string NEG_PREFIX = "neg-";

auto batch_paths(bool negative) -> std::vector<std::filesystem::path> {
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(BATCH_DIR)) {
        if (entry.path().extension() != ".json") { continue; }
        const bool is_neg = entry.path().filename().string().rfind(NEG_PREFIX, 0) == 0;
        if (is_neg == negative) { paths.push_back(entry.path()); }
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

// Calls f(batch name, items object) for every batch file (not the neg- files).
template <typename F>
void each_batch(F&& f) {
    const std::vector<std::filesystem::path> paths = batch_paths(false);
    REQUIRE_FALSE(paths.empty());
    for (const std::filesystem::path& path : paths) {
        std::ifstream file(path, std::ios::binary);
        JsonIn jsin(file);
        JsonObject root = jsin.get_object();
        root.allow_omitted_members();
        JsonObject items = root.get_object("items");
        items.allow_omitted_members();
        f(root.get_string("batch"), items);
    }
}

auto row_object(const JsonMember& member) -> JsonObject {
    JsonObject row = member.get_object();
    row.allow_omitted_members();
    return row;
}

// `id@file` names a duplicate id: the id proper is the part before the '@'.
auto plain_id(const std::string& key) -> std::string { return key.substr(0, key.find('@')); }

// Rows the owner's rule parks (the CDF record keeps its legacy keys on purpose).
auto row_is_parked(const JsonObject& row) -> bool {
    const std::string action = row.has_string("action") ? row.get_string("action") : "";
    const std::string suffix = "-PARKED";
    const bool parked_action = action.size() >= suffix.size() &&
                               action.compare(action.size() - suffix.size(), suffix.size(),
                                              suffix) == 0;
    return parked_action || row.has_member("well_not_applied");
}

// The top-level keys of every record of a data file, by its `id` / `abstract`.
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
        for (const char* member : {"id", "abstract"}) {
            if (entry.has_string(member)) {
                records[entry.get_string(member)].push_back(keys);
            }
        }
    }
    return cache.emplace(path, records).first->second;
}

// Returns the one record of `id` in `path`, or nullptr (none, or more than one).
auto own_record(const std::string& path, const std::string& id) -> const std::set<std::string>* {
    const record_keys& records = file_records(path);
    const auto found = records.find(id);
    if (found == records.end() || found->second.size() != 1) { return nullptr; }
    return &found->second.front();
}

} // namespace

// B1: the legacy keys a row says it dropped are gone from the own record.
TEST_CASE("p3 batch legacy keys gone", "[p3][data][batch]") {
    size_t checked_total = 0;
    each_batch([&](const std::string& name, const JsonObject& items) {
        size_t ids = 0;
        size_t checked = 0;
        size_t skipped = 0;
        std::vector<std::string> parked;
        for (const JsonMember member : items) {
            ids++;
            const JsonObject row = row_object(member);
            if (!row.has_string("file") || !row.has_array("dropped_keys") ||
                row.get_array("dropped_keys").size() == 0) {
                skipped++;
                continue;
            }
            if (row_is_parked(row)) {
                skipped++;
                parked.push_back(member.name());
                continue;
            }
            checked++;
            const std::string id = plain_id(member.name());
            const std::set<std::string>* record = own_record(row.get_string("file"), id);
            INFO("batch " << name << " id " << id << " file " << row.get_string("file"));
            REQUIRE(record != nullptr);
            for (const std::string& key : row.get_string_array("dropped_keys")) {
                INFO("dropped key " << key);
                CHECK(record->count(key) == 0);
            }
        }
        std::cout << "batch " << name << ": ids " << ids << " checked " << checked << " skipped "
                  << skipped << std::endl;
        for (const std::string& id : parked) {
            std::cout << "  parked " << id << " (legacy keys kept by the owner's rule)\n";
        }
        checked_total += checked;
    });
    // a case that read nothing would pass vacuously
    CHECK(checked_total > 0);
}

namespace {

// Does the loaded `type` list `id` in the pocket field `where`?
auto loaded_pockets_list(const itype& type, const std::string& where, const std::string& id,
                         bool& known_field) -> bool {
    known_field = true;
    for (const pocket_data& pocket : type.pockets) {
        if (where == "item_restriction") {
            if (pocket.item_id_restriction.count(itype_id(id)) > 0) { return true; }
        } else if (where == "allowed_speedloaders") {
            if (pocket.allowed_speedloaders.count(itype_id(id)) > 0) { return true; }
        } else if (where == "ammo_restriction") {
            if (pocket.ammo_restriction.count(ammotype(id)) > 0) { return true; }
        } else if (where == "flag_restriction") {
            if (pocket.get_flag_restrictions().count(flag_id(id)) > 0) { return true; }
        } else if (where == "default_magazine") {
            if (pocket.default_magazine == itype_id(id)) { return true; }
        } else {
            known_field = false;
            return false;
        }
    }
    return false;
}

} // namespace

// B2: ids a row says it dropped are not in the loaded pocket field they were dropped from; where a
// row has `well_diff.cdf_added` (none in the batches so far), those ids are in the loaded well.
TEST_CASE("p3 batch kept and dropped ids", "[p3][data][batch]") {
    size_t checked_total = 0;
    each_batch([&](const std::string& name, const JsonObject& items) {
        size_t ids = 0;
        size_t checked = 0;
        size_t skipped = 0;
        for (const JsonMember member : items) {
            ids++;
            const JsonObject row = row_object(member);
            const bool has_dropped =
                row.has_array("dropped_ids") && row.get_array("dropped_ids").size() > 0;
            // the well_diff object is read in full by the checks below: a named copy so its
            // omitted members (dda_only_kept, unfit) are not reported as unvisited here
            const bool has_added = [&row]() {
                if (!row.has_object("well_diff")) {
                    return false;
                }
                JsonObject well_diff_probe = row.get_object("well_diff");
                well_diff_probe.allow_omitted_members();
                return well_diff_probe.has_array("cdf_added") &&
                       well_diff_probe.get_array("cdf_added").size() > 0;
            }();
            if (!has_dropped && !has_added) {
                skipped++;
                continue;
            }
            const itype_id item_id(plain_id(member.name()));
            INFO("batch " << name << " id " << member.name());
            if (!item_id.is_valid()) {
                // an unloaded item cannot be checked: never silent, and a failure
                FAIL("item is not loaded: " << member.name());
            }
            checked++;
            const itype& type = item_id.obj();
            if (has_dropped) {
                for (JsonObject dropped : row.get_array("dropped_ids")) {
                    dropped.allow_omitted_members();
                    const std::string where = dropped.get_string("where");
                    const std::string dropped_id = dropped.get_string("id");
                    bool known_field = true;
                    const bool present = loaded_pockets_list(type, where, dropped_id, known_field);
                    INFO("dropped id " << dropped_id << " from " << where);
                    REQUIRE(known_field);
                    CHECK_FALSE(present);
                }
            }
            if (has_added) {
                JsonObject well_diff = row.get_object("well_diff");
                well_diff.allow_omitted_members();
                for (const std::string& added : well_diff.get_string_array("cdf_added")) {
                    bool known_field = true;
                    INFO("cdf_added id " << added);
                    CHECK(loaded_pockets_list(type, "item_restriction", added, known_field));
                }
            }
        }
        std::cout << "batch " << name << ": ids " << ids << " checked " << checked << " skipped "
                  << skipped << std::endl;
        checked_total += checked;
    });
    CHECK(checked_total > 0);
}

// B3: ids with two or more golden pockets keep the golden pocket order (R9). No merged batch has
// such an id yet; the case then prints `checked 0` and passes.
TEST_CASE("p3 batch pocket order", "[p3][data][batch]") {
    each_batch([&](const std::string& name, const JsonObject& items) {
        std::set<std::string> ids;
        for (const JsonMember member : items) {
            row_object(member);
            ids.insert(plain_id(member.name()));
        }
        size_t in_golden = 0;
        size_t loaded = 0;
        std::vector<std::string> skipped_ids;
        const std::vector<std::string> failures =
            p3_golden_order_ids(ids, in_golden, loaded, skipped_ids);
        std::cout << "batch " << name << ": ids " << ids.size() << " checked " << loaded
                  << " skipped " << ids.size() - loaded << std::endl;
        std::string head;
        for (size_t i = 0; i < failures.size() && i < 15; i++) { head += failures[i] + "\n"; }
        INFO("batch " << name << ": " << failures.size() << " order mismatches, first:\n" << head);
        CHECK(failures.empty());
        // a multi-pocket id that did not load was not checked
        INFO("not loaded: " << skipped_ids.size());
        CHECK(in_golden == loaded);
    });
}

// B4: rows that name their `own_keys` (the key set of the CDF record they must keep) still have
// exactly that key set. None of the merged batches carries `own_keys`: counted as skipped.
TEST_CASE("p3 batch rows not edited", "[p3][data][batch]") {
    each_batch([&](const std::string& name, const JsonObject& items) {
        size_t ids = 0;
        size_t checked = 0;
        size_t skipped = 0;
        for (const JsonMember member : items) {
            ids++;
            const JsonObject row = row_object(member);
            if (!row.has_string("file") || !row.has_array("own_keys")) {
                skipped++;
                continue;
            }
            checked++;
            const std::string id = plain_id(member.name());
            const std::set<std::string>* record = own_record(row.get_string("file"), id);
            INFO("batch " << name << " id " << id << " file " << row.get_string("file"));
            REQUIRE(record != nullptr);
            const std::vector<std::string> listed = row.get_string_array("own_keys");
            const std::set<std::string> expected(listed.begin(), listed.end());
            CHECK(*record == expected);
        }
        std::cout << "batch " << name << ": ids " << ids << " checked " << checked << " skipped "
                  << skipped << std::endl;
    });
}

namespace {

// A minimal mutable JSON tree for the in-memory copy of one golden item (the golden is plain JSON:
// objects, arrays, strings, numbers, booleans).
struct jnode {
    enum class kind_t { OBJ, ARR, STR, RAW };
    kind_t kind = kind_t::RAW;
    std::string text;               // STR: the unescaped string; RAW: the number / bool token
    std::vector<std::string> names; // OBJ: member names, parallel to `children`
    std::vector<jnode> children;    // OBJ members or ARR elements

    auto find(const std::string& name) -> jnode* {
        for (size_t i = 0; i < names.size(); i++) {
            if (names[i] == name) { return &children[i]; }
        }
        return nullptr;
    }
    void erase(const std::string& name) {
        for (size_t i = 0; i < names.size(); i++) {
            if (names[i] == name) {
                names.erase(names.begin() + i);
                children.erase(children.begin() + i);
                return;
            }
        }
    }
    void set(const std::string& name, const jnode& value) {
        if (jnode* existing = find(name)) {
            *existing = value;
        } else {
            names.push_back(name);
            children.push_back(value);
        }
    }
};

auto make_string(const std::string& text) -> jnode {
    jnode node;
    node.kind = jnode::kind_t::STR;
    node.text = text;
    return node;
}

class jparser {
    public:
        explicit jparser(const std::string& source) : src(source) {}
        auto parse() -> jnode {
            jnode node = value();
            skip();
            REQUIRE(pos == src.size());
            return node;
        }

    private:
        const std::string& src;
        size_t pos = 0;

        void skip() {
            while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos])) != 0) {
                pos++;
            }
        }
        auto string_token() -> std::string {
            REQUIRE(src[pos] == '"');
            pos++;
            std::string out;
            while (src[pos] != '"') {
                if (src[pos] == '\\') {
                    pos++;
                    switch (src[pos]) {
                        case 'n': out += '\n'; break;
                        case 't': out += '\t'; break;
                        case 'r': out += '\r'; break;
                        case 'b': out += '\b'; break;
                        case 'f': out += '\f'; break;
                        case 'u':
                            FAIL("\\u escape in the golden is not handled by the mini parser");
                            break;
                        default: out += src[pos]; break;
                    }
                } else {
                    out += src[pos];
                }
                pos++;
            }
            pos++;
            return out;
        }
        auto value() -> jnode {
            skip();
            jnode node;
            if (src[pos] == '{') {
                node.kind = jnode::kind_t::OBJ;
                pos++;
                skip();
                while (src[pos] != '}') {
                    skip();
                    node.names.push_back(string_token());
                    skip();
                    REQUIRE(src[pos] == ':');
                    pos++;
                    node.children.push_back(value());
                    skip();
                    if (src[pos] == ',') { pos++; }
                    skip();
                }
                pos++;
            } else if (src[pos] == '[') {
                node.kind = jnode::kind_t::ARR;
                pos++;
                skip();
                while (src[pos] != ']') {
                    node.children.push_back(value());
                    skip();
                    if (src[pos] == ',') { pos++; }
                    skip();
                }
                pos++;
            } else if (src[pos] == '"') {
                node.kind = jnode::kind_t::STR;
                node.text = string_token();
            } else {
                const size_t start = pos;
                while (pos < src.size() && src[pos] != ',' && src[pos] != '}' && src[pos] != ']' &&
                       std::isspace(static_cast<unsigned char>(src[pos])) == 0) {
                    pos++;
                }
                node.text = src.substr(start, pos - start);
            }
            return node;
        }
};

void write_node(const jnode& node, std::string& out) {
    switch (node.kind) {
        case jnode::kind_t::OBJ:
            out += '{';
            for (size_t i = 0; i < node.children.size(); i++) {
                if (i > 0) { out += ','; }
                out += '"' + node.names[i] + "\":";
                write_node(node.children[i], out);
            }
            out += '}';
            break;
        case jnode::kind_t::ARR:
            out += '[';
            for (size_t i = 0; i < node.children.size(); i++) {
                if (i > 0) { out += ','; }
                write_node(node.children[i], out);
            }
            out += ']';
            break;
        case jnode::kind_t::STR:
            out += '"';
            for (const char c : node.text) {
                if (c == '"' || c == '\\') { out += '\\'; }
                out += c;
            }
            out += '"';
            break;
        case jnode::kind_t::RAW:
            out += node.text;
            break;
    }
}

auto golden_item_pockets(const std::string& id) -> jnode {
    static jnode golden_items;
    static bool read = false;
    if (!read) {
        std::ifstream file(BATCH_GOLDEN_PATH, std::ios::binary);
        REQUIRE(file.good());
        std::stringstream text;
        text << file.rdbuf();
        const std::string source = text.str();
        jnode root = jparser(source).parse();
        jnode* items = root.find("items");
        REQUIRE(items != nullptr);
        golden_items = *items;
        read = true;
    }
    jnode* item = golden_items.find(id);
    REQUIRE(item != nullptr); // a negative fixture names a golden id
    jnode* pockets = item->find("pockets");
    REQUIRE(pockets != nullptr);
    return *pockets;
}

auto first_backticked(const std::string& text) -> std::string {
    const size_t open = text.find('`');
    if (open == std::string::npos) { return {}; }
    const size_t close = text.find('`', open + 1);
    return close == std::string::npos ? std::string() : text.substr(open + 1, close - open - 1);
}

auto find_member_index(const jnode& obj, const std::string& name) -> size_t {
    for (size_t i = 0; i < obj.names.size(); i++) {
        if (obj.names[i] == name) { return i; }
    }
    return obj.names.size();
}

// Applies one mutation of the neg file to the in-memory golden pockets. Returns false when the
// mutation does not apply to that pocket (a bad fixture), which fails the case.
auto apply_mutation(jnode& pockets, const std::string& kind, size_t pocket_index,
                    const std::string& mutation) -> bool {
    if (kind == "pocket_order") {
        if (pockets.children.size() < 2) { return false; }
        std::swap(pockets.children[0], pockets.children[1]);
        return true;
    }
    if (pocket_index >= pockets.children.size()) { return false; }
    jnode& pocket = pockets.children[pocket_index];
    const std::string named = first_backticked(mutation);
    if (kind == "cap_plus_one") {
        jnode* ammo = pocket.find("ammo_restriction");
        if (ammo == nullptr || ammo->children.empty()) { return false; }
        size_t index = find_member_index(*ammo, named);
        if (index == ammo->names.size()) { index = 0; }
        jnode& cap = ammo->children[index];
        cap.text = std::to_string(std::stoi(cap.text) + 1);
        return true;
    }
    if (kind == "well_volume") {
        if (pocket.find("magazine_well") == nullptr) { return false; }
        pocket.set("magazine_well", make_string("1 ml"));
        return true;
    }
    if (kind == "drop_restriction_id" || kind == "drop_speedloader") {
        const std::string field =
            kind == "drop_speedloader" ? "allowed_speedloaders" : "item_restriction";
        jnode* list = pocket.find(field);
        if (list == nullptr || list->children.empty()) { return false; }
        size_t index = 0;
        for (size_t i = 0; i < list->children.size(); i++) {
            if (!named.empty() && list->children[i].text == named) { index = i; }
        }
        list->children.erase(list->children.begin() + index);
        return true;
    }
    if (kind == "add_bogus_id") {
        jnode* list = pocket.find("item_restriction");
        if (list == nullptr) { return false; }
        list->children.push_back(make_string("p3_bogus_item_id"));
        return true;
    }
    if (kind == "wrong_default") {
        if (pocket.find("default_magazine") == nullptr) { return false; }
        pocket.set("default_magazine", make_string("p3_bogus_item_id"));
        return true;
    }
    if (kind == "flag") {
        jnode* list = pocket.find("flag_restriction");
        if (list == nullptr) {
            jnode fresh;
            fresh.kind = jnode::kind_t::ARR;
            pocket.set("flag_restriction", fresh);
            list = pocket.find("flag_restriction");
        }
        list->children.push_back(make_string("P3_BOGUS_FLAG"));
        return true;
    }
    if (kind == "hand_fallback_integral") {
        // the golden says integral and the loaded item must say so: flip the shape (integral
        // MAGAZINE <-> MAGAZINE_WELL) so that the loaded pocket no longer matches
        jnode* type = pocket.find("pocket_type");
        if (type == nullptr) { return false; }
        type->text = type->text == "MAGAZINE" ? "MAGAZINE_WELL" : "MAGAZINE";
        return true;
    }
    return false;
}

} // namespace

// B5: every mutation of every neg file, applied to an in-memory copy of the golden item, makes the
// comparator report a mismatch for exactly that id (the comparator is not blind to that kind).
TEST_CASE("p3 batch negative mutations", "[p3][data][batch]") {
    std::set<std::string> batch_names;
    each_batch([&](const std::string& name, const JsonObject&) { batch_names.insert(name); });

    size_t checked_total = 0;
    for (const std::filesystem::path& path : batch_paths(true)) {
        std::ifstream file(path, std::ios::binary);
        JsonIn jsin(file);
        JsonObject root = jsin.get_object();
        root.allow_omitted_members();
        const std::string batch = root.get_string("batch");
        INFO("neg file " << path.filename().string());
        CHECK(batch_names.count(batch) == 1);
        JsonArray mutations = root.get_array("mutations");
        size_t listed = 0;
        size_t checked = 0;
        for (JsonObject mutation : mutations) {
            mutation.allow_omitted_members();
            listed++;
            const std::string id = mutation.get_string("id");
            const std::string kind = mutation.get_string("kind");
            INFO("case " << mutation.get_string("case") << " kind " << kind << " id " << id);

            jnode pockets = golden_item_pockets(id);
            std::string clean;
            write_node(pockets, clean);
            bool was_loaded = false;
            const std::vector<std::string> baseline =
                p3_golden_compare_pockets(id, clean, was_loaded);
            REQUIRE(was_loaded);
            // the unmutated golden item must match, or a mismatch proves nothing
            INFO("baseline mismatch: " << (baseline.empty() ? std::string() : baseline.front()));
            REQUIRE(baseline.empty());

            REQUIRE(apply_mutation(pockets, kind, static_cast<size_t>(mutation.get_int("pocket")),
                                   mutation.get_string("mutation")));
            std::string mutated;
            write_node(pockets, mutated);
            REQUIRE(mutated != clean);
            const std::vector<std::string> failures =
                p3_golden_compare_pockets(id, mutated, was_loaded);
            REQUIRE(was_loaded);
            INFO("mutation " << mutation.get_string("mutation"));
            CHECK_FALSE(failures.empty());
            for (const std::string& failure : failures) {
                CHECK(failure.rfind(id + ":", 0) == 0);
            }
            checked++;
        }
        std::cout << "neg " << batch << ": mutations " << listed << " checked " << checked
                  << std::endl;
        CHECK(listed > 0);
        checked_total += checked;
    }
    CHECK(checked_total > 0);
}
