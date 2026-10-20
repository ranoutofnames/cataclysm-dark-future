#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "inventory_ui.h"
#include "item.h"
#include "item_category.h"
#include "item_pocket.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "type_id.h"

#include <vector>

TEST_CASE(
    "inventory selector restores consume selection by item type", "[inventory][ui][consume]") {
    clear_avatar();
    auto& dummy = get_avatar();

    auto bandages = item::spawn("bandages");
    auto heroin = item::spawn("heroin");

    auto selector = inventory_selector(dummy);
    selector.add_item(selector.own_inv_column, heroin.get());
    selector.add_item(selector.own_inv_column, bandages.get());

    REQUIRE(selector.select_item_type(bandages->typeId()));
    CHECK(selector.get_selected().any_item()->typeId() == bandages->typeId());
}

TEST_CASE(
    "inventory selector restores saved position before same type in another column",
    "[inventory][ui][consume]") {
    clear_avatar();
    auto& dummy = get_avatar();

    auto inventory_bandages = item::spawn("bandages");
    auto map_bandages = item::spawn("bandages");

    auto selector = inventory_selector(dummy);
    selector.add_item(selector.own_inv_column, inventory_bandages.get());
    selector.add_item(selector.map_column, map_bandages.get());

    REQUIRE(selector.select_item_type(map_bandages->typeId(), 1));
    const auto saved_position = selector.get_selection_position();
    REQUIRE(selector.select(inventory_bandages.get()));

    REQUIRE(selector.restore_selection(saved_position, map_bandages->typeId()));
    CHECK(selector.get_selected().any_item() == map_bandages.get());
}

TEST_CASE(
    "inventory selector rejects saved position when the type changed", "[inventory][ui][consume]") {
    clear_avatar();
    auto& dummy = get_avatar();

    auto inventory_bandages = item::spawn("bandages");
    auto map_heroin = item::spawn("heroin");

    auto selector = inventory_selector(dummy);
    selector.add_item(selector.own_inv_column, inventory_bandages.get());
    selector.add_item(selector.map_column, map_heroin.get());

    REQUIRE(selector.select_item_type(map_heroin->typeId(), 1));
    const auto stale_position = selector.get_selection_position();
    REQUIRE(selector.select(inventory_bandages.get()));

    CHECK_FALSE(
        selector.select_position_if_item_type(stale_position, inventory_bandages->typeId()));
    CHECK(selector.get_selected().any_item() == inventory_bandages.get());
}

TEST_CASE(
    "inventory selector restores saved row before same type fallback in the same column",
    "[inventory][ui][consume]") {
    clear_avatar();
    auto& dummy = get_avatar();

    auto inventory_bandages = item::spawn("bandages");
    auto first_map_bandages = item::spawn("bandages");
    auto second_map_bandages = item::spawn("bandages");

    auto selector = inventory_selector(dummy);
    selector.add_item(selector.own_inv_column, inventory_bandages.get());
    selector.add_item(selector.map_column, first_map_bandages.get());
    selector.add_item(selector.map_column, second_map_bandages.get());

    REQUIRE(selector.select_item_type(first_map_bandages->typeId(), 1));
    REQUIRE(selector.select(second_map_bandages.get()));
    const auto saved_position = selector.get_selection_position();
    REQUIRE(selector.select(inventory_bandages.get()));

    REQUIRE(selector.restore_selection(saved_position, second_map_bandages->typeId()));
    CHECK(selector.get_selected().any_item() == second_map_bandages.get());
}

TEST_CASE(
    "inventory selector type fallback starts in the saved column", "[inventory][ui][consume]") {
    clear_avatar();
    auto& dummy = get_avatar();

    auto inventory_bandages = item::spawn("bandages");
    auto map_heroin = item::spawn("heroin");
    auto map_bandages = item::spawn("bandages");

    auto selector = inventory_selector(dummy);
    selector.add_item(selector.own_inv_column, inventory_bandages.get());
    selector.add_item(selector.map_column, map_heroin.get());
    selector.add_item(selector.map_column, map_bandages.get());

    REQUIRE(selector.select_item_type(map_heroin->typeId(), 1));
    const auto stale_position = selector.get_selection_position();
    REQUIRE(selector.select(inventory_bandages.get()));

    REQUIRE(selector.restore_selection(stale_position, map_bandages->typeId()));
    CHECK(selector.get_selected().any_item() == map_bandages.get());
}

TEST_CASE(
    "inventory selector type fallback leaves saved column when needed",
    "[inventory][ui][consume]") {
    clear_avatar();
    auto& dummy = get_avatar();

    auto inventory_bandages = item::spawn("bandages");
    auto map_heroin = item::spawn("heroin");

    auto selector = inventory_selector(dummy);
    selector.add_item(selector.own_inv_column, inventory_bandages.get());
    selector.add_item(selector.map_column, map_heroin.get());

    REQUIRE(selector.select_item_type(map_heroin->typeId(), 1));
    const auto stale_position = selector.get_selection_position();

    REQUIRE(selector.restore_selection(stale_position, inventory_bandages->typeId()));
    CHECK(selector.get_selected().any_item() == inventory_bandages.get());
}

TEST_CASE(
    "inventory selector type fallback resumes default order after saved column",
    "[inventory][ui][consume]") {
    clear_avatar();
    auto& dummy = get_avatar();

    auto inventory_bandages = item::spawn("bandages");
    auto gear_bandages = item::spawn("bandages");
    auto map_heroin = item::spawn("heroin");

    auto selector = inventory_selector(dummy);
    selector.add_item(selector.own_inv_column, inventory_bandages.get());
    selector.add_item(selector.own_gear_column, gear_bandages.get());
    selector.add_item(selector.map_column, map_heroin.get());

    REQUIRE(selector.select_item_type(map_heroin->typeId(), 1));
    const auto stale_position = selector.get_selection_position();

    REQUIRE(selector.restore_selection(stale_position, inventory_bandages->typeId()));
    CHECK(selector.get_selected().any_item() == inventory_bandages.get());
}

TEST_CASE(
    "inventory selector does not restore a stale row when saved type is gone",
    "[inventory][ui][consume]") {
    clear_avatar();
    auto& dummy = get_avatar();

    auto inventory_aspirin = item::spawn("aspirin");
    auto inventory_heroin = item::spawn("heroin");
    auto map_heroin = item::spawn("heroin");
    auto missing_bandages = item::spawn("bandages");

    auto selector = inventory_selector(dummy);
    selector.add_item(selector.own_inv_column, inventory_aspirin.get());
    selector.add_item(selector.own_inv_column, inventory_heroin.get());
    selector.add_item(selector.map_column, map_heroin.get());

    REQUIRE(selector.select_item_type(map_heroin->typeId(), 1));
    const auto stale_position = selector.get_selection_position();

    selector.clear_items();
    selector.add_item(selector.own_inv_column, inventory_aspirin.get());
    selector.add_item(selector.own_inv_column, inventory_heroin.get());
    selector.add_item(selector.map_column, map_heroin.get());
    REQUIRE(selector.select_item_type(map_heroin->typeId(), 1));
    const auto expected_entries = selector.own_inv_column.get_entries([](const auto& entry) {
        return entry.is_selectable();
    });
    const auto expected_default = expected_entries.front()->any_item();

    CHECK_FALSE(selector.restore_selection(stale_position, missing_bandages->typeId()));
    CHECK(selector.get_selected().any_item() == expected_default);
}

TEST_CASE("inventory selector falls back from out of range saved row", "[inventory][ui][consume]") {
    clear_avatar();
    auto& dummy = get_avatar();

    auto inventory_bandages = item::spawn("bandages");
    auto map_bandages = item::spawn("bandages");

    auto selector = inventory_selector(dummy);
    selector.add_item(selector.own_inv_column, inventory_bandages.get());
    selector.add_item(selector.map_column, map_bandages.get());

    REQUIRE(selector.select_item_type(map_bandages->typeId(), 1));
    auto invalid_position = selector.get_selection_position();
    invalid_position.second = 999;
    REQUIRE(selector.select(inventory_bandages.get()));

    REQUIRE(selector.restore_selection(invalid_position, map_bandages->typeId()));
    CHECK(selector.get_selected().any_item() == map_bandages.get());
}

TEST_CASE(
    "inventory selector falls back from invalid saved consume column", "[inventory][ui][consume]") {
    clear_avatar();
    auto& dummy = get_avatar();

    auto inventory_bandages = item::spawn("bandages");
    auto map_bandages = item::spawn("bandages");

    auto selector = inventory_selector(dummy);
    selector.add_item(selector.own_inv_column, inventory_bandages.get());
    selector.add_item(selector.map_column, map_bandages.get());

    REQUIRE(selector.select(inventory_bandages.get()));
    const auto invalid_position = std::pair<size_t, size_t>{99, 0};

    REQUIRE(selector.restore_selection(invalid_position, map_bandages->typeId()));
    CHECK(selector.get_selected().any_item() == inventory_bandages.get());
}

TEST_CASE(
    "inventory selector populates the name cache and sorts deterministically",
    "[inventory][ui][consume]") {
    clear_avatar();
    auto& dummy = get_avatar();

    // Two comestibles share the "food" category, so their relative order is
    // decided purely by the name tiebreak in the sort comparator.
    auto orange_soda = item::spawn("orangesoda");
    auto maple_syrup = item::spawn("syrup");
    REQUIRE(orange_soda->tname(1) != maple_syrup->tname(1));

    auto selector = inventory_selector(dummy);
    // Add in non-alphabetical order so the sort has to reorder them.
    selector.add_item(selector.own_inv_column, orange_soda.get());
    selector.add_item(selector.own_inv_column, maple_syrup.get());

    // Triggers prepare_paging(): refreshes cached_name and sorts the column.
    REQUIRE(selector.select_item_type(maple_syrup->typeId()));

    const auto selectable = [](const auto& entry) { return entry.is_selectable(); };
    const auto entries = selector.own_inv_column.get_entries(selectable);
    REQUIRE(entries.size() == 2);
    // Precondition: both items must share a category so the name comparison (not the
    // category sort) decides their order; otherwise the order check below could pass
    // via category ordering and silently stop covering the regression.
    REQUIRE(entries.front()->get_category_ptr() == entries.back()->get_category_ptr());

    // Regression for #9713: PR #9038 dropped the only update_cache() call, leaving
    // cached_name empty so the alphabetic tiebreak was dead and equal-keyed comestibles
    // sorted in an unspecified order that differs across stdlib implementations.
    REQUIRE_FALSE(entries.front()->cached_name.empty());
    CHECK(entries.front()->cached_name == entries.front()->any_item()->tname(1));

    // The column is now ordered by name deterministically.
    CHECK(entries.front()->any_item()->tname(1) < entries.back()->any_item()->tname(1));
}

// CDF P2-7c: hierarchy (container tree) mode of the inventory selector
namespace {
auto item_rows(const inventory_column& col) -> std::vector<const inventory_entry*> {
    std::vector<const inventory_entry*> rows;
    for (const inventory_entry& e : col.entries) {
        if (e.is_item()) { rows.push_back(&e); }
    }
    return rows;
}
} // namespace

TEST_CASE(
    "inventory hierarchy mode lists contents under the container", "[inventory][ui][hierarchy]") {
    clear_all_state();
    avatar& u = get_avatar();
    clear_character(u, true);
    u.wear_item(item::spawn(itype_id("backpack_hiking"), calendar::turn), false);
    item& pack = *u.worn.front();
    item& rock = u.i_add(item::spawn(itype_id("test_rock"), calendar::turn));
    REQUIRE(rock.parent_item() == &pack);

    inventory_selector sel(u);
    sel.enable_view_mode_toggle();
    sel.add_character_items(u);
    sel.toggle_categorize_contained();
    sel.own_gear_column.prepare_paging();

    const std::vector<const inventory_entry*> rows = item_rows(sel.own_gear_column);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0]->any_item() == &pack);
    CHECK(rows[0]->indent == 0);
    CHECK(rows[0]->chevron);
    CHECK(rows[1]->any_item() == &rock);
    CHECK(rows[1]->indent == 2);
    CHECK(rows[1]->topmost_parent == &pack);

    SECTION("collapsed pockets hide the nested rows") {
        for (item_pocket* pocket : pack.get_standard_pockets()) {
            pocket->settings.set_collapse(true);
        }
        sel.own_gear_column.invalidate_paging();
        sel.own_gear_column.prepare_paging();
        const std::vector<const inventory_entry*> collapsed = item_rows(sel.own_gear_column);
        REQUIRE(collapsed.size() == 1);
        CHECK(collapsed[0]->any_item() == &pack);
        for (item_pocket* pocket : pack.get_standard_pockets()) {
            pocket->settings.set_collapse(false);
        }
        sel.own_gear_column.invalidate_paging();
        sel.own_gear_column.prepare_paging();
        CHECK(item_rows(sel.own_gear_column).size() == 2);
    }

    SECTION("toggling back restores the carried-group category view") {
        sel.toggle_categorize_contained();
        sel.own_gear_column.prepare_paging();
        sel.own_inv_column.prepare_paging();
        const std::vector<const inventory_entry*> gear = item_rows(sel.own_gear_column);
        REQUIRE(gear.size() == 1);
        CHECK(gear[0]->any_item() == &pack);
        CHECK_FALSE(gear[0]->chevron);
        const std::vector<const inventory_entry*> carried = item_rows(sel.own_inv_column);
        REQUIRE(carried.size() == 1);
        CHECK(carried[0]->any_item() == &rock);
        CHECK(carried[0]->indent == 0);
    }
}

TEST_CASE("inventory selector preset does not collate by default", "[inventory][p3][ui]") {
    inventory_selector_preset preset;
    CHECK_FALSE(preset.collate_entries());
}

namespace {
struct collating_preset : inventory_selector_preset {
    bool collate_entries() const override {
        return true;
    }
};

auto find_collation_header(inventory_column& col) -> inventory_entry* {
    for (inventory_entry& entry : col.entries) {
        if (entry.is_collation_header()) {
            return &entry;
        }
    }
    return nullptr;
}

// Two identical bandages and one different item.
struct collation_fixture {
    detached_ptr<item> bandage_a = item::spawn("bandages");
    detached_ptr<item> bandage_b = item::spawn("bandages");
    detached_ptr<item> rock = item::spawn("rock");
    collating_preset preset;
    inventory_column column{preset};

    collation_fixture() {
        column.add_entry(inventory_entry({bandage_a.get()}));
        column.add_entry(inventory_entry({bandage_b.get()}));
        column.add_entry(inventory_entry({rock.get()}));
        column.invalidate_paging();
    }
};
} // namespace

TEST_CASE(
    "inventory column collates identical items under one collapsed header",
    "[inventory][p3][ui]") {
    clear_avatar();
    collation_fixture fixture;
    fixture.column.prepare_paging();

    const std::vector<const inventory_entry*> rows = item_rows(fixture.column);
    REQUIRE(rows.size() == 2);

    const inventory_entry* header = find_collation_header(fixture.column);
    REQUIRE(header != nullptr);
    CHECK(header->chevron);
    CHECK(header->collation_meta->collapsed);
    CHECK(header->locations.size() == 2);

    // The other visible row is the rock.
    bool rock_visible = (rows[0]->any_item() == fixture.rock.get()) || (rows[1]->any_item() == fixture.rock.get());
    CHECK(rock_visible);

    REQUIRE(fixture.column.entries_hidden.size() == 2);
    for (const inventory_entry& hidden : fixture.column.entries_hidden) {
        CHECK(hidden.is_collation_entry());
    }
}

TEST_CASE(
    "expanding a collated header shows its entries and uncollate restores the original list",
    "[inventory][p3][ui]") {
    clear_avatar();
    collation_fixture fixture;
    fixture.column.prepare_paging();

    inventory_entry* header = find_collation_header(fixture.column);
    REQUIRE(header != nullptr);
    fixture.column.set_collapsed(*header, false);
    fixture.column.prepare_paging();

    const std::vector<const inventory_entry*> expanded = item_rows(fixture.column);
    REQUIRE(expanded.size() == 4);

    fixture.column.uncollate();
    fixture.column.prepare_paging();

    const std::vector<const inventory_entry*> restored = item_rows(fixture.column);
    REQUIRE(restored.size() == 3);
    for (const inventory_entry* entry : restored) {
        CHECK_FALSE(entry->chevron);
        CHECK_FALSE(entry->collation_meta);
    }
}

TEST_CASE(
    "collated_sort_compare keeps a header before its entries", "[inventory][p3][ui]") {
    clear_avatar();
    collation_fixture fixture;
    fixture.column.prepare_paging();

    inventory_entry* header = find_collation_header(fixture.column);
    REQUIRE(header != nullptr);
    REQUIRE(fixture.column.entries_hidden.size() == 2);
    inventory_entry* entry = &fixture.column.entries_hidden.front();
    REQUIRE(entry->is_collation_entry());
    REQUIRE(entry->collation_meta == header->collation_meta);

    CHECK(fixture.column.collated_sort_compare(*header, *entry));
    CHECK_FALSE(fixture.column.collated_sort_compare(*entry, *header));
}

TEST_CASE("inventory entry custom category replaces the natural category", "[inventory][p3][ui]") {
    auto bandages = item::spawn("bandages");
    const std::vector<item*> locations{bandages.get()};

    inventory_entry plain_entry(locations);
    const item_category* const natural_category = plain_entry.get_category_ptr();
    REQUIRE(natural_category == &bandages->get_category());

    item_category custom_category;
    plain_entry.set_custom_category(&custom_category);
    CHECK(plain_entry.get_category_ptr() == &custom_category);
    CHECK(plain_entry.get_category_ptr() != natural_category);

    // An entry that never had a custom category keeps its natural one.
    inventory_entry untouched_entry(locations);
    CHECK(untouched_entry.get_category_ptr() == natural_category);
}

TEST_CASE("inventory selector reports its only choice", "[inventory][p3][ui]") {
    clear_avatar();
    auto& dummy = get_avatar();

    auto bandages = item::spawn("bandages");

    auto selector = inventory_selector(dummy);
    selector.add_item(selector.own_inv_column, bandages.get());

    const auto only_choice = selector.get_only_choice();
    REQUIRE(only_choice.loc == bandages.get());
    // A plain selector never sets chosen_count, so the whole stack is implied.
    CHECK(only_choice.count == 0);
}
