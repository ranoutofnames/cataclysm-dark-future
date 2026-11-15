#pragma once
#ifndef CATA_TESTS_KEY_SCRIPT_HELPER_H
#    define CATA_TESTS_KEY_SCRIPT_HELPER_H

#    include "catch/catch.hpp"
#    include "input.h"
#    include "inventory_ui.h"
#    include "item.h"
#    include "item_handling_util.h"
#    include "pickup_token.h"

#    include <concepts>
#    include <functional>
#    include <optional>
#    include <string>
#    include <utility>
#    include <vector>

// CDF: PL-64 IN1. Drives a real screen (an inventory_selector's execute(), or a screen that owns
// its selector, e.g. game_menus::inv::common) from a scripted key list. Input goes through
// input_manager::test_push_event(), read first by both get_input_event() implementations; when the
// script runs out the queue answers with a timeout event instead of throwing, so the screen closes
// through its own QUIT path and run_screen reports still_open. ui_manager::redraw() is a no-op in
// test mode, so the harness lays the selector out once itself (test_prepare_layout) and the
// screen's own redraw calls are the frame pump and no window is needed.
//
// Keys are key names from the keybinding tables ("j", "RETURN", "ESC", "TAB", "F5", ...); a single
// character is itself. Expected results in the tests come from DDA's code (b4b62254ae2), never from
// the CDF code under test.

using key_script_keys = std::vector<std::string>;

struct key_script_result {
    // The script ran out while the screen was still open (the screen did not close on these keys).
    bool still_open = false;
    // The empty-queue flag was set (same condition as still_open: the screen asked for one more key).
    bool exhausted = false;
    // The selector's view mode at the end: "categories" or "hierarchy" (the `;` toggle).
    std::string mode;
    // Index of the active column at the end.
    size_t active_column = 0;
    // The selected item row at the end, or nullptr for a category row / no row.
    const item *selection = nullptr;
    // execute()'s result: every returned item, and the amount for count-carrying results.
    const item *returned = nullptr;
    std::vector<const item *> returned_items;
    std::vector<int> returned_counts;
};

namespace key_script_detail
{

inline auto script_event(const std::string &key) -> input_event
{
    const bool mouse = key.rfind("MOUSE_", 0) == 0 || key == "SCROLL_UP" ||
                       key == "SCROLL_DOWN";
    int code = 0;
    if (key.size() == 1) {
        code = static_cast<unsigned char>(key[0]);
    } else {
        code = inp_mngr.get_keycode(key);
        REQUIRE(code != 0);
    }
    return input_event(code, mouse ? input_event_t::mouse : input_event_t::keyboard);
}

// The count prompt (inventory_multiselector::query_count) would open a real string_input_popup
// window, which no scripted run can answer. The harness returns the queued digit the prompt was
// opened with instead; the next RETURN in the script then confirms the count as it confirms the
// popup. A test that needs another answer sets query_count_test_hook itself.
inline auto count_prompt_answer( const std::string &init_str ) -> std::optional<std::string>
{
    return init_str;
}

// Fills the queue on construction of a run and always leaves the manager clean, also when a
// Catch2 assertion unwinds through it. handle_input() restores the input timeout only on its
// normal exit, so the run restores it here as well.
class queue_guard
{
    public:
        queue_guard() : old_timeout( inp_mngr.get_timeout() ),
            old_query_count( inventory_multiselector::query_count_test_hook ) {}
        ~queue_guard() {
            inp_mngr.test_clear_input_queue();
            inp_mngr.set_timeout(old_timeout);
            inventory_multiselector::query_count_test_hook = old_query_count;
        }
        queue_guard(const queue_guard &) = delete;
        queue_guard &operator=(const queue_guard &) = delete;

        void fill(const key_script_keys &keys) {
            inp_mngr.test_clear_input_queue();
            for (const std::string &key : keys) {
                inp_mngr.test_push_event(script_event(key));
            }
            // A run with no keys at all still has to fail on the first read, not block on getch().
            inp_mngr.test_enable_input_queue();
            // A test may install its own answer for the count prompt before the run; only the
            // default is filled in here.
            if( !inventory_multiselector::query_count_test_hook ) {
                inventory_multiselector::query_count_test_hook = count_prompt_answer;
            }
        }

    private:
        int old_timeout;
        std::function<std::optional<std::string>( const std::string & )> old_query_count;
};

inline void note_return(key_script_result &result, const item *it)
{
    if (it != nullptr) {
        result.returned_items.push_back(it);
        result.returned_counts.push_back(0);
    }
}

inline void note_return(key_script_result &result, const std::pair<const item *, const item *> &pair)
{
    note_return(result, pair.first);
    note_return(result, pair.second);
}

inline void note_return(key_script_result &result, const iuse_location &loc)
{
    if (loc.loc) {
        note_return(result, loc.loc.get());
        result.returned_counts.back() = loc.count;
    }
}

inline void note_return(key_script_result &result, const pickup::pick_drop_selection &sel)
{
    if (sel.target) {
        note_return(result, sel.target.get());
        result.returned_counts.back() = sel.quantity.value_or(0);
    }
}

inline void note_return(key_script_result &result, const unload_selection &sel)
{
    note_return(result, sel.to_unload);
}

inline void note_return(key_script_result &result, const drop_locations &drops)
{
    for (const drop_location &drop : drops) {
        note_return(result, drop);
    }
}

inline void note_return(key_script_result &, int)
{
    // Screens like inventory_examiner return a status code, not an item.
}

template <typename T>
inline void note_return(key_script_result &result, const std::vector<T> &values)
{
    for (const T &value : values) {
        note_return(result, value);
    }
}

inline void finish(key_script_result &result, inventory_selector &sel)
{
    result.active_column = sel.get_selection_position().first;
    result.mode = sel.get_active_column().hierarchy ? "hierarchy" : "categories";
    const inventory_entry &selected = sel.get_active_column().get_selected();
    result.selection = selected.is_item() ? selected.any_item() : nullptr;
    if (!result.returned_items.empty()) {
        result.returned = result.returned_items.front();
    }
}

} // namespace key_script_detail

// Runs sel.execute() with the scripted keys. The selector is laid out once first: ui_manager::
// redraw() is a no-op in test mode, so the frame pump never calls prepare_layout(). A screen that
// closes before the keys run out returns normally (still_open false, returned_items set); a screen
// still open when they run out closes through get_input()'s QUIT and is reported as still_open
// (the input queue is exhausted).
template <typename Selector>
    requires requires(Selector &sel) { sel.execute(); sel.test_prepare_layout(); }
auto run_screen(Selector &sel, const key_script_keys &keys) -> key_script_result
{
    key_script_result result;
    key_script_detail::queue_guard guard;
    guard.fill(keys);
    sel.test_prepare_layout();
    const auto value = sel.execute();
    key_script_detail::note_return(result, value);
    result.exhausted = inp_mngr.test_input_exhausted();
    result.still_open = result.exhausted;
    key_script_detail::finish(result, sel);
    return result;
}

// Runs a screen that owns its selector (game_menus::inv::common). The selector is local to the
// screen, so only still_open and the queue state can be reported.
template <typename Screen>
    requires std::invocable<const Screen &>
auto run_screen(const Screen &screen, const key_script_keys &keys) -> key_script_result
{
    key_script_result result;
    key_script_detail::queue_guard guard;
    guard.fill(keys);
    screen();
    result.exhausted = inp_mngr.test_input_exhausted();
    result.still_open = result.exhausted;
    return result;
}

#endif // CATA_TESTS_KEY_SCRIPT_HELPER_H
