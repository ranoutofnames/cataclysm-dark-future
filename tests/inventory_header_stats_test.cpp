// CDF-authored tests for the inventory header stats (P2-5a, DDA inventory_ui.cpp:3085-3122):
// Free/Max pocket lines and the pocket constraints behind them.
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "inventory_ui.h"
#include "item.h"
#include "item_pocket.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "type_id.h"

#include <functional>
#include <string>
#include <vector>

static const itype_id itype_backpack_hiking("backpack_hiking");
static const itype_id itype_test_rock("test_rock");

namespace {
class header_stats_selector: public inventory_selector {
public:
    explicit header_stats_selector(player& p): inventory_selector(p) {}
    auto lines() const -> std::vector<std::string> { return get_stats(); }
};

auto all_container_pockets(const Character& who) -> std::vector<pocket_with_constraint> {
    return who.get_all_pockets_with_constraints(
        [](const item_pocket& p) { return p.is_type(pocket_type::CONTAINER); },
        [](const item_pocket&) { return true; });
}

auto wear_pack() -> item& {
    avatar& u = get_avatar();
    clear_avatar();
    detached_ptr<item> pack = item::spawn(itype_backpack_hiking, calendar::turn);
    item& ref = *pack;
    u.wear_item(std::move(pack), false);
    return ref;
}
} // namespace

TEST_CASE("header stats list free pocket space", "[inventory][ui][header_stats]") {
    clear_all_state();
    wear_pack();
    header_stats_selector sel(get_avatar());
    const std::vector<std::string> lines = sel.lines();
    REQUIRE(lines.size() >= 2);
    CHECK(lines[0].find("Bulk Volume:") != std::string::npos);
    CHECK(lines[1].find("Free:") != std::string::npos);
}

TEST_CASE("pocket constraints skip nothing by themselves", "[inventory][ui][header_stats]") {
    clear_all_state();
    wear_pack();
    const std::vector<pocket_with_constraint> pockets = all_container_pockets(get_avatar());
    CHECK_FALSE(pockets.empty());
    for (const pocket_with_constraint& pwc : pockets) {
        CHECK(pwc.pocket != nullptr);
        CHECK(pwc.constraint.volume_capacity > 0_ml);
    }
}

TEST_CASE("header stats ignore disabled and whitelisted pockets", "[inventory][ui][header_stats]") {
    clear_all_state();
    item& pack = wear_pack();
    header_stats_selector sel(get_avatar());
    const std::vector<std::string> before = sel.lines();

    SECTION("disabled pockets do not count") {
        for (item_pocket* p : pack.get_container_pockets()) { p->settings.set_disabled(true); }
        const std::vector<std::string> after = header_stats_selector(get_avatar()).lines();
        CHECK(after[1] != before[1]);
    }
    SECTION("whitelisted pockets are not listed as spaces") {
        for (item_pocket* p : pack.get_container_pockets()) {
            p->settings.whitelist_item(itype_test_rock);
        }
        const std::vector<std::string> after = header_stats_selector(get_avatar()).lines();
        CHECK(after[1] != before[1]);
    }
}
