#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "inventory_ui.h"
#include "item.h"
#include "item_category.h"
#include "item_pocket.h"
#include "json.h"
#include "map/map.h"
#include "options.h"
#include "options_helpers.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "translations.h"
#include "type_id.h"
#include "uistate.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// FM7: a selector remembers its view mode in process-wide state. A test that switches the mode puts
// the four saved states back, so that the next test still starts from the defaults.
namespace {
struct view_mode_state_guard {
    inventory_selector::uimode sel = inventory_sel_default_state.uimode;
    inventory_selector::uimode inv = inventory_ui_default_state.uimode;
    inventory_selector::uimode pickup_sel = pickup_sel_default_state.uimode;
    inventory_selector::uimode pickup_ui = pickup_ui_default_state.uimode;
    ~view_mode_state_guard() {
        inventory_sel_default_state.uimode = sel;
        inventory_ui_default_state.uimode = inv;
        pickup_sel_default_state.uimode = pickup_sel;
        pickup_ui_default_state.uimode = pickup_ui;
    }
};
} // namespace

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

    // The column is now ordered by name deterministically, in DDA's localized order (FM3).
    CHECK(localized_compare(entries.front()->cached_name, entries.back()->cached_name));
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
    const view_mode_state_guard view_mode;
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

    SECTION("toggling back puts the worn contents in the category view") {
        sel.toggle_categorize_contained();
        sel.own_gear_column.prepare_paging();
        sel.own_inv_column.prepare_paging();
        const std::vector<const inventory_entry*> gear = item_rows(sel.own_gear_column);
        REQUIRE(gear.size() == 1);
        CHECK(gear[0]->any_item() == &pack);
        // DDA FM1: add_entry_rec marks every parent whose contents are listed, in either view.
        CHECK(gear[0]->chevron);
        const std::vector<const inventory_entry*> carried = item_rows(sel.own_inv_column);
        REQUIRE(carried.size() == 1);
        CHECK(carried[0]->any_item() == &rock);
        // DDA FM1: the nested row keeps its tree indent; the category column does not apply it.
        CHECK(carried[0]->indent == 2);
    }
}

// NEST1: a worn vest with two ESAPI plates in its large plate pockets.
namespace {
struct worn_vest_fixture {
    static auto fresh_avatar() -> avatar& {
        clear_all_state();
        return get_avatar();
    }
    avatar& u = fresh_avatar();
    item* vest = nullptr;
    std::vector<item*> plates;

    worn_vest_fixture() {
        clear_character(u, true);
        u.wear_item(item::spawn(itype_id("ballistic_vest_esapi"), calendar::turn), false);
        vest = u.worn.front();
        for (int i = 0; i < 2; ++i) {
            REQUIRE(vest->put_in(item::spawn(itype_id("esapi_plate"), calendar::turn),
                                 pocket_type::CONTAINER).success());
        }
        for (item* top : vest->contents.all_items_top()) {
            plates.push_back(top);
            REQUIRE(top->parent_item() == vest);
        }
        REQUIRE(plates.size() == 2);
    }
};
} // namespace

// NEST1: armor pieces that sit in the pockets of a worn item are child rows of the worn row in the
// worn column, in the categories view too. There is no option: this is simply the listing.
TEST_CASE("worn armor pieces nest under their worn row", "[inventory][nest]") {
    const view_mode_state_guard view_mode;
    worn_vest_fixture f;
    REQUIRE(f.u.is_worn(*f.vest));

    inventory_selector sel(f.u);
    sel.add_character_items(f.u);
    sel.own_gear_column.prepare_paging();
    sel.own_inv_column.prepare_paging();

    const std::vector<const inventory_entry*> rows = item_rows(sel.own_gear_column);
    REQUIRE(rows.size() == 3);
    CHECK(rows[0]->any_item() == f.vest);
    CHECK(rows[0]->indent == 0);
    CHECK(rows[0]->chevron);
    const item_category* worn_cat = rows[0]->get_category_ptr();
    REQUIRE(worn_cat != nullptr);
    CHECK(worn_cat->get_id().str() == "ITEMS_WORN");

    // The two plates sit directly under the vest, indented, in the worn category.
    for (size_t i = 1; i < rows.size(); ++i) {
        CAPTURE(i);
        CHECK(std::find(f.plates.begin(), f.plates.end(), rows[i]->any_item()) != f.plates.end());
        CHECK(rows[i]->indent == 2);
        CHECK(rows[i]->topmost_parent == f.vest);
        CHECK(rows[i]->get_category_ptr() == worn_cat);
        CHECK_FALSE(rows[i]->chevron);
    }
    // They are not top-level rows of the inventory column.
    for (const inventory_entry* row : item_rows(sel.own_inv_column)) {
        CHECK(std::find(f.plates.begin(), f.plates.end(), row->any_item()) == f.plates.end());
    }

    SECTION("collapsing the vest hides the plate rows") {
        REQUIRE(sel.select(f.vest));
        sel.toggle_selected_contents();
        sel.own_gear_column.invalidate_paging();
        sel.own_gear_column.prepare_paging();
        const std::vector<const inventory_entry*> collapsed = item_rows(sel.own_gear_column);
        REQUIRE(collapsed.size() == 1);
        CHECK(collapsed[0]->any_item() == f.vest);

        // The filter still finds a plate inside the collapsed vest.
        sel.own_gear_column.set_filter("ESAPI");
        const std::vector<const inventory_entry*> filtered = item_rows(sel.own_gear_column);
        REQUIRE(filtered.size() == 2);
        for (const inventory_entry* row : filtered) {
            CHECK(std::find(f.plates.begin(), f.plates.end(), row->any_item()) != f.plates.end());
        }
        sel.own_gear_column.set_filter("");

        for (item_pocket* pocket : f.vest->get_standard_pockets()) {
            pocket->settings.set_collapse(false);
        }
        sel.own_gear_column.invalidate_paging();
        sel.own_gear_column.prepare_paging();
        CHECK(item_rows(sel.own_gear_column).size() == 3);
    }
}

// NEST1: the same for clothing that is not an ablative insert: a helmet in a worn bag's pocket.
TEST_CASE("worn clothing in a worn bag's pocket nests under the bag", "[inventory][nest]") {
    const view_mode_state_guard view_mode;
    clear_all_state();
    avatar& u = get_avatar();
    clear_character(u, true);
    u.wear_item(item::spawn(itype_id("backpack_hiking"), calendar::turn), false);
    item& pack = *u.worn.front();
    item& helmet = u.i_add(item::spawn(itype_id("helmet_army"), calendar::turn));
    REQUIRE(helmet.parent_item() == &pack);
    REQUIRE(helmet.is_armor());

    inventory_selector sel(u);
    sel.add_character_items(u);
    sel.own_gear_column.prepare_paging();
    sel.own_inv_column.prepare_paging();

    const std::vector<const inventory_entry*> rows = item_rows(sel.own_gear_column);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0]->any_item() == &pack);
    CHECK(rows[0]->chevron);
    CHECK(rows[1]->any_item() == &helmet);
    CHECK(rows[1]->indent == 2);
    CHECK(rows[1]->topmost_parent == &pack);
    CHECK(rows[1]->get_category_ptr()->get_id().str() == "ITEMS_WORN");
    for (const inventory_entry* row : item_rows(sel.own_inv_column)) {
        CHECK(row->any_item() != &helmet);
    }
}

// PL-64 PK1/G36: the pickup selector lists what sits inside a box on the tile as child rows, as
// DDA's _add_map_items (inventory_ui.cpp:2337-2358) does: add_entry_rec on every map item, with the
// box row carrying the chevron.
TEST_CASE("pickup selector lists the contents of a box on the tile", "[inventory][ui][fm]") {
    const view_mode_state_guard view_mode;
    clear_all_state();
    avatar& u = get_avatar();
    clear_character(u, true);
    // The pickup menu opens in the container hierarchy (pickup_ui_default_state).
    pickup_ui_default_state.uimode = inventory_selector::uimode::hierarchy;

    detached_ptr<item> box_ptr = item::spawn(itype_id("box_small"), calendar::turn);
    item& box = *box_ptr;
    REQUIRE(box.put_in(item::spawn(itype_id("test_rock"), calendar::turn),
                       pocket_type::CONTAINER).success());
    item& rock = box.contents.front();
    get_map().add_item(u.bub_pos(), std::move(box_ptr));
    REQUIRE(rock.parent_item() == &box);

    pickup_inventory_preset preset(u);
    preset.save_state = &pickup_ui_default_state;
    inventory_pickup_selector sel(u, preset);
    sel.add_map_items(u.bub_pos());
    sel.map_column.prepare_paging();

    REQUIRE(sel.map_column.indent_entries());
    const std::vector<const inventory_entry*> rows = item_rows(sel.map_column);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0]->any_item() == &box);
    CHECK(rows[0]->indent == 0);
    CHECK(rows[0]->chevron);
    CHECK(rows[1]->any_item() == &rock);
    CHECK(rows[1]->indent == 2);
    CHECK(rows[1]->topmost_parent == &box);

    SECTION("collapsing the box hides the child row") {
        for (item_pocket* pocket : box.get_standard_pockets()) {
            pocket->settings.set_collapse(true);
        }
        sel.map_column.invalidate_paging();
        sel.map_column.prepare_paging();
        const std::vector<const inventory_entry*> collapsed = item_rows(sel.map_column);
        REQUIRE(collapsed.size() == 1);
        CHECK(collapsed[0]->any_item() == &box);
        CHECK(collapsed[0]->chevron);

        for (item_pocket* pocket : box.get_standard_pockets()) {
            pocket->settings.set_collapse(false);
        }
        sel.map_column.invalidate_paging();
        sel.map_column.prepare_paging();
        CHECK(item_rows(sel.map_column).size() == 2);
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

TEST_CASE("default selector preset shows no collation rows", "[inventory][p3][ui]") {
    clear_avatar();
    auto& dummy = get_avatar();

    auto bandage_a = item::spawn("bandages");
    auto bandage_b = item::spawn("bandages");

    inventory_selector selector(dummy);
    selector.add_item(selector.own_inv_column, bandage_a.get());
    selector.add_item(selector.own_inv_column, bandage_b.get());
    selector.own_inv_column.prepare_paging();

    const std::vector<const inventory_entry*> rows = item_rows(selector.own_inv_column);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0]->any_item() == bandage_a.get());
    CHECK(rows[1]->any_item() == bandage_b.get());
    for (const inventory_entry* row : rows) {
        CHECK_FALSE(row->chevron);
        CHECK_FALSE(row->collation_meta);
    }
}

TEST_CASE("default selector select keeps base behaviour", "[inventory][p3][ui]") {
    clear_avatar();
    auto& dummy = get_avatar();

    auto first = item::spawn("bandages");
    auto second = item::spawn("bandages");

    inventory_selector selector(dummy);
    selector.add_item(selector.own_inv_column, first.get());
    selector.add_item(selector.own_inv_column, second.get());
    selector.own_inv_column.prepare_paging();

    const std::vector<const inventory_entry*> rows = item_rows(selector.own_inv_column);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0]->any_item() == first.get());
    CHECK(rows[1]->any_item() == second.get());
    for (const inventory_entry* row : rows) {
        CHECK_FALSE(row->chevron);
        CHECK_FALSE(row->collation_meta);
    }

    REQUIRE(selector.select(second.get()));
    CHECK(selector.get_selected().any_item() == second.get());

    struct denying_preset : inventory_selector_preset {
        const item* denied = nullptr;
        std::string get_denial(const item* it) const override {
            return it == denied ? "denied" : std::string();
        }
    };
    denying_preset preset;
    preset.denied = first.get();

    inventory_selector denied_selector(dummy, preset);
    denied_selector.add_item(denied_selector.own_inv_column, first.get());
    denied_selector.add_item(denied_selector.own_inv_column, second.get());
    denied_selector.own_inv_column.prepare_paging();

    CHECK_FALSE(denied_selector.select(first.get()));
    CHECK(denied_selector.get_selected().any_item() != first.get());
}

TEST_CASE("collated header caption and sibling navigation", "[inventory][p3][ui]") {
    clear_avatar();
    auto& dummy = get_avatar();

    auto bandage_a = item::spawn("bandages");
    auto bandage_b = item::spawn("bandages");

    collating_preset preset;
    inventory_selector selector(dummy, preset);
    selector.add_item(selector.own_inv_column, bandage_a.get());
    selector.add_item(selector.own_inv_column, bandage_b.get());
    REQUIRE(selector.select_item_type(bandage_a->typeId()));

    inventory_column& col = selector.own_inv_column;
    inventory_entry* header = find_collation_header(col);
    REQUIRE(header != nullptr);
    REQUIRE(header->locations.size() == 2);
    CHECK_FALSE(preset.get_cell_text(*header, 0).empty());

    col.set_collapsed(*header, false);
    REQUIRE(selector.select_item_type(bandage_a->typeId()));

    REQUIRE(selector.select(bandage_b.get(), false, true));
    REQUIRE(selector.get_selected().is_collation_entry());
    CHECK(selector.get_collation_next() == bandage_a.get());

    REQUIRE(selector.select(bandage_a.get()));
    REQUIRE(selector.get_selected().is_collation_header());
    CHECK(selector.get_collation_next() == bandage_a.get());
}

// CDF P3-UI1: selector core (collapse fix, hide-all, reset filter, skip-unselectable,
// parent/child highlight, DDA column actions). Pure state, no ui_adaptor.
namespace {
auto row_for(const inventory_selector& sel, const item* it) -> const inventory_entry* {
    for (const inventory_column* col :
         {&sel.own_gear_column, &sel.own_inv_column, &sel.map_column}) {
        for (const inventory_entry* e : item_rows(*col)) {
            if (e->any_item() == it) {
                return e;
            }
        }
    }
    return nullptr;
}

// A worn hiking pack holding a small box (with a rock) and a blanket.
struct carried_pack_fixture {
    static auto fresh_avatar() -> avatar& {
        clear_all_state();
        return get_avatar();
    }
    avatar& u = fresh_avatar();
    item* pack = nullptr;
    item* box = nullptr;
    item* blanket = nullptr;
    item* rock = nullptr;

    carried_pack_fixture() {
        clear_character(u, true);
        u.wear_item(item::spawn(itype_id("backpack_hiking"), calendar::turn), false);
        pack = u.worn.front();
        // the blanket (5 L) cannot nest in the small box, so both stay top level in the pack
        blanket = &u.i_add(item::spawn(itype_id("blanket"), calendar::turn));
        box = &u.i_add(item::spawn(itype_id("box_small"), calendar::turn));
        REQUIRE(box->try_put_in(item::spawn(itype_id("test_rock"), calendar::turn)).success());
        rock = box->contents.all_items_top().front();
        REQUIRE(box->parent_item() == pack);
        REQUIRE(blanket->parent_item() == pack);
        REQUIRE(rock->parent_item() == box);
    }
};

void refresh_columns(inventory_selector& sel) {
    sel.own_gear_column.prepare_paging();
    sel.own_inv_column.prepare_paging();
    sel.map_column.prepare_paging();
}

struct hide_override_guard {
    hide_override_guard() { uistate.hide_entries_override.reset(); }
    ~hide_override_guard() { uistate.hide_entries_override.reset(); }
};
} // namespace

TEST_CASE("collapse keeps the toggled row listed", "[inventory][ui][p3]") {
    const view_mode_state_guard view_mode;
    carried_pack_fixture f;
    inventory_selector sel(f.u);
    sel.add_character_items(f.u);
    refresh_columns(sel);
    REQUIRE(row_for(sel, f.box) != nullptr);
    REQUIRE(row_for(sel, f.blanket) != nullptr);

    SECTION("carried group view") {
        REQUIRE(sel.select(f.box));
        sel.toggle_selected_contents();
        refresh_columns(sel);
        // the pressed row toggled its own pockets, not the holder's
        CHECK(f.box->is_collapsed());
        CHECK_FALSE(f.pack->is_collapsed());
        CHECK(row_for(sel, f.box) != nullptr);
        CHECK(row_for(sel, f.blanket) != nullptr);

        REQUIRE(sel.select(f.box));
        sel.toggle_selected_contents();
        refresh_columns(sel);
        CHECK_FALSE(f.box->is_collapsed());
        CHECK(row_for(sel, f.box) != nullptr);
        CHECK(row_for(sel, f.blanket) != nullptr);
    }

    SECTION("hierarchy view hides the contents of the pressed row only") {
        sel.toggle_categorize_contained();
        refresh_columns(sel);
        REQUIRE(row_for(sel, f.box) != nullptr);
        REQUIRE(row_for(sel, f.rock) != nullptr);

        REQUIRE(sel.select(f.box));
        sel.toggle_selected_contents();
        refresh_columns(sel);
        CHECK(row_for(sel, f.box) != nullptr);
        CHECK(row_for(sel, f.blanket) != nullptr);
        CHECK(row_for(sel, f.rock) == nullptr);

        REQUIRE(sel.select(f.box));
        sel.toggle_selected_contents();
        refresh_columns(sel);
        CHECK(row_for(sel, f.rock) != nullptr);
    }
}

TEST_CASE("holder row collapse hides its group", "[inventory][ui][p3]") {
    carried_pack_fixture f;
    inventory_selector sel(f.u);
    sel.add_character_items(f.u);
    refresh_columns(sel);
    REQUIRE(row_for(sel, f.pack) != nullptr);
    REQUIRE(row_for(sel, f.box) != nullptr);
    REQUIRE(row_for(sel, f.blanket) != nullptr);

    REQUIRE(sel.select(f.pack));
    sel.toggle_selected_contents();
    refresh_columns(sel);
    CHECK(f.pack->is_collapsed());
    CHECK(row_for(sel, f.pack) != nullptr);
    CHECK(row_for(sel, f.box) == nullptr);
    CHECK(row_for(sel, f.blanket) == nullptr);

    REQUIRE(sel.select(f.pack));
    sel.toggle_selected_contents();
    refresh_columns(sel);
    CHECK_FALSE(f.pack->is_collapsed());
    CHECK(row_for(sel, f.box) != nullptr);
    CHECK(row_for(sel, f.blanket) != nullptr);
}

TEST_CASE("hide override cycles", "[inventory][ui][p3]") {
    const view_mode_state_guard view_mode;
    const hide_override_guard guard;
    carried_pack_fixture f;
    inventory_selector sel(f.u);
    sel.enable_view_mode_toggle();
    sel.add_character_items(f.u);
    sel.toggle_categorize_contained();
    refresh_columns(sel);
    REQUIRE_FALSE(sel.own_gear_column.hide_entries_override.has_value());
    REQUIRE(row_for(sel, f.rock) != nullptr);

    sel.cycle_hide_override_all();
    REQUIRE(sel.own_gear_column.hide_entries_override.has_value());
    CHECK_FALSE(*sel.own_gear_column.hide_entries_override);
    CHECK(uistate.hide_entries_override == sel.own_gear_column.hide_entries_override);
    // false shows every container's contents, even a collapsed one
    for (item_pocket* pocket : f.box->get_standard_pockets()) {
        pocket->settings.set_collapse(true);
    }
    refresh_columns(sel);
    CHECK(row_for(sel, f.rock) != nullptr);

    sel.cycle_hide_override_all();
    REQUIRE(sel.own_gear_column.hide_entries_override.has_value());
    CHECK(*sel.own_gear_column.hide_entries_override);
    refresh_columns(sel);
    CHECK(row_for(sel, f.pack) != nullptr);
    CHECK(row_for(sel, f.box) == nullptr);
    CHECK(row_for(sel, f.blanket) == nullptr);
    CHECK(row_for(sel, f.rock) == nullptr);

    sel.cycle_hide_override_all();
    CHECK_FALSE(sel.own_gear_column.hide_entries_override.has_value());
    CHECK_FALSE(uistate.hide_entries_override.has_value());
    // back to the pocket settings: the box is still collapsed, the blanket shows again
    refresh_columns(sel);
    CHECK(row_for(sel, f.box) != nullptr);
    CHECK(row_for(sel, f.blanket) != nullptr);
    CHECK(row_for(sel, f.rock) == nullptr);

    // a new column starts from the saved state
    uistate.hide_entries_override = true;
    const inventory_column fresh;
    CHECK(fresh.hide_entries_override.has_value());
}

TEST_CASE("reset filter clears", "[inventory][ui][p3]") {
    clear_avatar();
    auto bandages = item::spawn("bandages");
    auto heroin = item::spawn("heroin");

    inventory_selector sel(get_avatar());
    sel.add_item(sel.own_inv_column, bandages.get());
    sel.add_item(sel.own_inv_column, heroin.get());
    sel.own_inv_column.prepare_paging();
    REQUIRE(item_rows(sel.own_inv_column).size() == 2);

    sel.apply_filter("zzz_no_such_item");
    REQUIRE(sel.get_filter() == "zzz_no_such_item");
    REQUIRE(item_rows(sel.own_inv_column).empty());

    sel.reset_filter();
    CHECK(sel.get_filter().empty());
    CHECK(item_rows(sel.own_inv_column).size() == 2);
}

TEST_CASE("skip unselectable", "[inventory][ui][p3]") {
    clear_avatar();
    std::vector<detached_ptr<item>> items;
    for (int i = 0; i < 3; ++i) {
        items.push_back(item::spawn("bandages"));
    }
    inventory_selector_preset preset;
    inventory_column col(preset);
    // Added enabled, disabled, enabled; FM3's sort_compare puts the disabled row last, so the rows
    // come out [items[0], items[2], items[1(disabled)]].
    col.add_entry(inventory_entry({items[0].get()}, nullptr, true));
    col.add_entry(inventory_entry({items[1].get()}, nullptr, false));
    col.add_entry(inventory_entry({items[2].get()}, nullptr, true));
    col.invalidate_paging();
    col.prepare_paging();

    std::vector<size_t> row_index;
    for (size_t i = 0; i < col.entries.size(); ++i) {
        if (col.entries[i].is_item()) {
            row_index.push_back(i);
        }
    }
    REQUIRE(row_index.size() == 3);
    REQUIRE(col.entries[row_index[0]].any_item() == items[0].get());
    REQUIRE(col.entries[row_index[1]].any_item() == items[2].get());
    REQUIRE(col.entries[row_index[2]].any_item() == items[1].get());
    REQUIRE_FALSE(col.entries[row_index[2]].is_selectable());

    col.toggle_skip_unselectable(true);
    // The disabled row at the end is skipped over, and a forward step off it wraps to row 0.
    CHECK(col.next_highlightable_index(row_index[0], scroll_direction::FORWARD) == row_index[1]);
    CHECK(col.next_highlightable_index(row_index[1], scroll_direction::FORWARD) == row_index[0]);
    CHECK(col.next_highlightable_index(row_index[1], scroll_direction::BACKWARD) == row_index[0]);

    col.toggle_skip_unselectable(false);
    // Skipping off: the cursor may land on the disabled row, and the last row wraps to the first.
    CHECK(col.next_highlightable_index(row_index[0], scroll_direction::FORWARD) == row_index[1]);
    CHECK(col.next_highlightable_index(row_index[1], scroll_direction::FORWARD) == row_index[2]);
    CHECK(col.next_highlightable_index(row_index[2], scroll_direction::FORWARD) == row_index[0]);

    SECTION("the selector flag reaches its columns") {
        const bool before = inventory_selector::get_skip_unselectable();
        inventory_selector sel(get_avatar());
        sel.toggle_skip_unselectable();
        CHECK(inventory_selector::get_skip_unselectable() == !before);
        CHECK(sel.own_inv_column.get_skip_unselectable() == !before);
        CHECK(sel.own_gear_column.get_skip_unselectable() == !before);
        sel.toggle_skip_unselectable();
        CHECK(inventory_selector::get_skip_unselectable() == before);
        CHECK(sel.map_column.get_skip_unselectable() == before);
    }
}

TEST_CASE("parent and child flags", "[inventory][ui][p3]") {
    const view_mode_state_guard view_mode;
    carried_pack_fixture f;
    inventory_selector sel(f.u);
    sel.enable_view_mode_toggle();
    sel.add_character_items(f.u);
    sel.toggle_categorize_contained();
    refresh_columns(sel);
    REQUIRE(row_for(sel, f.pack) != nullptr);
    REQUIRE(row_for(sel, f.box) != nullptr);
    REQUIRE(row_for(sel, f.blanket) != nullptr);
    REQUIRE(row_for(sel, f.rock) != nullptr);

    REQUIRE(sel.select(f.box));
    sel.highlight();
    CHECK(row_for(sel, f.pack)->highlight_as_parent);
    CHECK(row_for(sel, f.rock)->highlight_as_child);
    CHECK_FALSE(row_for(sel, f.box)->highlight_as_parent);
    CHECK_FALSE(row_for(sel, f.box)->highlight_as_child);
    CHECK_FALSE(row_for(sel, f.blanket)->highlight_as_child);
    CHECK_FALSE(row_for(sel, f.blanket)->highlight_as_parent);
    CHECK_FALSE(row_for(sel, f.pack)->highlight_as_child);

    // moving the highlight clears the old marks
    REQUIRE(sel.select(f.rock));
    sel.highlight();
    CHECK(row_for(sel, f.box)->highlight_as_parent);
    CHECK_FALSE(row_for(sel, f.pack)->highlight_as_parent);
    CHECK_FALSE(row_for(sel, f.rock)->highlight_as_child);
    CHECK_FALSE(row_for(sel, f.box)->highlight_as_child);
}

TEST_CASE("column actions move the active column", "[inventory][ui][p3]") {
    const view_mode_state_guard view_mode;
    clear_avatar();
    auto bandages = item::spawn("bandages");
    auto heroin = item::spawn("heroin");
    auto rock = item::spawn("rock");

    SECTION("three columns") {
        inventory_selector sel(get_avatar());
        sel.add_item(sel.own_inv_column, bandages.get());
        sel.add_item(sel.map_column, heroin.get());
        sel.add_item(sel.own_gear_column, rock.get());
        REQUIRE(sel.get_selection_position().first == 0);

        CHECK(sel.handle_column_action("NEXT_COLUMN"));
        CHECK(sel.get_selection_position().first == 1);
        CHECK(sel.handle_column_action("PREV_COLUMN"));
        CHECK(sel.get_selection_position().first == 0);
        CHECK(sel.handle_column_action("PREV_COLUMN"));
        CHECK(sel.get_selection_position().first == 2);
        CHECK(sel.handle_column_action("NEXT_COLUMN"));
        CHECK(sel.get_selection_position().first == 0);
        CHECK_FALSE(sel.handle_column_action("CONFIRM"));
        CHECK(sel.get_selection_position().first == 0);
    }

    SECTION("a single column does not move") {
        inventory_selector sel(get_avatar());
        sel.add_item(sel.own_inv_column, bandages.get());
        CHECK(sel.handle_column_action("NEXT_COLUMN"));
        CHECK(sel.get_selection_position().first == 0);
        CHECK(sel.handle_column_action("PREV_COLUMN"));
        CHECK(sel.get_selection_position().first == 0);
    }
}

// CDF P3-UI2: multiselect parity (DDA TOGGLE_ENTRY / MARK_WITH_COUNT / TOGGLE_NON_FAVORITE,
// pocket-aware deselect). Pure state, no ui_adaptor and no prompts.
namespace {
struct ui2_drop_selector : inventory_drop_selector {
    using inventory_drop_selector::inventory_drop_selector;
    using inventory_drop_selector::update_drop_preview;
    using inventory_drop_selector::get_drop_preview;
    using inventory_drop_selector::get_raw_stats;
    using inventory_drop_selector::view_mode_footer_text;
    using inventory_drop_selector::get_selection_column_items;
};

auto entry_for(const inventory_selector& sel, const item* it) -> inventory_entry* {
    return const_cast<inventory_entry*>(row_for(sel, it));
}

auto chosen_of(const inventory_selector& sel, const item* it) -> size_t {
    const inventory_entry* e = row_for(sel, it);
    return e == nullptr ? 0 : e->chosen_count;
}

auto chosen_rows(const inventory_selector& sel) -> size_t {
    size_t n = 0;
    for (const inventory_column* col :
         {&sel.own_gear_column, &sel.own_inv_column, &sel.map_column}) {
        for (const inventory_entry* e : item_rows(*col)) {
            if (e->chosen_count > 0) {
                ++n;
            }
        }
    }
    return n;
}

// Container tree view: pack, box, rock and blanket each get their own row.
void load_hierarchy(inventory_selector& sel, avatar& u) {
    sel.enable_view_mode_toggle();
    sel.add_character_items(u);
    sel.toggle_categorize_contained();
    refresh_columns(sel);
}

// TOGGLE_ENTRY on one item.
void press_toggle_entry(inventory_multiselector& sel, const item* it) {
    REQUIRE(sel.select(it));
    int count = 0;
    sel.toggle_entries(count);
}

void check_container_marking(inventory_multiselector& sel, carried_pack_fixture& f) {
    load_hierarchy(sel, f.u);
    REQUIRE(entry_for(sel, f.box) != nullptr);
    REQUIRE(entry_for(sel, f.rock) != nullptr);

    press_toggle_entry(sel, f.rock);
    CHECK(chosen_of(sel, f.rock) > 0);
    CHECK(chosen_rows(sel) == 1);

    press_toggle_entry(sel, f.box);
    CHECK(chosen_of(sel, f.box) > 0);
    CHECK(chosen_of(sel, f.rock) == 0);
    CHECK(chosen_rows(sel) == 1);
}
} // namespace

TEST_CASE("marking a container deselects its contained items", "[inventory][ui][p3]") {
    carried_pack_fixture f;
    SECTION("drop") {
        ui2_drop_selector sel(f.u);
        check_container_marking(sel, f);
    }
    SECTION("iuse") {
        inventory_iuse_selector sel(f.u, "test");
        check_container_marking(sel, f);
    }
    SECTION("pickup") {
        inventory_pickup_selector sel(f.u);
        check_container_marking(sel, f);
    }
}

TEST_CASE("marking a contained item after its container keeps both consistent",
          "[inventory][ui][p3]") {
    carried_pack_fixture f;
    ui2_drop_selector sel(f.u);
    load_hierarchy(sel, f.u);
    REQUIRE(entry_for(sel, f.pack) != nullptr);
    REQUIRE(entry_for(sel, f.box) != nullptr);
    REQUIRE(entry_for(sel, f.rock) != nullptr);
    REQUIRE(entry_for(sel, f.blanket) != nullptr);

    SECTION("a direct child") {
        press_toggle_entry(sel, f.box);
        press_toggle_entry(sel, f.rock);
        CHECK(chosen_of(sel, f.box) > 0);
        CHECK(chosen_of(sel, f.rock) == 0);
        CHECK(chosen_rows(sel) == 1);
    }

    SECTION("a grandchild and a child of the outer container") {
        press_toggle_entry(sel, f.pack);
        press_toggle_entry(sel, f.rock);
        press_toggle_entry(sel, f.blanket);
        CHECK(chosen_of(sel, f.pack) > 0);
        CHECK(chosen_of(sel, f.rock) == 0);
        CHECK(chosen_of(sel, f.blanket) == 0);
        CHECK(chosen_rows(sel) == 1);
    }

    SECTION("a sibling stays marked") {
        press_toggle_entry(sel, f.box);
        press_toggle_entry(sel, f.blanket);
        CHECK(chosen_of(sel, f.box) > 0);
        CHECK(chosen_of(sel, f.blanket) > 0);
        CHECK(chosen_of(sel, f.rock) == 0);
        CHECK(chosen_rows(sel) == 2);
    }
}

TEST_CASE("unmarking a container unmarks its items", "[inventory][ui][p3]") {
    carried_pack_fixture f;
    ui2_drop_selector sel(f.u);
    load_hierarchy(sel, f.u);
    REQUIRE(entry_for(sel, f.box) != nullptr);
    REQUIRE(entry_for(sel, f.rock) != nullptr);

    press_toggle_entry(sel, f.rock);
    press_toggle_entry(sel, f.box);
    REQUIRE(chosen_rows(sel) == 1);

    press_toggle_entry(sel, f.box);
    CHECK(chosen_of(sel, f.box) == 0);
    CHECK(chosen_of(sel, f.rock) == 0);
    CHECK(chosen_rows(sel) == 0);
}

TEST_CASE("non-favorite toggle marks only non-favorite non-worn entries", "[inventory][ui][p3]") {
    carried_pack_fixture f;
    auto bandages = item::spawn("bandages");
    auto rock = item::spawn("rock");
    auto heroin = item::spawn("heroin");
    heroin->is_favorite = true;
    REQUIRE(f.u.is_worn(*f.pack));

    inventory_iuse_selector sel(f.u, "test");
    sel.add_item(sel.own_inv_column, bandages.get());
    sel.add_item(sel.own_inv_column, rock.get());
    sel.add_item(sel.own_inv_column, heroin.get());
    sel.add_item(sel.own_inv_column, f.pack);
    sel.own_inv_column.prepare_paging();
    REQUIRE(sel.select(bandages.get()));

    int count = 0;
    sel.toggle_entries(count, inventory_multiselector::toggle_mode::NON_FAVORITE_NON_WORN);
    CHECK(chosen_of(sel, bandages.get()) > 0);
    CHECK(chosen_of(sel, rock.get()) > 0);
    CHECK(chosen_of(sel, heroin.get()) == 0);
    CHECK(chosen_of(sel, f.pack) == 0);

    // a second press clears them again
    count = 0;
    sel.toggle_entries(count, inventory_multiselector::toggle_mode::NON_FAVORITE_NON_WORN);
    CHECK(chosen_rows(sel) == 0);
}

TEST_CASE("drop preview still counts after a mark", "[inventory][ui][p3]") {
    carried_pack_fixture f;
    ui2_drop_selector sel(f.u);
    load_hierarchy(sel, f.u);
    REQUIRE(entry_for(sel, f.blanket) != nullptr);
    CHECK_FALSE(entry_for(sel, f.blanket)->drop_order.has_value());

    // no update_drop_preview() call here: the mark itself refreshes the preview
    press_toggle_entry(sel, f.blanket);
    CHECK_FALSE(sel.get_drop_preview().empty());
    REQUIRE(entry_for(sel, f.blanket) != nullptr);
    CHECK(entry_for(sel, f.blanket)->drop_order.has_value());
    CHECK(entry_for(sel, f.blanket)->automatic_drop_count == 0);

    press_toggle_entry(sel, f.blanket);
    CHECK(sel.get_drop_preview().empty());
    CHECK_FALSE(entry_for(sel, f.blanket)->drop_order.has_value());
}

TEST_CASE("count keys change the chosen count within the stack", "[inventory][ui][p3]") {
    clear_avatar();
    auto rock_a = item::spawn("rock");
    auto rock_b = item::spawn("rock");
    auto rock_c = item::spawn("rock");
    const std::vector<item*> stack{rock_a.get(), rock_b.get(), rock_c.get()};

    inventory_iuse_selector sel(get_avatar(), "test");
    sel.own_inv_column.add_entry(inventory_entry(stack));
    sel.own_inv_column.prepare_paging();
    REQUIRE(sel.select(rock_a.get()));
    REQUIRE(entry_for(sel, rock_a.get()) != nullptr);
    REQUIRE(entry_for(sel, rock_a.get())->get_available_count() == 3);

    for (size_t expected : {1, 2, 3, 3, 3}) {
        CHECK(sel.handle_mark_action("INCREASE_COUNT"));
        CHECK(chosen_of(sel, rock_a.get()) == expected);
    }
    for (size_t expected : {2, 1, 0, 0}) {
        CHECK(sel.handle_mark_action("DECREASE_COUNT"));
        CHECK(chosen_of(sel, rock_a.get()) == expected);
    }
    CHECK_FALSE(sel.handle_mark_action("CONFIRM"));
}

// CDF P3-UI6: mouse in the pick selectors. No real mouse: the rectangle map is filled by hand and
// the mouse actions go through handle_mouse_action (what execute() runs for a mouse event).
namespace {
struct mouse_selector_fixture {
    std::vector<decltype(item::spawn("rock"))> owned;
    std::vector<item*> items;
    inventory_pick_selector sel;

    static auto fresh_avatar() -> avatar& {
        clear_avatar();
        return get_avatar();
    }

    explicit mouse_selector_fixture(size_t count)
        : sel(fresh_avatar()) {
        for (size_t i = 0; i < count; ++i) {
            owned.push_back(item::spawn("rock"));
            items.push_back(owned.back().get());
            sel.own_inv_column.add_entry(inventory_entry(std::vector<item*>{items.back()}));
        }
        sel.own_inv_column.prepare_paging();
    }

    auto entry_of(const item* it) -> inventory_entry* {
        for (inventory_entry& e : sel.own_inv_column.entries) {
            if (e.is_item() && e.any_item() == it) {
                return &e;
            }
        }
        return nullptr;
    }
};

struct drag_hook_guard {
    std::vector<std::pair<item*, item*>> calls;
    bool result = false;
    drag_hook_guard() {
        inventory_selector::drag_drop_test_hook = [this](item& source, item& dest) {
            calls.emplace_back(&source, &dest);
            return result;
        };
    }
    ~drag_hook_guard() { inventory_selector::drag_drop_test_hook = nullptr; }
};
} // namespace

TEST_CASE("find_entry_by_coordinate returns the row under the point", "[inventory][ui][p3]") {
    mouse_selector_fixture f(3);
    inventory_entry* first = f.entry_of(f.items[0]);
    inventory_entry* second = f.entry_of(f.items[1]);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    inventory_entry* header = nullptr;
    for (inventory_entry& e : f.sel.own_inv_column.entries) {
        if (!e.is_item() && e) {
            header = &e;
        }
    }

    f.sel.rect_entry_map.clear();
    f.sel.rect_entry_map.push_back({inclusive_rectangle<point>(point(2, 5), point(30, 5)), first});
    f.sel.rect_entry_map.push_back({inclusive_rectangle<point>(point(2, 6), point(30, 6)), second});
    if (header != nullptr) {
        f.sel.rect_entry_map.push_back(
            {inclusive_rectangle<point>(point(2, 7), point(30, 7)), header});
    }

    CHECK(f.sel.find_entry_by_coordinate(point(2, 5)) == first);
    CHECK(f.sel.find_entry_by_coordinate(point(30, 5)) == first);
    CHECK(f.sel.find_entry_by_coordinate(point(10, 6)) == second);
    // empty space: above, below, left and right of the rows
    CHECK(f.sel.find_entry_by_coordinate(point(10, 4)) == nullptr);
    CHECK(f.sel.find_entry_by_coordinate(point(10, 8)) == nullptr);
    CHECK(f.sel.find_entry_by_coordinate(point(1, 5)) == nullptr);
    CHECK(f.sel.find_entry_by_coordinate(point(31, 6)) == nullptr);
    if (header != nullptr) {
        // a header row is found, but is not selectable, so process_input ignores it
        inventory_entry* found = f.sel.find_entry_by_coordinate(point(10, 7));
        CHECK(found == header);
        CHECK_FALSE(found->is_selectable());
    }
}

TEST_CASE("mouse move highlights without selecting", "[inventory][ui][p3]") {
    mouse_selector_fixture f(3);
    REQUIRE(f.sel.select(f.items[0]));
    inventory_entry* third = f.entry_of(f.items[2]);
    REQUIRE(third != nullptr);

    const auto outcome = f.sel.handle_mouse_action("MOUSE_MOVE", *third);
    CHECK(outcome.picked == nullptr);
    CHECK_FALSE(outcome.toggle_contents);
    CHECK(f.sel.get_selected().any_item() == f.items[2]);

    SECTION("a click picks the row") {
        const auto clicked = f.sel.handle_mouse_action("SELECT", *third);
        CHECK(clicked.picked == f.items[2]);
    }
    SECTION("hover is ignored while a drag is active") {
        drag_hook_guard hook;
        f.sel.drag_enabled = true;
        f.sel.handle_mouse_action("CLICK_AND_DRAG", *third);
        REQUIRE(f.sel.is_drag_active());
        inventory_entry* first = f.entry_of(f.items[0]);
        f.sel.handle_mouse_action("MOUSE_MOVE", *first);
        CHECK(f.sel.get_selected().any_item() == f.items[2]);
    }
}

TEST_CASE("scroll actions move the highlight within bounds", "[inventory][ui][p3]") {
    mouse_selector_fixture f(8);
    inventory_column& col = f.sel.own_inv_column;
    col.set_height(3);
    col.prepare_paging();
    REQUIRE(f.sel.select(f.items[0]));
    const size_t start = col.get_selected_index();

    col.scroll_selection_page(scroll_direction::FORWARD);
    CHECK(col.get_selected_index() != start);
    for (int i = 0; i < 20; ++i) {
        col.scroll_selection_page(scroll_direction::FORWARD);
        REQUIRE(col.get_selected_index() < col.entries.size());
        CHECK(col.get_selected().is_item());
    }
    for (int i = 0; i < 20; ++i) {
        col.scroll_selection_page(scroll_direction::BACKWARD);
        REQUIRE(col.get_selected_index() < col.entries.size());
        CHECK(col.get_selected().is_item());
    }
}

// Exact page-scroll semantics (DDA inventory_column::scroll_selection_page, mouse wheel): jump to
// the first highlightable entry of the next (FORWARD) or previous (BACKWARD) page, wrapping around
// the whole column. Ten rocks at height 4 give three entries per page: every page is a category
// header followed by two rocks, so the rocks sit at entry indices 1,2 4,5 7,8 10,11 13,14.
TEST_CASE("page scroll lands on exact entries across several pages", "[inventory][ui][p3]") {
    mouse_selector_fixture f(10);
    inventory_column& col = f.sel.own_inv_column;
    col.set_height(4);
    col.prepare_paging();
    REQUIRE(col.entries.size() == 15);
    for (const size_t cat : {0u, 3u, 6u, 9u, 12u}) {
        REQUIRE(col.entries[cat].is_category());
    }
    // rock number k lives at entry index item_index[k]
    const std::array<size_t, 10> item_index = {1, 2, 4, 5, 7, 8, 10, 11, 13, 14};
    for (size_t k = 0; k < item_index.size(); ++k) {
        REQUIRE(col.entries[item_index[k]].any_item() == f.items[k]);
    }

    REQUIRE(f.sel.select(f.items[0]));
    REQUIRE(col.get_selected_index() == 1);

    SECTION("forward walks page by page, then wraps to the first page") {
        for (const size_t expected : {4u, 7u, 10u, 13u, 1u, 4u}) {
            col.scroll_selection_page(scroll_direction::FORWARD);
            CHECK(col.get_selected_index() == expected);
        }
    }
    SECTION("forward from the second row of a page still goes to the next page") {
        REQUIRE(f.sel.select(f.items[1]));
        REQUIRE(col.get_selected_index() == 2);
        col.scroll_selection_page(scroll_direction::FORWARD);
        CHECK(col.get_selected_index() == 4);
    }
    SECTION("forward past the last page wraps to the first row") {
        REQUIRE(f.sel.select(f.items[9]));
        REQUIRE(col.get_selected_index() == 14);
        col.scroll_selection_page(scroll_direction::FORWARD);
        CHECK(col.get_selected_index() == 1);
        REQUIRE(f.sel.select(f.items[8]));
        col.scroll_selection_page(scroll_direction::FORWARD);
        CHECK(col.get_selected_index() == 1);
    }
    SECTION("backward from the first page wraps to the last row, then steps back page by page") {
        for (const size_t expected : {14u, 11u, 8u, 5u, 2u, 14u}) {
            col.scroll_selection_page(scroll_direction::BACKWARD);
            CHECK(col.get_selected_index() == expected);
        }
    }
}

TEST_CASE("drag between two items calls the select-pocket flow with source and destination",
          "[inventory][ui][p3]") {
    mouse_selector_fixture f(3);
    drag_hook_guard hook;
    inventory_entry* src = f.entry_of(f.items[0]);
    inventory_entry* dst = f.entry_of(f.items[1]);
    REQUIRE(src != nullptr);
    REQUIRE(dst != nullptr);

    SECTION("drag enabled") {
        f.sel.drag_enabled = true;
        f.sel.handle_mouse_action("CLICK_AND_DRAG", *src);
        CHECK(f.sel.is_drag_active());
        const auto outcome = f.sel.handle_mouse_action("SELECT", *dst);
        CHECK(outcome.picked == nullptr);
        CHECK_FALSE(f.sel.is_drag_active());
        REQUIRE(hook.calls.size() == 1);
        CHECK(hook.calls[0].first == f.items[0]);
        CHECK(hook.calls[0].second == f.items[1]);
    }
    SECTION("release on the starting row is a plain click") {
        f.sel.drag_enabled = true;
        f.sel.handle_mouse_action("CLICK_AND_DRAG", *src);
        const auto outcome = f.sel.handle_mouse_action("SELECT", *src);
        CHECK(outcome.picked == f.items[0]);
        CHECK(hook.calls.empty());
    }
    SECTION("drag disabled") {
        f.sel.handle_mouse_action("CLICK_AND_DRAG", *src);
        CHECK_FALSE(f.sel.is_drag_active());
        const auto outcome = f.sel.handle_mouse_action("SELECT", *dst);
        CHECK(outcome.picked == f.items[1]);
        CHECK(hook.calls.empty());
    }
}

// CDF P3-UI8: the pickup selector takes DDA's menu behaviour but keeps CDF's pickup pipeline.
// Pure state, no ui_adaptor and no prompts; the items sit in the map column like ground items.
namespace {
struct ui8_pickup_selector : inventory_pickup_selector {
    using inventory_pickup_selector::inventory_pickup_selector;
    using inventory_pickup_selector::build_selection_stats_line;
    using inventory_pickup_selector::get_raw_stats;
    using inventory_pickup_selector::ctxt;
    using inventory_pickup_selector::add_entry;
};

struct pickup_menu_fixture {
    std::vector<decltype(item::spawn("rock"))> owned;
    std::vector<item*> items;
    ui8_pickup_selector sel;

    static auto fresh_avatar() -> avatar& {
        clear_avatar();
        return get_avatar();
    }

    pickup_menu_fixture() : sel(fresh_avatar()) {}

    // One row per rock.
    void add_rows(size_t count) {
        for (size_t i = 0; i < count; ++i) {
            owned.push_back(item::spawn("rock"));
            items.push_back(owned.back().get());
            // through the selector, like add_map_items does, so it knows it holds items
            sel.add_item(sel.map_column, items.back());
        }
        sel.map_column.prepare_paging();
    }

    // One row standing for all the rocks (a stack).
    void add_stack(size_t count) {
        std::vector<item*> stack;
        for (size_t i = 0; i < count; ++i) {
            owned.push_back(item::spawn("rock"));
            items.push_back(owned.back().get());
            stack.push_back(items.back());
        }
        sel.add_entry(sel.map_column, std::move(stack));
        sel.map_column.prepare_paging();
    }

    auto row_of(const item* it) -> inventory_entry* {
        for (inventory_entry& e : sel.map_column.entries) {
            if (e.is_item() && e.any_item() == it) {
                return &e;
            }
        }
        return nullptr;
    }

    void mark(const item* it, int count = 0) {
        REQUIRE(sel.select(it));
        sel.toggle_entries(count);
    }
};
} // namespace

TEST_CASE("pickup hint names the DDA action keys", "[inventory][ui][p3]") {
    pickup_menu_fixture f;
    const std::string& hint = f.sel.get_hint();
    for (const char* action : {"WIELD", "WEAR", "SHOW_HIDE_CONTENTS", "SHOW_HIDE_CONTENTS_ALL",
                               "EXAMINE", "MARK_WITH_COUNT", "INCREASE_COUNT", "DECREASE_COUNT",
                               "TOGGLE_ENTRY"}) {
        CAPTURE(action);
        const std::string cell = "<color_yellow>" + f.sel.ctxt.get_desc(action) + "</color>";
        // FM4: DDA prints the fixed five-action line, including an unbound action ("Unbound").
        CHECK(hint.find(cell) != std::string::npos);
    }
}

TEST_CASE("empty selection is refused with the toggle-entry key named", "[inventory][ui][p3]") {
    pickup_menu_fixture f;
    f.add_rows(2);
    // nothing marked: the confirm path has nothing to return
    CHECK(f.sel.get_marked_pickups().empty());
    CHECK(f.sel.build_selection().empty());
    const std::string text = f.sel.no_selection_text();
    CHECK_FALSE(text.empty());
    CHECK(text.find(f.sel.ctxt.get_desc("TOGGLE_ENTRY")) != std::string::npos);

    f.mark(f.items[0]);
    CHECK(f.sel.get_marked_pickups().size() == 1);
    CHECK_FALSE(f.sel.build_selection().empty());
}

TEST_CASE("pickup stats header", "[inventory][ui][p3]") {
    pickup_menu_fixture f;
    f.add_stack(3);
    const item& rock = *f.items[0];
    const auto before = f.sel.get_raw_stats();
    REQUIRE_FALSE(before.empty());
    CHECK(before.back() ==
          ui8_pickup_selector::build_selection_stats_line(0_ml, 0_gram));

    // two of the three rocks marked: the selected line counts exactly those two
    f.mark(&rock, 2);
    REQUIRE(f.row_of(&rock) != nullptr);
    REQUIRE(f.row_of(&rock)->chosen_count == 2);
    const auto after = f.sel.get_raw_stats();
    REQUIRE(after.size() == before.size());
    CHECK(after.back() ==
          ui8_pickup_selector::build_selection_stats_line(rock.volume() * 2, rock.weight() * 2));
    CHECK(after.back() != before.back());
}

TEST_CASE("wield from the pickup menu removes the item from the pickup list and keeps the menu",
          "[inventory][ui][p3]") {
    pickup_menu_fixture f;
    f.add_rows(3);
    f.mark(f.items[0]);
    REQUIRE(f.sel.get_marked_pickups().size() == 1);
    REQUIRE(f.sel.select(f.items[0]));
    REQUIRE(f.sel.item_after_in_active_column(*f.items[0]) == f.items[1]);

    // the data path of wield/wear: the item and its mark leave the list, the highlight moves on
    f.sel.remove_used_item(*f.items[0]);
    CHECK(f.row_of(f.items[0]) == nullptr);
    CHECK(f.sel.get_marked_pickups().empty());
    REQUIRE(f.row_of(f.items[1]) != nullptr);
    REQUIRE(f.row_of(f.items[2]) != nullptr);
    CHECK(f.sel.get_selected().any_item() == f.items[1]);
    CHECK_FALSE(f.sel.empty());
}

TEST_CASE("optimize_pickup result unchanged for the same marks", "[inventory][ui][p3]") {
    pickup_menu_fixture f;
    f.add_rows(3);
    f.mark(f.items[0]);
    f.mark(f.items[2]);

    const std::vector<item*> targets{f.items[0], f.items[2]};
    const std::vector<int> quantities{1, 1};
    const auto expected = pickup::optimize_pickup(targets, quantities);
    const auto got = f.sel.build_selection();
    REQUIRE_FALSE(expected.empty());
    REQUIRE(got.size() == expected.size());
    for (size_t i = 0; i < got.size(); ++i) {
        CHECK(got[i].quantity == expected[i].quantity);
        CHECK(got[i].children.size() == expected[i].children.size());
    }
}

// CDF P3-UI fix tests (FIX3a): A-M1, A-M2, B-M1, B-M2.
namespace {
// The skip-unselectable flag is static; every test that changes it puts it back.
struct skip_flag_guard {
    bool before = inventory_selector::get_skip_unselectable();
    void set(inventory_selector& sel, bool value) {
        if (inventory_selector::get_skip_unselectable() != value) {
            sel.toggle_skip_unselectable();
        }
        REQUIRE(inventory_selector::get_skip_unselectable() == value);
    }
    ~skip_flag_guard() {
        if (inventory_selector::get_skip_unselectable() != before) {
            inventory_selector tmp(get_avatar());
            tmp.toggle_skip_unselectable();
        }
    }
};
} // namespace

TEST_CASE("column select lands on a disabled row only while skipping is off", "[inventory][ui][p3]") {
    clear_avatar();
    std::vector<detached_ptr<item>> items;
    for (int i = 0; i < 3; ++i) {
        items.push_back(item::spawn("bandages"));
    }
    inventory_selector_preset preset;
    inventory_column col(preset);
    col.add_entry(inventory_entry({items[0].get()}, nullptr, true));
    col.add_entry(inventory_entry({items[1].get()}, nullptr, false));
    col.add_entry(inventory_entry({items[2].get()}, nullptr, true));
    col.invalidate_paging();
    col.prepare_paging();

    size_t disabled_row = col.entries.size();
    size_t last_row = col.entries.size();
    for (size_t i = 0; i < col.entries.size(); ++i) {
        if (col.entries[i].is_item() && col.entries[i].any_item() == items[1].get()) {
            disabled_row = i;
        }
        if (col.entries[i].is_item() && col.entries[i].any_item() == items[2].get()) {
            last_row = i;
        }
    }
    REQUIRE(disabled_row < col.entries.size());
    REQUIRE(last_row < col.entries.size());

    col.toggle_skip_unselectable(true);
    CHECK_FALSE(col.select(items[1].get()));

    col.toggle_skip_unselectable(false);
    REQUIRE(col.select(items[2].get()));
    CHECK(col.get_selected_index() == last_row);
    // A-M2: the cursor may rest on the disabled row, so re-selecting it must find it.
    CHECK(col.select(items[1].get()));
    CHECK(col.get_selected_index() == disabled_row);
}

TEST_CASE("pick selector confirm refuses a disabled row", "[inventory][ui][p3]") {
    clear_avatar();
    inventory_pick_selector sel(get_avatar());
    auto enabled_item = item::spawn("bandages");
    auto disabled_item = item::spawn("bandages");
    sel.own_inv_column.add_entry(inventory_entry({enabled_item.get()}, nullptr, true));
    sel.own_inv_column.add_entry(inventory_entry({disabled_item.get()}, nullptr, false));
    sel.own_inv_column.prepare_paging();

    skip_flag_guard skip;
    skip.set(sel, false);
    REQUIRE(sel.select(disabled_item.get()));
    REQUIRE(sel.get_selected().any_item() == disabled_item.get());
    CHECK(sel.confirmed_item() == nullptr);

    REQUIRE(sel.select(enabled_item.get()));
    CHECK(sel.confirmed_item() == enabled_item.get());
}

TEST_CASE("a drag that ends without a row click is cancelled", "[inventory][ui][p3]") {
    mouse_selector_fixture f(3);
    drag_hook_guard hook;
    f.sel.drag_enabled = true;
    inventory_entry* src = f.entry_of(f.items[0]);
    inventory_entry* other = f.entry_of(f.items[1]);
    REQUIRE(src != nullptr);
    REQUIRE(other != nullptr);

    f.sel.handle_mouse_action("CLICK_AND_DRAG", *src);
    REQUIRE(f.sel.is_drag_active());

    // moving the mouse keeps the drag
    f.sel.end_stale_drag(inventory_input{"MOUSE_MOVE", 0, other, true});
    CHECK(f.sel.is_drag_active());
    // the release landed on no row: SELECT without an entry
    f.sel.end_stale_drag(inventory_input{"SELECT", 0, nullptr, false});
    CHECK_FALSE(f.sel.is_drag_active());

    // the next ordinary click is a plain click, not a drop of the old source
    const auto outcome = f.sel.handle_mouse_action("SELECT", *other);
    CHECK(outcome.picked == f.items[1]);
    CHECK(hook.calls.empty());

    // a release on a row keeps the drag until handle_mouse_action ends it
    f.sel.handle_mouse_action("CLICK_AND_DRAG", *src);
    f.sel.end_stale_drag(inventory_input{"SELECT", 0, other, true});
    CHECK(f.sel.is_drag_active());
    // any other input (a key) ends it
    f.sel.end_stale_drag(inventory_input{"ANY_INPUT", 'x', nullptr, false});
    CHECK_FALSE(f.sel.is_drag_active());
}

TEST_CASE("drag and drop never stores a container inside its own contents", "[inventory][ui][p3]") {
    carried_pack_fixture f;
    drag_hook_guard hook;
    hook.result = true;
    inventory_selector sel(f.u);

    // the rock sits in the box: the box cannot be dropped into its own contents
    CHECK_FALSE(sel.drag_drop_item(f.box, f.rock));
    // and neither can the pack (two levels up)
    CHECK_FALSE(sel.drag_drop_item(f.pack, f.rock));
    CHECK(hook.calls.empty());

    // the other way round is allowed: the flow is asked
    CHECK(sel.drag_drop_item(f.rock, f.box));
    REQUIRE(hook.calls.size() == 1);
    CHECK(hook.calls[0].first == f.rock);
    CHECK(hook.calls[0].second == f.box);
}

TEST_CASE("selectors that ignore the hide override show every row", "[inventory][ui][p3]") {
    struct override_guard {
        std::optional<bool> before = uistate.hide_entries_override;
        ~override_guard() { uistate.hide_entries_override = before; }
    } restore;
    uistate.hide_entries_override = true;
    clear_avatar();

    inventory_selector plain(get_avatar());
    CHECK(plain.own_inv_column.hide_entries_override == std::optional<bool>(true));

    unload_selector unload(get_avatar());
    CHECK_FALSE(unload.own_inv_column.hide_entries_override.has_value());
    CHECK_FALSE(unload.own_gear_column.hide_entries_override.has_value());
    unload.cycle_hide_override_all();
    CHECK_FALSE(unload.own_inv_column.hide_entries_override.has_value());
    // and it does not touch the saved setting
    CHECK(uistate.hide_entries_override == std::optional<bool>(true));
}

TEST_CASE("rebuilding a selector clears the row rectangles", "[inventory][ui][p3]") {
    mouse_selector_fixture f(2);
    inventory_entry* first = f.entry_of(f.items[0]);
    REQUIRE(first != nullptr);
    f.sel.rect_entry_map.push_back({inclusive_rectangle<point>(point(2, 5), point(30, 5)), first});
    REQUIRE(f.sel.find_entry_by_coordinate(point(3, 5)) == first);

    f.sel.clear_items();
    CHECK(f.sel.rect_entry_map.empty());
    CHECK(f.sel.find_entry_by_coordinate(point(3, 5)) == nullptr);

    f.sel.rect_entry_map.push_back({inclusive_rectangle<point>(point(2, 5), point(30, 5)), first});
    f.sel.reset_filter();
    CHECK(f.sel.rect_entry_map.empty());
}

// PL-63 FM2: row format (DDA inventory_ui.cpp:1799-1829, 810-860; item_category.json).
TEST_CASE(
    "symbol mode marks the parent row with a caret and the children with another",
    "[inventory][ui][fm]") {
    carried_pack_fixture f;
    inventory_selector sel(f.u);
    sel.add_character_items(f.u);
    refresh_columns(sel);
    REQUIRE(row_for(sel, f.pack) != nullptr);
    REQUIRE(row_for(sel, f.rock) != nullptr);
    REQUIRE(sel.select(f.box));

    SECTION("the default option is symbol") {
        CHECK(get_option<std::string>("INVENTORY_HIGHLIGHT") == "symbol");
        sel.highlight();
        CHECK(row_for(sel, f.pack)->highlight_as_parent);
        CHECK(row_for(sel, f.rock)->highlight_as_child);
    }

    SECTION("highlight keeps the marks") {
        override_option highlight("INVENTORY_HIGHLIGHT", "highlight");
        sel.highlight();
        CHECK(row_for(sel, f.pack)->highlight_as_parent);
        CHECK(row_for(sel, f.rock)->highlight_as_child);
    }
}

TEST_CASE("disable mode marks nothing", "[inventory][ui][fm]") {
    carried_pack_fixture f;
    inventory_selector sel(f.u);
    sel.add_character_items(f.u);
    refresh_columns(sel);
    REQUIRE(sel.select(f.box));

    override_option highlight("INVENTORY_HIGHLIGHT", "disable");
    sel.highlight();
    CHECK_FALSE(row_for(sel, f.pack)->highlight_as_parent);
    CHECK_FALSE(row_for(sel, f.rock)->highlight_as_child);
}

// PL-64 OPT1: DDA ASTERISK_POSITION (item_tname.cpp:120, 652) puts the favourite mark
// before the name (prefix, DDA's effective default) or after it (suffix).
TEST_CASE("favourite mark honours ASTERISK_POSITION", "[inventory][ui][fm]") {
    carried_pack_fixture f;
    f.blanket->set_favorite(true);
    inventory_selector sel(f.u);
    sel.add_character_items(f.u);
    refresh_columns(sel);
    const inventory_entry* row = row_for(sel, f.blanket);
    REQUIRE(row != nullptr);

    SECTION("default is prefix") {
        CHECK(get_option<std::string>("ASTERISK_POSITION") == "prefix");
        const std::string text = default_preset.get_cell_text(*row, 0);
        CHECK(text.rfind("* ", 0) == 0);
        CHECK(text.find(" *") == std::string::npos);
        // DDA item_tname.h:129 leaves FAVORITE_PRE out of unprefixed_tname.
        CHECK(f.blanket->tname(1, false).rfind("* ", 0) != 0);
    }

    SECTION("suffix puts it at the end") {
        override_option opt("ASTERISK_POSITION", "suffix");
        const std::string text = default_preset.get_cell_text(*row, 0);
        CHECK(text.rfind("* ", 0) != 0);
        REQUIRE(text.size() >= 2);
        CHECK(text.compare(text.size() - 2, 2, " *") == 0);
        // FAVORITE_POST is not a prefix segment, so it stays in the unprefixed name.
        const std::string bare = f.blanket->tname(1, false);
        REQUIRE(bare.size() >= 2);
        CHECK(bare.compare(bare.size() - 2, 2, " *") == 0);
    }
}

TEST_CASE(
    "worn container name carries volume weight and length with the default option",
    "[inventory][ui][fm]") {
    carried_pack_fixture f;
    inventory_selector sel(f.u);
    sel.add_character_items(f.u);
    refresh_columns(sel);
    const inventory_entry* pack_row = row_for(sel, f.pack);
    const inventory_entry* box_row = row_for(sel, f.box);
    REQUIRE(pack_row != nullptr);
    REQUIRE(box_row != nullptr);

    SECTION("default is WORN") {
        CHECK(get_option<std::string>("DETAILED_CONTAINERS") == "WORN");
        const std::string pack_text = default_preset.get_cell_text(*pack_row, 0);
        CHECK(pack_text.find("(remains") != std::string::npos);
        CHECK(pack_text.find("max length") != std::string::npos);
        // the box is carried, not worn, so it gets no suffix
        CHECK(default_preset.get_cell_text(*box_row, 0).find("(remains") == std::string::npos);
    }

    SECTION("NONE adds nothing") {
        override_option detailed("DETAILED_CONTAINERS", "NONE");
        CHECK(default_preset.get_cell_text(*pack_row, 0).find("(remains") == std::string::npos);
    }

    SECTION("ALL covers the carried box too") {
        override_option detailed("DETAILED_CONTAINERS", "ALL");
        CHECK(default_preset.get_cell_text(*pack_row, 0).find("(remains") != std::string::npos);
        CHECK(default_preset.get_cell_text(*box_row, 0).find("(remains") != std::string::npos);
    }
}

TEST_CASE("restricted-only pockets get no suffix", "[inventory][ui][fm]") {
    carried_pack_fixture f;
    f.u.wear_item(item::spawn(itype_id("chestpouch"), calendar::turn), false);
    inventory_selector sel(f.u);
    sel.add_character_items(f.u);
    refresh_columns(sel);
    const item* pouch = nullptr;
    for (item* worn : f.u.top_items_loc()) {
        if (worn->typeId() == itype_id("chestpouch")) {
            pouch = worn;
        }
    }
    REQUIRE(pouch != nullptr);
    const inventory_entry* pouch_row = row_for(sel, pouch);
    REQUIRE(pouch_row != nullptr);
    CHECK(default_preset.get_cell_text(*pouch_row, 0).find("(remains") == std::string::npos);

    override_option detailed("DETAILED_CONTAINERS", "ALL");
    CHECK(default_preset.get_cell_text(*pouch_row, 0).find("(remains") == std::string::npos);
}

TEST_CASE(
    "category headers and ranks match the DDA table for shared ids", "[inventory][ui][fm]") {
    // The 23 ids both games share, from DDA b4b62254ae2 data/json/item_category.json
    // (the table behind PL-63 work/category-diff.tsv).
    struct expected_category {
        const char* id;
        const char* header;
        int rank;
    };
    static const std::array<expected_category, 23> dda_categories = {{
            { "WEAPON_HELD", "Weapon held", -200 },
            { "ITEMS_WORN", "Items worn", -100 },
            { "guns", "Guns", -23 },
            { "magazines", "Magazines", -22 },
            { "ammo", "Ammo", -21 },
            { "weapons", "Weapons", -20 },
            { "tools", "Tools", -19 },
            { "food", "Food", -18 },
            { "drugs", "Drugs", -17 },
            { "books", "Books", -15 },
            { "maps", "Maps", -14 },
            { "mods", "Mods", -13 },
            { "bionics", "Bionics", -12 },
            { "mutagen", "Mutagens", -12 },
            { "veh_parts", "Vehicle parts", -10 },
            { "other", "Other", -9 },
            { "fuel", "Fuel", -8 },
            { "seeds", "Seeds", -7 },
            { "chems", "Chemical stuff", 5 },
            { "spare_parts", "Spare parts", 8 },
            { "container", "Containers", 9 },
            { "artifacts", "Artifacts", 10 },
            { "clothing", "Clothing", 19 },
        }
    };

    for (const expected_category& expected : dda_categories) {
        CAPTURE(expected.id);
        const item_category& category = item_category_id(expected.id).obj();
        CHECK(category.name() == expected.header);
        CHECK(category.sort_rank() == expected.rank);
    }
}

// FM3: DDA's column layout, row order, filter-with-collapse and selection width
// (DDA b4b62254ae2 inventory_ui.cpp:755-763, 1410-1430, 1561-1564, 1882-1895, 2510-2560).
namespace {
struct denial_text_preset : inventory_selector_preset {
    std::string denial;
    std::string get_denial(const item*) const override { return denial; }
};

// rearrange_columns is protected; this mirrors the production call in prepare_layout.
struct fm3_selector : inventory_selector {
    using inventory_selector::inventory_selector;
    using inventory_selector::prepare_layout;
    using inventory_selector::rearrange_columns;
};
} // namespace

TEST_CASE("favourites sort before other items in a category", "[inventory][ui][fm]") {
    clear_avatar();
    auto& u = get_avatar();

    auto plain = item::spawn("bandages");
    auto favourite = item::spawn("bandages");
    favourite->set_favorite(true);

    inventory_selector sel(u);
    // The plain row is added first, so a stable sort without the favourites step keeps it first.
    sel.add_item(sel.own_inv_column, plain.get());
    sel.add_item(sel.own_inv_column, favourite.get());
    refresh_columns(sel);

    const std::vector<const inventory_entry*> rows = item_rows(sel.own_inv_column);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0]->any_item() == favourite.get());
    CHECK(rows[1]->any_item() == plain.get());
}

TEST_CASE("names sort case-insensitively as DDA's localized compare does",
          "[inventory][ui][fm]") {
    const std::locale previous_locale = std::locale();
    if (!try_set_utf8_locale()) {
        WARN("Skipped: the platform has no UTF-8 locale for the collation");
        return;
    }
    struct locale_restore {
        std::locale before;
        ~locale_restore() { std::locale::global(before); }
    } restore{previous_locale};

    auto apple_item = item::spawn("bandages");
    auto banana_item = item::spawn("bandages");
    auto cherry_item = item::spawn("bandages");

    inventory_entry apple({apple_item.get()}, nullptr, true);
    inventory_entry banana({banana_item.get()}, nullptr, true);
    inventory_entry cherry({cherry_item.get()}, nullptr, true);
    apple.update_cache();
    banana.update_cache();
    cherry.update_cache();
    apple.cached_name = "Apple";
    banana.cached_name = "banana";
    cherry.cached_name = "Cherry";

    // DDA's preset comparer is localized_compare, so raw byte order (Apple, Cherry, banana) is wrong.
    CHECK(default_preset.sort_compare(apple, banana));
    CHECK(default_preset.sort_compare(banana, cherry));
    CHECK_FALSE(default_preset.sort_compare(cherry, apple));
}

TEST_CASE("a filter shows matching rows inside a collapsed container and clearing it hides them again",
          "[inventory][ui][fm]") {
    carried_pack_fixture f;
    inventory_selector sel(f.u);
    sel.add_character_items(f.u);
    refresh_columns(sel);

    REQUIRE(sel.select(f.pack));
    sel.toggle_selected_contents();
    refresh_columns(sel);
    REQUIRE(f.pack->is_collapsed());
    REQUIRE(row_for(sel, f.rock) == nullptr);

    sel.own_inv_column.set_filter("rock");
    CHECK(row_for(sel, f.rock) != nullptr);
    CHECK(row_for(sel, f.blanket) == nullptr);

    sel.own_inv_column.set_filter("");
    CHECK(row_for(sel, f.rock) == nullptr);
}

TEST_CASE("merging rows into the own column keeps the highlighted row and the indent",
          "[inventory][ui][fm]") {
    const view_mode_state_guard view_mode;
    carried_pack_fixture f;
    fm3_selector sel(f.u);
    sel.enable_view_mode_toggle();
    sel.add_character_items(f.u);
    sel.toggle_categorize_contained(); // hierarchy: every row lives in the gear column
    refresh_columns(sel);
    REQUIRE(sel.own_inv_column.empty());

    const inventory_entry* before = nullptr;
    for (inventory_entry& row : sel.own_gear_column.entries) {
        if (row.is_item() && row.any_item() == f.rock) {
            before = &row;
        }
    }
    REQUIRE(before != nullptr);
    const int indent = before->indent;
    REQUIRE(indent > 0);
    REQUIRE(sel.select(f.rock));
    REQUIRE(sel.own_gear_column.get_selected().any_item() == f.rock);

    // Widths as prepare_layout sets them before rearrange_columns.
    sel.own_gear_column.reset_width({});
    sel.own_inv_column.reset_width({});
    // A one-cell-wide screen makes every column overflow, so the gear column is merged.
    sel.rearrange_columns(1);

    CHECK(sel.own_gear_column.empty());
    const inventory_entry* merged = nullptr;
    for (inventory_entry& row : sel.own_inv_column.entries) {
        if (row.is_item() && row.any_item() == f.rock) {
            merged = &row;
        }
    }
    REQUIRE(merged != nullptr);
    CHECK(sel.own_inv_column.indent_entries());
    CHECK(merged->indent == indent);
    CHECK(sel.get_selected().any_item() == f.rock);
}

TEST_CASE("selection column does not reserve denial width", "[inventory][ui][fm]") {
    clear_avatar();
    auto denied = item::spawn("bandages");
    auto allowed = item::spawn("bandages");

    denial_text_preset preset;
    preset.denial = std::string(120, 'x');
    inventory_entry denied_entry({denied.get()}, nullptr, false);

    // The rule that the selection column relies on: no denial space when asked for text only.
    inventory_column wide(preset);
    wide.expand_to_fit(denied_entry, false);
    const size_t text_width = wide.get_width();
    wide.expand_to_fit(denied_entry, true);
    CHECK(wide.get_width() > text_width);

    // The selection column measures the rows of the other columns with the rule above.
    inventory_column source(preset);
    source.add_entry(denied_entry);
    selection_column selection("FM3", "Selected");
    selection.reset_width({&source});
    const size_t denied_width = selection.get_width();

    preset.denial.clear();
    inventory_column source_allowed(preset);
    source_allowed.add_entry(inventory_entry({allowed.get()}, nullptr, true));
    selection.reset_width({&source_allowed});
    CHECK(selection.get_width() == denied_width);
}

TEST_CASE("first layout highlights the first selectable row", "[inventory][ui][fm]") {
    const view_mode_state_guard view_mode;
    const hide_override_guard hide_override;
    inventory_sel_default_state.uimode = inventory_selector::uimode::categories;
    carried_pack_fixture f;
    fm3_selector sel(f.u);
    sel.add_character_items(f.u);
    // CDF: add_character_items already pages its columns through move_entries_to -> prepare_paging
    // (DDA the same), so the first row is highlighted before the first draw; only the column
    // widths wait for the layout.
    sel.prepare_layout(120, 40);

    REQUIRE(sel.get_selected().is_item());
    // The layout puts the cursor on the first selectable row of the active (inventory) column.
    const inventory_column &active = sel.get_active_column();
    const auto first = std::find_if(active.entries.begin(), active.entries.end(),
    [](const inventory_entry &entry) {
        return entry.is_item() && entry.is_selectable();
    });
    REQUIRE(first != active.entries.end());
    CHECK(sel.get_selected().any_item() == first->any_item());
    // DDA's categories view puts the worn contents in the inventory column. A container is listed
    // under the category of its contents (item::get_category, src/item.cpp:10910), so the box row
    // (spare parts, like the rock it holds) is the first selectable row, not the worn pack in the
    // gear column and not the rock: the box's name sorts before "TEST rock".
    CHECK(sel.get_selected().any_item() == f.box);
}

// PL-63 FM4: header, footer, stats bars, count prompt and selection column
// (DDA inventory_ui.cpp:2663-2719, 3305-3323, 3416-3418, 4669-4675, 4917-4939, 1897-1900).
namespace {
auto header_stats_to_lines(const inventory_selector::header_stats& stats)
    -> std::vector<std::string> {
    std::vector<std::string> lines;
    for (const inventory_selector::header_stats_line& line : stats) {
        std::string joined;
        for (const std::string& cell : line) {
            joined += cell;
        }
        lines.push_back(joined);
    }
    return lines;
}

auto any_line_has(const std::vector<std::string>& lines, const std::string& text) -> bool {
    return std::any_of(lines.begin(), lines.end(),
                       [&text](const std::string& line) { return line.find(text) != std::string::npos; });
}

struct numpad_state_guard {
    bool saved = uistate.numpad_navigation;
    ~numpad_state_guard() { uistate.numpad_navigation = saved; }
};

struct count_hook_guard {
    ~count_hook_guard() {
        inventory_multiselector::query_count_test_hook = nullptr;
        inventory_multiselector::query_count_error_test_hook = nullptr;
    }
};

struct fm4_selector : inventory_selector {
    using inventory_selector::inventory_selector;
    using inventory_selector::view_mode_footer_text;
    using inventory_selector::header_numpad_text;
    using inventory_selector::reassign_invlets;
    using inventory_selector::on_input;
    using inventory_selector::ctxt;
    void set_uimode(uimode mode) { _uimode = mode; }
    auto get_uimode() const -> uimode { return _uimode; }
};

struct fm4_pick_selector : inventory_pick_selector {
    using inventory_pick_selector::inventory_pick_selector;
    using inventory_pick_selector::view_mode_footer_text;
    using inventory_pick_selector::ctxt;
};

struct fm4_multiselector : inventory_multiselector {
    using inventory_multiselector::inventory_multiselector;
    using inventory_multiselector::get_raw_stats;
    using inventory_multiselector::query_count;
    using inventory_multiselector::get_selection_column_items;
};

struct fm4_compare_selector : inventory_compare_selector {
    using inventory_compare_selector::inventory_compare_selector;
    using inventory_compare_selector::get_raw_stats;
};

struct fm4_iuse_selector : inventory_iuse_selector {
    using inventory_iuse_selector::inventory_iuse_selector;
    using inventory_iuse_selector::get_raw_stats;
    using inventory_iuse_selector::view_mode_footer_text;
    using inventory_selector::get_header_height;
};
} // namespace

// T1
TEST_CASE("footer shows the view-mode line in every selector", "[inventory][ui][fm]") {
    carried_pack_fixture f;
    fm4_selector base(f.u);
    const std::string expected = string_format(
        "< [%s] %s >", base.ctxt.get_desc("VIEW_CATEGORY_MODE"), _("categories"));
    CHECK(base.view_mode_footer_text() == expected);
    base.set_uimode(inventory_selector::uimode::hierarchy);
    CHECK(base.view_mode_footer_text().find(_("hierarchy")) != std::string::npos);
    CHECK(base.view_mode_footer_text().find(_("categories")) == std::string::npos);

    fm4_pick_selector pick(f.u);
    CHECK(pick.view_mode_footer_text().find(_("categories")) != std::string::npos);

    fm4_iuse_selector iuse(f.u, "test");
    CHECK(iuse.view_mode_footer_text().find(_("categories")) != std::string::npos);

    ui2_drop_selector drop(f.u);
    CHECK(drop.view_mode_footer_text().find(_("categories")) != std::string::npos);
}

// T2
TEST_CASE("numpad marker appears only when the state is on", "[inventory][ui][fm]") {
    numpad_state_guard guard;
    carried_pack_fixture f;
    fm4_selector sel(f.u);
    uistate.numpad_navigation = false;
    CHECK(sel.header_numpad_text().empty());

    auto rock = item::spawn("rock");
    sel.add_item(sel.map_column, rock.get());
    sel.map_column.prepare_paging();
    sel.reassign_invlets();
    const inventory_entry* row = row_for(sel, rock.get());
    REQUIRE(row != nullptr);
    CHECK(row->get_invlet() == '0');

    sel.on_input(inventory_input{"TOGGLE_NUMPAD_NAVIGATION", 0, nullptr});
    CHECK(uistate.numpad_navigation);
    CHECK(sel.header_numpad_text() == _("Numpad ON"));
    CHECK(row->get_invlet() == 0);

    sel.on_input(inventory_input{"TOGGLE_NUMPAD_NAVIGATION", 0, nullptr});
    CHECK_FALSE(uistate.numpad_navigation);
    CHECK(row->get_invlet() == '0');

    count_hook_guard hook;
    bool queried = false;
    inventory_multiselector::query_count_test_hook =
        [&queried](const std::string&) {
        queried = true;
        return std::optional<std::string>("1");
    };
    fm4_multiselector ms(f.u);
    uistate.numpad_navigation = true;
    CHECK_FALSE(ms.handle_mark_action("", '3'));
    CHECK_FALSE(queried);
    uistate.numpad_navigation = false;
    ms.handle_mark_action("", '3');
    CHECK(queried);
}

// T3
TEST_CASE("compare and haul style multiselector has an empty stats list", "[inventory][ui][fm]") {
    clear_avatar();
    avatar& u = get_avatar();
    fm4_compare_selector compare(u);
    compare.add_character_items(u);
    compare.own_inv_column.prepare_paging();
    CHECK(compare.get_raw_stats().empty());

    // The 3-argument multiselector without a stats callback (DDA's compare/haul shape).
    fm4_multiselector plain(u);
    CHECK(plain.get_raw_stats().empty());
}

// T4
TEST_CASE("iuse selector header height is 1 without a callback", "[inventory][ui][fm]") {
    clear_avatar();
    fm4_iuse_selector sel(get_avatar(), "test");
    CHECK(sel.get_raw_stats().empty());
    CHECK(sel.get_header_height() == 1);
}

// T5
TEST_CASE("drop selector stats list has the summary and a Selected line", "[inventory][ui][fm]") {
    carried_pack_fixture f;
    ui2_drop_selector sel(f.u);
    sel.add_character_items(f.u);
    refresh_columns(sel);

    std::vector<std::string> lines = header_stats_to_lines(sel.get_raw_stats());
    REQUIRE_FALSE(lines.empty());
    CHECK(lines.front().find("Bulk Volume:") != std::string::npos);
    CHECK(lines.front().find("Total Weight:") != std::string::npos);
    CHECK(lines.back().find("Selected") != std::string::npos);
    // the predicted-drop numbers stay as extra lines (QUESTIONS Q3)
    CHECK(any_line_has(lines, "Weight ("));
    CHECK(any_line_has(lines, "Volume ("));

    const std::string empty_selection = lines.back();
    press_toggle_entry(sel, f.blanket);
    lines = header_stats_to_lines(sel.get_raw_stats());
    REQUIRE_FALSE(lines.empty());
    CHECK(lines.back().find("Selected") != std::string::npos);
    CHECK(lines.back() != empty_selection);
}

// T6
TEST_CASE("selection column keeps non-matching marked items when a filter is set",
          "[inventory][ui][fm]") {
    carried_pack_fixture f;
    ui2_drop_selector sel(f.u);
    sel.add_character_items(f.u);
    refresh_columns(sel);
    press_toggle_entry(sel, f.box);
    press_toggle_entry(sel, f.blanket);

    sel.apply_filter("blanket");

    bool has_box = false;
    bool has_blanket = false;
    for (const inventory_entry* entry : sel.get_selection_column_items()) {
        has_box |= entry->any_item() == f.box;
        has_blanket |= entry->any_item() == f.blanket;
    }
    CHECK(has_box);
    CHECK(has_blanket);
}

// T7
TEST_CASE("count prompt rejects text and starts empty", "[inventory][ui][fm]") {
    clear_avatar();
    fm4_multiselector sel(get_avatar(), default_preset);
    count_hook_guard hook;
    std::string initial = "unset";
    inventory_multiselector::query_count_test_hook =
        [&initial](const std::string& text) {
        initial = text;
        return std::optional<std::string>("5");
    };

    CHECK(sel.query_count() == 5);
    CHECK(initial.empty());
    CHECK(sel.query_count('7', true) == 5);
    CHECK(initial == "7");

    std::string error;
    inventory_multiselector::query_count_test_hook =
        [](const std::string&) {
        return std::optional<std::string>("x");
    };
    inventory_multiselector::query_count_error_test_hook =
        [&error](const std::string& text) {
        error = text;
    };
    CHECK(sel.query_count() == -1);
    CHECK(error == _("That is not an integer."));

    inventory_multiselector::query_count_test_hook =
        [](const std::string&) {
        return std::optional<std::string>();
    };
    CHECK(sel.query_count() == -1);
}

// T8
TEST_CASE("pickup hint lists the five actions", "[inventory][ui][fm]") {
    pickup_menu_fixture f;
    const std::string& hint = f.sel.get_hint();
    for (const char* action : {"WIELD", "WEAR", "SHOW_HIDE_CONTENTS", "SHOW_HIDE_CONTENTS_ALL",
                               "EXAMINE"}) {
        CAPTURE(action);
        const std::string cell = "<color_yellow>" + f.sel.ctxt.get_desc(action) + "</color>";
        CHECK(hint.find(cell) != std::string::npos);
    }
    // An unbound action prints what the context prints for it (DDA's All entry). The action has
    // an INVENTORY keybinding row with no keys, so get_desc reports it as unbound locally.
    CHECK(f.sel.ctxt.get_desc("SHOW_HIDE_CONTENTS_ALL") == _("Unbound locally!"));
    // The Quantity line stays.
    CHECK(hint.find("Quantity:") != std::string::npos);
}

// PL-63 FM7: saved UI state (DDA inventory_ui.cpp:345-364, 3446-3447, 3497-3503).
// T1
// CDF: uistatedata::serialize/deserialize live in savegame_json.cpp, which this change may not edit;
// the round trip therefore goes through save_inv_state/load_inv_state, the pair that serializer has
// to call on every save and load.
TEST_CASE("view mode survives a uistate round trip", "[inventory][ui][fm]") {
    const view_mode_state_guard view_mode;
    clear_avatar();
    avatar& u = get_avatar();

    inventory_ui_default_state.uimode = inventory_selector::uimode::hierarchy;

    std::ostringstream out;
    JsonOut jsout(out);
    jsout.start_object();
    save_inv_state(jsout);
    jsout.end_object();

    // a later session starts from the compiled-in default; the load must bring the saved mode back
    inventory_ui_default_state.uimode = inventory_selector::uimode::categories;
    std::istringstream in(out.str());
    JsonIn jsin(in);
    load_inv_state(jsin.get_object());
    CHECK(inventory_ui_default_state.uimode == inventory_selector::uimode::hierarchy);

    // and a selector that carries the state starts in the saved mode
    inventory_selector_preset preset;
    preset.save_state = &inventory_ui_default_state;
    fm4_selector sel(u, preset);
    CHECK(sel.get_uimode() == inventory_selector::uimode::hierarchy);
    // CDF: the columns are put in hierarchy mode too, so the nested rows are indented and grouped
    CHECK(sel.own_gear_column.indent_entries());
}

// T2
TEST_CASE("pickup selector defaults to hierarchy, inventory selector to categories",
          "[inventory][ui][fm]") {
    const view_mode_state_guard view_mode;
    clear_avatar();
    avatar& u = get_avatar();

    // DDA inventory_ui.cpp:345-348: the four saved defaults
    CHECK(inventory_sel_default_state.uimode == inventory_selector::uimode::categories);
    CHECK(inventory_ui_default_state.uimode == inventory_selector::uimode::categories);
    CHECK(pickup_sel_default_state.uimode == inventory_selector::uimode::categories);
    CHECK(pickup_ui_default_state.uimode == inventory_selector::uimode::hierarchy);

    // a preset without a state pointer falls back to the inventory default
    fm4_selector plain(u);
    CHECK(plain.get_uimode() == inventory_selector::uimode::categories);
    CHECK_FALSE(plain.own_gear_column.indent_entries());

    // the pickup screen's preset carries the pickup state, which opens in hierarchy
    inventory_selector_preset pickup;
    pickup.save_state = &pickup_ui_default_state;
    fm4_selector pick(u, pickup);
    CHECK(pick.get_uimode() == inventory_selector::uimode::hierarchy);

    // DDA game_inventory.cpp:562-564: the pickup preset class remembers its own state
    pickup_inventory_preset pickup_preset(u);
    CHECK(pickup_preset.save_state == &pickup_sel_default_state);
}

// T3
TEST_CASE("toggling in one selector kind does not change the other kind's default",
          "[inventory][ui][fm]") {
    const view_mode_state_guard view_mode;
    clear_avatar();
    avatar& u = get_avatar();

    inventory_selector_preset inventory_preset;
    inventory_preset.save_state = &inventory_ui_default_state;
    inventory_selector_preset pickup_preset;
    pickup_preset.save_state = &pickup_ui_default_state;

    pickup_ui_default_state.uimode = inventory_selector::uimode::categories;
    inventory_ui_default_state.uimode = inventory_selector::uimode::categories;

    {
        fm4_selector pick(u, pickup_preset);
        REQUIRE(pick.get_uimode() == inventory_selector::uimode::categories);
        pick.toggle_categorize_contained();
    }
    CHECK(pickup_ui_default_state.uimode == inventory_selector::uimode::hierarchy);
    // the other kind and the fallback keep their own mode
    CHECK(inventory_ui_default_state.uimode == inventory_selector::uimode::categories);
    CHECK(inventory_sel_default_state.uimode == inventory_selector::uimode::categories);

    {
        fm4_selector inv(u, inventory_preset);
        REQUIRE(inv.get_uimode() == inventory_selector::uimode::categories);
        inv.toggle_categorize_contained();
    }
    CHECK(inventory_ui_default_state.uimode == inventory_selector::uimode::hierarchy);
    CHECK(pickup_ui_default_state.uimode == inventory_selector::uimode::hierarchy);
}

// T5
// The row lives in the INVENTORY context of data/raw/keybindings/keybindings.json; the selector
// registers TOGGLE_NUMPAD_NAVIGATION in that context (DDA inventory_ui.cpp:3472).
TEST_CASE("numpad key row exists and has no duplicate key in its context",
          "[inventory][ui][fm]") {
    std::ifstream file("data/raw/keybindings/keybindings.json", std::ios::binary);
    REQUIRE(file.good());
    const std::string contents{ std::istreambuf_iterator<char>(file),
                                std::istreambuf_iterator<char>() };

    std::istringstream in(contents);
    JsonIn jsin(in);
    std::vector<std::string> numpad_keys;
    int other_inventory_f5 = 0;
    for (JsonObject row : jsin.get_array()) {
        if (!row.has_string("category") || row.get_string("category") != "INVENTORY") {
            continue;
        }
        std::vector<std::string> keys;
        for (JsonObject binding : row.get_array("bindings")) {
            keys.push_back(binding.get_string("key"));
        }
        if (row.get_string("id") == "TOGGLE_NUMPAD_NAVIGATION") {
            numpad_keys = keys;
            continue;
        }
        for (const std::string& key : keys) {
            if (key == "F5") {
                ++other_inventory_f5;
                CAPTURE(row.get_string("id"));
            }
        }
    }
    REQUIRE(numpad_keys.size() == 1);
    CHECK(numpad_keys.front() == "F5");
    CHECK(other_inventory_f5 == 0);
}

// PL-64 G32: the carried tree lists the pocket types the preset asks for (DDA inventory_ui.cpp:
// 2109-2141, default { pocket_type::CONTAINER }), so a gun's mod and magazine well are not child
// rows, while the contents of a worn bag still are.
TEST_CASE("contained items follow the preset pocket types", "[inventory][ui][fm]") {
    const view_mode_state_guard view_mode;
    clear_all_state();
    avatar& u = get_avatar();
    clear_character(u, true);

    // a box in a worn bag: a container pocket row
    u.wear_item(item::spawn(itype_id("backpack_hiking"), calendar::turn), false);
    item& bag = *u.worn.front();
    item& box = u.i_add(item::spawn(itype_id("box_small"), calendar::turn));
    REQUIRE(box.parent_item() == &bag);

    // a gun in the same bag with a mod in its MOD pocket: the mod is not a child row
    detached_ptr<item> gun_ptr = item::spawn(itype_id("glock_19"), calendar::turn);
    REQUIRE(gun_ptr->has_pocket_type(pocket_type::MOD));
    detached_ptr<item> mod = item::spawn(itype_id("holo_sight"), calendar::turn);
    item* const mod_ptr = mod.get();
    REQUIRE(gun_ptr->put_in(std::move(mod), pocket_type::MOD).success());
    // and an empty magazine in its well: the well is not a listing pocket either
    detached_ptr<item> mag = item::spawn(itype_id("glockmag"), calendar::turn);
    item* const mag_ptr = mag.get();
    REQUIRE(gun_ptr->put_in(std::move(mag), pocket_type::MAGAZINE_WELL).success());
    item& gun = u.i_add(std::move(gun_ptr));
    REQUIRE(gun.parent_item() == &bag);
    REQUIRE(mod_ptr->parent_item() == &gun);
    REQUIRE(mag_ptr->parent_item() == &gun);

    inventory_selector sel(u);
    sel.enable_view_mode_toggle();
    sel.add_character_items(u);
    sel.toggle_categorize_contained();
    sel.own_gear_column.prepare_paging();

    const std::vector<const inventory_entry*> rows = item_rows(sel.own_gear_column);
    REQUIRE(rows.size() == 3);
    CHECK(rows[0]->any_item() == &bag);
    CHECK(rows[0]->chevron);

    const auto row_of = [&rows](const item* it) -> const inventory_entry* {
        for (const inventory_entry* row : rows) {
            if (row->any_item() == it) {
                return row;
            }
        }
        return nullptr;
    };

    const inventory_entry* box_row = row_of(&box);
    REQUIRE(box_row != nullptr);
    CHECK(box_row->indent == 2);
    CHECK(box_row->topmost_parent == &bag);

    const inventory_entry* gun_row = row_of(&gun);
    REQUIRE(gun_row != nullptr);
    CHECK(gun_row->indent == 2);
    // the gun has no listed contents, so it gets no chevron
    CHECK_FALSE(gun_row->chevron);
    // the mod in the gun's MOD pocket is not a row of its own
    CHECK(row_of(mod_ptr) == nullptr);
    // neither is the magazine in its well
    CHECK(row_of(mag_ptr) == nullptr);
}

// PL-64 IN2: input-path fixes (G02-G05, G19-G22, G31).
namespace {
struct in2_pick_probe : inventory_pick_selector {
    using inventory_pick_selector::inventory_pick_selector;
    using inventory_selector::process_input;
    using inventory_selector::ctxt;
    auto actions() const -> std::vector<std::string> {
        return ctxt.get_registered_actions_copy();
    }
};

struct in2_drop_probe : inventory_drop_selector {
    using inventory_drop_selector::inventory_drop_selector;
    using inventory_selector::ctxt;
    auto actions() const -> std::vector<std::string> {
        return ctxt.get_registered_actions_copy();
    }
};

struct in2_compare_probe : inventory_compare_selector {
    using inventory_compare_selector::inventory_compare_selector;
    using inventory_selector::ctxt;
    auto actions() const -> std::vector<std::string> {
        return ctxt.get_registered_actions_copy();
    }
};

struct in2_pickup_probe : inventory_pickup_selector {
    using inventory_pickup_selector::inventory_pickup_selector;
    using inventory_selector::ctxt;
    auto actions() const -> std::vector<std::string> {
        return ctxt.get_registered_actions_copy();
    }
};

auto in2_has_action(const std::vector<std::string>& actions, const std::string& id) -> bool {
    return std::find(actions.begin(), actions.end(), id) != actions.end();
}
} // namespace

TEST_CASE("the view mode key is registered in every selector kind", "[inventory][ui][in2]") {
    clear_all_state();
    avatar& you = get_avatar();

    // G03/G04: DDA registers VIEW_CATEGORY_MODE in the base ctor (inventory_ui.cpp:3471) and
    // WIELD/WEAR only in the pickup selector (4997-4998).
    in2_pick_probe pick(you);
    CHECK(in2_has_action(pick.actions(), "VIEW_CATEGORY_MODE"));
    CHECK_FALSE(in2_has_action(pick.actions(), "WIELD"));
    CHECK_FALSE(in2_has_action(pick.actions(), "WEAR"));

    in2_pickup_probe pickup(you);
    CHECK(in2_has_action(pickup.actions(), "VIEW_CATEGORY_MODE"));
    CHECK(in2_has_action(pickup.actions(), "WIELD"));
    CHECK(in2_has_action(pickup.actions(), "WEAR"));

    in2_drop_probe drop(you);
    CHECK(in2_has_action(drop.actions(), "VIEW_CATEGORY_MODE"));
    CHECK_FALSE(in2_has_action(drop.actions(), "WIELD"));
    CHECK_FALSE(in2_has_action(drop.actions(), "WEAR"));

    in2_compare_probe compare(you);
    CHECK(in2_has_action(compare.actions(), "VIEW_CATEGORY_MODE"));
}

TEST_CASE("a bound key is not stolen by an item invlet", "[inventory][ui][in2]") {
    // G22: an item with invlet r must not take the reset-filter key. Only an unbound key
    // (ANY_INPUT) selects a row by its letter.
    clear_all_state();
    avatar& you = get_avatar();
    item& bandage = you.i_add(item::spawn("bandages"));
    bandage.invlet = 'r';

    in2_pick_probe sel(you);
    sel.add_character_items(you);
    sel.own_inv_column.prepare_paging();

    const inventory_input bound = sel.process_input("RESET_FILTER", 'r');
    CHECK(bound.entry == nullptr);

    const inventory_input unbound = sel.process_input("ANY_INPUT", 'r');
    REQUIRE(unbound.entry != nullptr);
    CHECK(unbound.entry->any_item() == &bandage);
}

TEST_CASE("a digit bound to an action is not a count in a multiselector",
          "[inventory][ui][in2]") {
    // G19: 4 is NEXT_COLUMN and 6 is TOGGLE_ENTRY. They keep their action instead of opening the
    // count prompt. Only an unbound digit seeds the prompt.
    clear_all_state();
    avatar& you = get_avatar();
    item& rock = you.i_add(item::spawn("rock"));

    inventory_iuse_selector sel(you, "test");
    sel.add_character_items(you);
    sel.own_inv_column.prepare_paging();
    REQUIRE(sel.select(&rock));

    CHECK_FALSE(sel.handle_mark_action("NEXT_COLUMN", '4'));
    CHECK(sel.handle_mark_action("TOGGLE_ENTRY", '6'));
    const inventory_entry* row = row_for(sel, &rock);
    REQUIRE(row != nullptr);
    CHECK(row->chosen_count > 0);
}

TEST_CASE("examine with no highlighted row is a no-op", "[inventory][ui][in2]") {
    // G31/R-001: the column handler used to call get_selected().any_item() without a guard.
    clear_all_state();
    auto disabled = item::spawn("bandages");

    inventory_selector_preset preset;
    inventory_column col(preset);
    col.add_entry(inventory_entry({disabled.get()}, nullptr, false));
    col.invalidate_paging();
    col.on_activate();
    col.prepare_paging();

    // skip_unselectable is on by default, so the only row cannot be highlighted.
    REQUIRE_FALSE(col.get_selected().is_item());
    col.on_input(inventory_input{"EXAMINE", 0, nullptr});
    CHECK_FALSE(col.get_selected().is_item());
}

// PL-64 MS1: mouse in every selector (DDA inventory_ui.cpp:3452-3453, 3468-3469, 4774-4781).
namespace {
struct ms1_iuse_probe : inventory_iuse_selector {
    using inventory_iuse_selector::inventory_iuse_selector;
    using inventory_multiselector::handle_entry_action;
    using inventory_selector::ctxt;
    auto actions() const -> std::vector<std::string> {
        return ctxt.get_registered_actions_copy();
    }
};

struct ms1_ammo_probe : ammo_inventory_selector {
    using ammo_inventory_selector::ammo_inventory_selector;
    using inventory_selector::ctxt;
    auto actions() const -> std::vector<std::string> {
        return ctxt.get_registered_actions_copy();
    }
};
} // namespace

TEST_CASE("mouse actions are registered in every selector kind", "[inventory][ui][ms1]") {
    clear_all_state();
    avatar& you = get_avatar();
    auto rock = item::spawn("rock");

    in2_pick_probe pick(you);
    in2_drop_probe drop(you);
    in2_compare_probe compare(you);
    in2_pickup_probe pickup(you);
    ms1_iuse_probe iuse(you, "test");
    ms1_ammo_probe ammo(you, *rock);

    struct named_actions {
        const char* name;
        std::vector<std::string> actions;
    };
    const std::vector<named_actions> probes{
        {"pick", pick.actions()},
        {"drop/insert", drop.actions()},
        {"compare", compare.actions()},
        {"pickup", pickup.actions()},
        {"iuse", iuse.actions()},
        {"ammo", ammo.actions()},
    };
    for (const named_actions& probe : probes) {
        CAPTURE(probe.name);
        for (const char* action : {"COORDINATE", "MOUSE_MOVE", "SELECT", "CLICK_AND_DRAG"}) {
            CAPTURE(action);
            CHECK(in2_has_action(probe.actions, action));
        }
    }
}

TEST_CASE("multiselector hover highlights a row and a click toggles it",
          "[inventory][ui][ms1]") {
    clear_all_state();
    avatar& you = get_avatar();
    item& rock = you.i_add(item::spawn("rock"));

    ms1_iuse_probe sel(you, "test");
    sel.add_character_items(you);
    sel.own_inv_column.prepare_paging();
    inventory_entry* row = entry_for(sel, &rock);
    REQUIRE(row != nullptr);

    sel.handle_entry_action(inventory_input{"MOUSE_MOVE", 0, row, true});
    CHECK(sel.get_selected().any_item() == &rock);
    CHECK(row->chosen_count == 0);

    sel.handle_entry_action(inventory_input{"CLICK_AND_DRAG", 0, row, true});
    CHECK(row->chosen_count == 0);

    sel.handle_entry_action(inventory_input{"SELECT", 0, row, true});
    CHECK(row->chosen_count > 0);
    sel.handle_entry_action(inventory_input{"SELECT", 0, row, true});
    CHECK(row->chosen_count == 0);

    // a key invlet is still a mark, as in DDA's ANY_INPUT branch
    sel.handle_entry_action(inventory_input{"ANY_INPUT", rock.invlet, row, false});
    CHECK(row->chosen_count > 0);
}

// NEST1: the drop, pickup and unload selectors list and act on the nested armor rows (they share
// add_character_items and the column entries).
TEST_CASE("drop pickup and unload selectors list the nested plates", "[inventory][nest]") {
    const view_mode_state_guard view_mode;
    worn_vest_fixture f;
    item* const plate = f.plates.front();

    SECTION("drop selector marks a nested plate") {
        ui2_drop_selector sel(f.u);
        sel.add_character_items(f.u);
        refresh_columns(sel);
        REQUIRE(row_for(sel, plate) != nullptr);
        REQUIRE(row_for(sel, plate)->is_selectable());
        press_toggle_entry(sel, plate);
        CHECK(chosen_of(sel, plate) > 0);
    }
    SECTION("pickup selector lists a nested plate") {
        inventory_pickup_selector sel(f.u);
        sel.add_character_items(f.u);
        refresh_columns(sel);
        REQUIRE(row_for(sel, plate) != nullptr);
        CHECK(row_for(sel, plate)->topmost_parent == f.vest);
    }
    SECTION("unload selector lists a nested plate") {
        unload_selector sel(f.u);
        sel.add_character_items(f.u);
        refresh_columns(sel);
        REQUIRE(row_for(sel, plate) != nullptr);
        CHECK(row_for(sel, plate)->is_selectable());
    }
}
