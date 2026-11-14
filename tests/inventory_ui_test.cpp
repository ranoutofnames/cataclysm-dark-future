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
#include "uistate.h"

#include <array>
#include <optional>
#include <utility>
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
    // enabled, disabled, enabled
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
    REQUIRE(col.entries[row_index[1]].any_item() == items[1].get());
    REQUIRE_FALSE(col.entries[row_index[1]].is_selectable());

    col.toggle_skip_unselectable(true);
    CHECK(col.next_highlightable_index(row_index[0], scroll_direction::FORWARD) == row_index[2]);
    CHECK(col.next_highlightable_index(row_index[2], scroll_direction::BACKWARD) == row_index[0]);

    col.toggle_skip_unselectable(false);
    CHECK(col.next_highlightable_index(row_index[0], scroll_direction::FORWARD) == row_index[1]);
    CHECK(col.next_highlightable_index(row_index[2], scroll_direction::BACKWARD) == row_index[1]);

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
        // B-L1: the first-line hints of an action with no bound key are left out (no "Unbound" cell).
        const bool bound = !inp_mngr.get_input_for_action(action, "INVENTORY").empty();
        const bool first_line_item = std::string(action) != "MARK_WITH_COUNT" &&
                                     std::string(action) != "INCREASE_COUNT" &&
                                     std::string(action) != "DECREASE_COUNT" &&
                                     std::string(action) != "TOGGLE_ENTRY";
        if (bound || !first_line_item) {
            CHECK(hint.find(cell) != std::string::npos);
        } else {
            CHECK(hint.find(cell) == std::string::npos);
        }
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
