// COMPAT2: a DDA mod lists "dda" (the DDA core mod id) as a dependency. CDF has no dda mod,
// so the id resolves to the CDF core pack "bn" (AUTO-239/240, COMPAT-DDA-PROBE1 step 2).
// Unknown dependency ids still fail. Fixtures: tests/data/mod_compat/.
#include "catch/catch.hpp"
#include "dependency_tree.h"
#include "mod_manager.h"
#include "type_id.h"

#include <map>
#include <string>
#include <vector>

static const std::string COMPAT_FIXTURE_DIR = "tests/data/mod_compat";

static auto load_compat_tree() -> dependency_tree {
    const std::vector<MOD_INFORMATION> mods = mod_management::load_mods_from(COMPAT_FIXTURE_DIR);

    std::map<mod_id, std::vector<mod_id>> dependency_map;
    std::map<mod_id, std::vector<mod_id>> conflict_map;
    for (const MOD_INFORMATION& mi : mods) {
        dependency_map[mi.ident] = mi.dependencies;
        conflict_map[mi.ident] = mi.conflicts;
    }

    dependency_tree tree;
    tree.init(dependency_map, conflict_map);
    return tree;
}

static auto find_fixture(const std::string& ident) -> const MOD_INFORMATION* {
    static const std::vector<MOD_INFORMATION> mods = mod_management::load_mods_from(
        COMPAT_FIXTURE_DIR);
    for (const MOD_INFORMATION& mi : mods) {
        if (mi.ident == mod_id(ident)) { return &mi; }
    }
    return nullptr;
}

TEST_CASE("dda_dependency_alias_resolves_to_bn", "[mod][compat][p3]") {
    const MOD_INFORMATION* mi = find_fixture("compat_dda_alias");
    REQUIRE(mi != nullptr);
    REQUIRE(mi->dependencies.size() == 1);
    CHECK(mi->dependencies.front() == mod_management::get_default_core_content_pack());

    const dependency_tree tree = load_compat_tree();
    CHECK(tree.is_available(mod_id("bn")));
    CHECK(tree.is_available(mod_id("compat_dda_alias")));
}

TEST_CASE("unknown_dependency_still_fails", "[mod][compat][p3]") {
    const MOD_INFORMATION* mi = find_fixture("compat_unknown_dep");
    REQUIRE(mi != nullptr);
    REQUIRE(mi->dependencies.size() == 1);
    CHECK(mi->dependencies.front() == mod_id("no_such_mod"));

    const dependency_tree tree = load_compat_tree();
    CHECK_FALSE(tree.is_available(mod_id("compat_unknown_dep")));
}

TEST_CASE("bn_and_dda_together_resolve_once", "[mod][compat][p3]") {
    const MOD_INFORMATION* mi = find_fixture("compat_both");
    REQUIRE(mi != nullptr);
    CHECK(mi->dependencies.size() == 1);
    CHECK(mi->dependencies.front() == mod_management::get_default_core_content_pack());

    const dependency_tree tree = load_compat_tree();
    CHECK(tree.is_available(mod_id("compat_both")));
}
