#include "catch/catch.hpp"
#include "filesystem.h"
#include "input.h"
#include "path_info.h"

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

namespace {

class user_keybindings_file_guard {
public:
    ~user_keybindings_file_guard() {
        remove_file(PATH_INFO::user_keybindings());
        inp_mngr.init();
    }
};

auto write_user_keybindings_fixture(const std::string& fixture) -> void {
    std::ifstream in("tests/data/kbcheck/" + fixture, std::ios::binary);
    REQUIRE(in.good());
    std::ofstream out(PATH_INFO::user_keybindings(), std::ios::binary);
    REQUIRE(out.good());
    out << in.rdbuf();
    REQUIRE(out.good());
}

auto contains(const std::vector<std::string>& values, const std::string& value) -> bool {
    return std::find(values.begin(), values.end(), value) != values.end();
}

} // namespace

// AT2: the startup self-check must show the user-file overrides without
// reporting a required action as missing.
TEST_CASE("keybinding self-check reports a user override", "[audit][kb]") {
    auto restore_keybindings = user_keybindings_file_guard();
    write_user_keybindings_fixture("override_next_column.json");
    inp_mngr.init();

    const keybinding_self_check result = inp_mngr.check_keybindings();
    CHECK(contains(result.overridden_action_ids, "NEXT_COLUMN"));
    CHECK(contains(result.override_contexts, "INVENTORY"));
    CHECK(result.missing_required_actions.empty());
}

// AT2 seeded defect: a user file that deletes TOGGLE_ENTRY must be reported.
TEST_CASE("keybinding self-check reports a deleted required action", "[audit][kb]") {
    auto restore_keybindings = user_keybindings_file_guard();
    write_user_keybindings_fixture("delete_toggle_entry.json");
    inp_mngr.init();

    const keybinding_self_check result = inp_mngr.check_keybindings();
    CHECK(result.missing_required_actions == std::vector<std::string>{"TOGGLE_ENTRY"});
}
