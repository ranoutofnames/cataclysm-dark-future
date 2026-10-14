#include "avatar.h"
#include "catch/catch.hpp"
#include "drop_preview.h"
#include "inventory_ui.h"
#include "item.h"
#include "player_helpers.h"
#include "state_helpers.h"

#include <algorithm>

namespace {

class drop_preview_selector: public inventory_drop_selector {
public:
    using inventory_drop_selector::get_drop_preview;
    using inventory_drop_selector::inventory_drop_selector;
    using inventory_drop_selector::process_selected;
    using inventory_drop_selector::set_chosen_count;
    using inventory_drop_selector::update_drop_preview;
    using inventory_multiselector::get_selection_column_items;
};

} // namespace

TEST_CASE(
    "category drop preview groups linked items with each container",
    "[inventory][ui][drop_token]") {
    clear_all_state();
    auto& owner = get_avatar();
    clear_character(owner, false);
    REQUIRE_FALSE(owner.wear_item(item::spawn("test_backpack"), false));
    REQUIRE_FALSE(owner.wear_item(item::spawn("test_briefcase"), false));
    auto id = 0;
    for (auto* clothing : owner.worn) {
        clothing->set_var("DROP_WITH_CLOTHING_ID", ++id);
        auto object = item::spawn("test_amputator");
        object->set_var("DROP_WITH_CLOTHING_TARGET", id);
        owner.i_add(std::move(object));
    }

    auto selector = drop_preview_selector(owner);
    selector.add_character_items(owner);
    const auto gear = selector.own_gear_column.get_all_entries([](const auto& entry) {
        return entry.is_item();
    });
    auto count = 0;
    selector.process_selected(count, gear);
    selector.update_drop_preview();
    const auto selected = selector.get_selection_column_items();
    REQUIRE(selected.size() == 4);
    for (auto index = size_t{0}; index < selected.size(); index += 2) {
        const auto* clothing = selected[index]->any_item();
        const auto* linked = selected[index + 1]->any_item();
        CHECK(owner.is_worn(*clothing));
        CHECK(linked->get_var("DROP_WITH_CLOTHING_TARGET", 0)
              == clothing->get_var("DROP_WITH_CLOTHING_ID", -1));
    }
}
