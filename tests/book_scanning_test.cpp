#include "avatar.h"
#include "book_scanning.h"
#include "calendar.h"
#include "cata_utility.h"
#include "catch/catch.hpp"
#include "character_functions.h"
#include "digital_media.h"
#include "game.h"
#include "inventory_ui.h"
#include "key_script_helper.h"
#include "temp_crafting_inventory.h"
#include "item.h"
#include "item_contents.h"
#include "itype.h"
#include "json.h"
#include "map/map.h"
#include "map_helpers.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "skill.h"
#include "state_helpers.h"
#include "text_snippets.h"
#include "itype_slots/islot_book.h"

#include <sstream>

// CDF: defined in src/item/book_scanning.cpp and also declared in src/iuse.cpp; keep this
// signature identical. Not in book_scanning.h, where a declaration would rebuild every includer.
// The key-script harness cannot drive the selector this function creates (see the helper below),
// so the scan menu test builds the same menu itself.
auto select_scannable_text( avatar &you ) -> item *;

namespace {
// CDF: P3 keeps a tool's battery as an item in its integral MAGAZINE pocket, so a reader's
// contents hold the battery next to the digital files. Find the files by their DIGITAL flag
// instead of by position, and count only them.
auto digital_files_on(item& media) -> std::vector<item*> {
    auto files = std::vector<item*>{};
    for (auto* it : media.contents.all_items_top()) {
        if (is_digital_file(*it)) { files.push_back(it); }
    }
    return files;
}

auto finish_scan(avatar& who) -> void {
    for (auto turn = 0; turn < 300 && !who.activity->is_null(); ++turn) {
        calendar::turn += 1_turns;
        who.moves = 100;
        who.activity->do_turn(who);
    }
    REQUIRE(who.activity->is_null());
}

// CDF: the key-script harness can lay out only a selector it is handed. select_scannable_text()
// builds its selector inside titled_filter_menu() (src/game_inventory.cpp:389-394), and in test
// mode ui_manager::redraw() is a no-op, so that selector never runs prepare_layout(): its cursor
// stays unset and CONFIRM returns nullptr whatever the menu holds. Build the same menu the action
// builds - titled_filter_menu() is a filter preset plus add_character_items() and
// add_nearby_items(radius) - and drive that selector. The scan reach itself is asserted through
// start_book_scan(), which validates the same tile set (src/item/book_scanning.cpp:40-75).
class scannable_text_preset : public inventory_selector_preset {
    public:
        auto is_shown( const item* loc ) const -> bool override {
            return is_scannable_text( *loc );
        }
};

auto pick_scannable_text( avatar& who ) -> item* {
    scannable_text_preset preset;
    inventory_pick_selector sel( who, preset );
    sel.set_display_stats( false );
    sel.add_character_items( who );
    sel.add_nearby_items( 1 ); // CDF: scan_reach in src/item/book_scanning.cpp
    return const_cast<item*>( run_screen( sel, key_script_keys{"RETURN"} ).returned );
}
} // namespace

TEST_CASE("scanning_preserves_distinct_snippets", "[digital_media][book_scanning]") {
    clear_all_state();
    const auto cleanup = on_out_of_scope([]() { clear_all_state(); });
    set_time(calendar::turn_zero + 12_hours);
    auto& who = get_avatar();
    clear_character(who);
    auto& reader = who.i_add(item::spawn("wrist_comp_budget"));
    reader.ammo_set(itype_id("battery"), 10);
    auto& text = who.i_add(item::spawn("test_scannable_snippet"));
    REQUIRE_FALSE(text.is_book());
    REQUIRE(text.is_armor());
    REQUIRE(is_scannable_text(text));
    auto identities = std::vector<std::string>{};
    auto scanned = std::vector<item*>{};
    for (const auto& id :
         {snippet_id("test_scannable_snippet_first"),
          snippet_id("test_scannable_snippet_second")}) {
        text.set_snippet(id);
        REQUIRE(start_book_scan(who, reader, text));
        finish_scan(who);
        CHECK_FALSE(is_digital_file(text));
        CHECK(text.snip_id == id);
        const auto files = digital_files_on(reader);
        REQUIRE(files.size() == scanned.size() + 1);
        // CDF: the file's pocket is not the last one (the battery sits in the MAGAZINE pocket),
        // so pick the new file by its snippet instead of by position
        auto* file = static_cast<item*>(nullptr);
        for (auto* candidate : files) {
            if (candidate->snip_id == id) { file = candidate; }
        }
        REQUIRE(file != nullptr);
        scanned.push_back(file);
        CHECK(file->snip_id == id);
        CHECK(file->info_string().find(SNIPPET.get_snippet_ref_by_id(id).translated())
              != std::string::npos);
        CHECK(file->typeId() == itype_id("digital_scanned_text"));
        CHECK(file->type_name() == "digital copy of " + text.type_name());
        CHECK_FALSE(file->is_armor());
        CHECK_FALSE(file->is_tool());
        CHECK(file->type->use_methods.empty());
        CHECK(file->volume() == 1_ml);
        CHECK(file->weight() == 0_gram);
        CHECK(digital_reader_for(who, *file));
        CHECK_FALSE(is_scannable_text(*file));
        CHECK_FALSE(start_book_scan(who, reader, text));
        identities.push_back(file->get_var("DIGITAL_FILE_ID"));
    }
    REQUIRE(identities.size() == 2);
    CHECK(identities.front() != identities.back());
    CHECK(digital_files_on(reader).size() == 2);

    auto& card = who.i_add(item::spawn("sd_memory_card"));
    REQUIRE(upload_digital_files(who, {.source = reader, .destination = card}) == 2);
    auto output = std::ostringstream{};
    auto writer = JsonOut(output);
    card.serialize(writer);
    auto input = std::istringstream(output.str());
    auto parser = JsonIn(input);
    const auto loaded = item::spawn(parser);
    auto& other_reader = who.i_add(item::spawn("wrist_comp_budget"));
    other_reader.ammo_set(itype_id("battery"), 10);
    REQUIRE(download_digital_files(other_reader, *loaded, who) == 2);
    const auto downloaded = digital_files_on(other_reader);
    REQUIRE(downloaded.size() == 2);
    // both scans arrived, whatever order the pocket keeps them in
    const auto has_snippet = [](const std::vector<item*>& files, const snippet_id& id) {
        for (const auto* entry : files) {
            if (entry->snip_id == id) { return true; }
        }
        return false;
    };
    CHECK(has_snippet(downloaded, snippet_id("test_scannable_snippet_first")));
    CHECK(has_snippet(downloaded, snippet_id("test_scannable_snippet_second")));
    CHECK(downloaded.front()->type_name() == "digital copy of " + text.type_name());
    CHECK(downloaded.front()->typeId() == itype_id("digital_scanned_text"));
    CHECK(download_digital_files(other_reader, *loaded, who) == 0);

    auto& rag = who.i_add(item::spawn("rag"));
    CHECK_FALSE(is_scannable_text(rag));
    CHECK_FALSE(start_book_scan(who, reader, rag));
}

TEST_CASE("scanned_books_can_be_read_in_darkness", "[digital_media][book_scanning][reading]") {
    clear_all_state();
    const auto cleanup = on_out_of_scope([]() { clear_all_state(); });
    set_time(calendar::turn_zero + 12_hours);
    auto& who = get_avatar();
    clear_character(who);
    auto& reader = who.i_add(item::spawn("wrist_comp_budget"));
    reader.ammo_set(itype_id("battery"), 10);
    auto& book = who.i_add(item::spawn("test_scannable_book"));
    REQUIRE(start_book_scan(who, reader, book));
    finish_scan(who);
    const auto files = digital_files_on(reader);
    REQUIRE(files.size() == 1);
    auto& file = *files.front();
    REQUIRE(is_digital_file(file));
    who.do_read(&file);
    REQUIRE(who.has_identified(file.typeId()));

    set_time(calendar::turn_zero);
    constexpr auto pos = tripoint_bub_ms(60, 60, 0);
    g->place_player(pos);
    auto& here = get_map();
    here.ter_set(pos, ter_id("t_floor"));
    here.furn_set(pos, furn_id("f_null"));
    here.ter_set(pos + tripoint_above, ter_id("t_flat_roof"));
    here.invalidate_map_cache(pos.z());
    here.build_map_cache(pos.z());
    here.update_visibility_cache(pos.z());
    REQUIRE_FALSE(character_funcs::can_see_fine_details(who));

    auto reasons = std::vector<std::string>{};
    SECTION("a powered screen allows normal speed reading and learning") {
        CHECK(who.get_book_reader(book, reasons) == nullptr);
        reasons.clear();
        CHECK(who.get_book_reader(file, reasons) == &who);
        CHECK(reasons.empty());
        const auto dark_reading_time = who.time_to_read(file, who);
        CHECK(who.time_to_read(book, who) > dark_reading_time);
        auto& lamp = who.i_add(item::spawn("atomic_lamp"));
        REQUIRE(character_funcs::can_see_fine_details(who));
        CHECK(who.time_to_read(file, who) == dark_reading_time);
        const auto removed_lamp = lamp.detach();
        REQUIRE_FALSE(character_funcs::can_see_fine_details(who));

        REQUIRE(who.read(&file, true));
        REQUIRE_FALSE(who.activity->is_null());
        for (auto turn = 0; turn < 300 && !who.activity->is_null(); ++turn) {
            calendar::turn += 1_turns;
            who.moves = 100;
            who.activity->do_turn(who);
        }
        CHECK(who.activity->is_null());
        CHECK(who.get_skill_level_object(skill_id("electronics")).exercise() > 0);
    }
    SECTION("a screen still requires power") {
        reader.ammo_unset();
        CHECK(who.get_book_reader(file, reasons) == nullptr);
    }
    SECTION("a screen cannot overcome blindness") {
        who.add_effect(efftype_id("blind"), 1_hours);
        REQUIRE(who.is_blind());
        CHECK(who.get_book_reader(file, reasons) == nullptr);
    }
    SECTION("a screen still requires reading glasses") {
        who.toggle_trait(trait_id("HYPEROPIC"));
        CHECK(who.get_book_reader(file, reasons) == nullptr);
    }
}

TEST_CASE(
    "scanned_books_retain_their_contents_as_transferable_digital_files",
    "[digital_media][book_scanning]") {
    clear_all_state();
    const auto cleanup = on_out_of_scope([]() { clear_all_state(); });
    set_time(calendar::turn_zero + 12_hours);
    auto& who = get_avatar();
    clear_character(who);
    auto& reader = who.i_add(item::spawn("wrist_comp_budget"));
    reader.ammo_set(itype_id("battery"), 10);
    auto& book = who.i_add(item::spawn("test_scannable_book"));
    REQUIRE_FALSE(is_digital_file(book));
    REQUIRE(start_book_scan(who, reader, book));
    // CDF: the reader's battery is a content item, so "nothing scanned yet" means no digital file
    CHECK(digital_files_on(reader).empty());
    finish_scan(who);
    auto files = digital_files_on(reader);
    REQUIRE(files.size() == 1);
    auto& file = *files.front();
    CHECK(is_digital_file(file));
    CHECK(file.typeId() == book.typeId());
    CHECK(file.type_name() != book.type_name());
    CHECK(file.weight() == 0_gram);
    CHECK(file.volume() == 1_ml);
    file.set_var("volume", 0);
    CHECK(file.volume() == 1_ml);
    CHECK(file.charges_per_volume(1_ml) == 1);
    CHECK(file.type->book->skill == book.type->book->skill);
    CHECK(reader.ammo_remaining() < 10);
    CHECK(who.has_item(book));
    CHECK_FALSE(is_digital_file(book));
    CHECK(book.volume() == 100_ml);
    CHECK_FALSE(start_book_scan(who, reader, book));
    CHECK(reader.can_contain(file).success());
    CHECK_FALSE(reader.can_contain(book).success());
    CHECK_FALSE(item::spawn("bottle_plastic")->can_contain(file).success());
    CHECK(digital_reader_for(who, file));

    who.set_skill_level(skill_id("electronics"), 1);
    const auto& recipe = recipe_id("test_soldering_iron_digital_media").obj();
    // CDF: P3 crafting queries take a temp_crafting_inventory
    auto inv = temp_crafting_inventory{};
    inv.add_item_loc(reader);
    CHECK(who.get_recipes_from_books(inv).contains(recipe));
    REQUIRE(who.read(&file, true));
    REQUIRE_FALSE(who.activity->is_null());
    who.cancel_activity();

    auto card = item::spawn("sd_memory_card");
    REQUIRE(
        upload_digital_files(who, {.source = reader, .destination = *card, .file = &file}) == 1);
    auto output = std::ostringstream{};
    auto writer = JsonOut(output);
    card->serialize(writer);
    auto input = std::istringstream(output.str());
    auto parser = JsonIn(input);
    const auto loaded = item::spawn(parser);
    auto& other_reader = who.i_add(item::spawn("test_digital_music_reader"));
    REQUIRE(download_digital_files(other_reader, *loaded, who) == 1);
    const auto imported = digital_files_on(other_reader);
    REQUIRE(imported.size() == 1);
    const auto& imported_file = *imported.front();
    CHECK(imported_file.typeId() == book.typeId());
    CHECK(imported_file.weight() == 0_gram);
    CHECK(imported_file.volume() == 1_ml);
    CHECK(imported_file.get_var("DIGITAL_FILE_ID") == file.get_var("DIGITAL_FILE_ID"));
    CHECK(digital_reader_for(who, imported_file));
}

TEST_CASE("book_scanning_stops_when_its_requirements_are_lost", "[digital_media][book_scanning]") {
    clear_all_state();
    const auto cleanup = on_out_of_scope([]() { clear_all_state(); });
    set_time(calendar::turn_zero + 12_hours);
    auto& who = get_avatar();
    clear_character(who);
    auto& reader = who.i_add(item::spawn("wrist_comp_budget"));
    reader.ammo_set(itype_id("battery"), 10);
    auto& book = who.i_add(item::spawn("test_scannable_book"));
    REQUIRE(start_book_scan(who, reader, book));

    auto removed = detached_ptr<item>{};
    SECTION("power is exhausted") {
        reader.ammo_unset();
        finish_scan(who);
    }
    SECTION("the reader is removed") {
        removed = reader.detach();
        finish_scan(who);
    }
    SECTION("the book is removed") {
        removed = book.detach();
        finish_scan(who);
    }
    SECTION("the player cancels") { who.cancel_activity(); }
    // CDF: no digital file was added; the reader's battery is still a content item
    CHECK(digital_files_on(reader).empty());
    CHECK_FALSE(is_digital_file(book));
}

// CDF: the scan menu used to offer carried books only (titled_filter_menu's radius default -1).
// DDA's eBook save scans a book the player can reach (game_inventory.cpp:1666-1692), so the menu
// now passes radius 1 and start_book_scan accepts a book on the player's tile or a neighbour.
TEST_CASE("book_scanning_offers_books_within_reach", "[digital_media][book_scanning]") {
    clear_all_state();
    const auto cleanup = on_out_of_scope([]() { clear_all_state(); });
    set_time(calendar::turn_zero + 12_hours);
    auto& who = get_avatar();
    clear_character(who);
    auto& reader = who.i_add(item::spawn("wrist_comp_budget"));
    reader.ammo_set(itype_id("battery"), 10);

    constexpr auto pos = tripoint_bub_ms(60, 60, 0);
    g->place_player(pos);
    auto& here = get_map();
    const auto ground = [&](const tripoint_bub_ms& at) {
        here.ter_set(at, ter_id("t_floor"));
        here.furn_set(at, furn_id("f_null"));
    };
    ground(pos);
    ground(pos + tripoint_east);
    ground(pos + tripoint_east + tripoint_east);
    here.invalidate_map_cache(pos.z());
    here.build_map_cache(pos.z());
    here.update_visibility_cache(pos.z());

    SECTION("a book on the player's tile is offered and scanned in place") {
        here.add_item(pos, item::spawn("test_scannable_book"));
        item& book = here.i_at(pos).only_item();
        item* picked = pick_scannable_text(who);
        REQUIRE(picked == &book);
        REQUIRE(start_book_scan(who, reader, book));
        finish_scan(who);
        const auto files = digital_files_on(reader);
        REQUIRE(files.size() == 1);
        CHECK(files.front()->typeId() == book.typeId());
        CHECK_FALSE(is_digital_file(book));
    }

    SECTION("a book on an adjacent tile is offered and scanned in place") {
        const auto adjacent = pos + tripoint_east;
        here.add_item(adjacent, item::spawn("test_scannable_book"));
        item& book = here.i_at(adjacent).only_item();
        item* picked = pick_scannable_text(who);
        REQUIRE(picked == &book);
        REQUIRE(start_book_scan(who, reader, book));
        finish_scan(who);
        CHECK(digital_files_on(reader).size() == 1);
        CHECK_FALSE(is_digital_file(book));
    }

    SECTION("a book two tiles away is neither offered nor a valid target") {
        const auto far = pos + tripoint_east + tripoint_east;
        here.add_item(far, item::spawn("test_scannable_book"));
        item& book = here.i_at(far).only_item();
        item* picked = pick_scannable_text(who);
        CHECK(picked == nullptr);
        CHECK_FALSE(start_book_scan(who, reader, book));
        CHECK(digital_files_on(reader).empty());
    }
}
