#include "../src/map/map.h"
#include "activity_handlers.h"
#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "clothing_link.h"
#include "drop_token.h"
#include "item.h"
#include "locations.h"
#include "map/map_selector.h"
#include "map_helpers.h"
#include "pickup.h"
#include "pickup_token.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "units_mass.h"
#include "units_volume.h"

#include <algorithm>
#include <climits>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

class testing_stack: public item_stack {
public:
    testing_stack(): item_stack(new location_vector<item>(new fake_item_location())) {}
    ~testing_stack() override { delete (items); }

    auto insert_with_return(detached_ptr<item>&& newitem) -> item_stack::iterator {
        return items->insert(items->end(), std::move(newitem));
    }
    void insert(detached_ptr<item>&& newitem) override { insert_with_return(std::move(newitem)); }
    auto remove(item* to_remove) -> detached_ptr<item> override {
        for (auto it = items->begin(); it != items->end(); it++) {
            if (*it == to_remove) {
                detached_ptr<item> out;
                erase(it, &out);
                return out;
            }
        }
        return detached_ptr<item>();
    }
    auto erase(const_iterator it, detached_ptr<item>* out = nullptr) -> iterator override {
        return items->erase(it, out);
    }
    auto count_limit() const -> int override { return INT_MAX; }
    auto max_volume() const -> units::volume override {
        return units::from_milliliter(std::numeric_limits<units::volume::value_type>::max());
    }
};

TEST_CASE("full backpack pickup", "[drop_token]") {
    clear_all_state();
    constexpr tripoint_bub_ms pos = tripoint_bub_ms(60, 60, 0);
    avatar& dummy = get_avatar();
    item& an_item = *item::spawn_temporary("bottle_glass");
    item& backpack = *item::spawn_temporary("backpack");
    item& duffel_bag = *item::spawn_temporary("duffelbag");
    dummy.set_moves(100);

    dummy.worn.push_back(item::spawn(duffel_bag));

    map& here = get_map();
    drop_token_provider& token_provider = drop_token::get_provider();
    GIVEN("a backpack full of items lying on a ground tile") {
        detached_ptr<item> bp = item::spawn(backpack);
        item& parent = *bp;
        here.add_item(pos, std::move(bp));
        *parent.drop_token = token_provider.make_next(calendar::turn);
        for (units::volume remaining_storage = backpack.get_storage();
             remaining_storage > an_item.volume(); remaining_storage -= an_item.volume()) {
            detached_ptr<item> det = item::spawn(an_item);
            item& child = *det;
            here.add_item(pos, std::move(det));
            *child.drop_token = token_provider.make_next(calendar::turn);
            child.drop_token->parent_number = parent.drop_token->drop_number;
        }

        map_stack stack = here.i_at(pos);
        REQUIRE(stack.size() > 2);
        for (const item* const& it : stack) {
            REQUIRE(it->drop_token->turn == parent.drop_token->turn);
        }

        WHEN("a character with enough volume and weight capacity tries to pick it up") {
            units::mass weight_sum = std::accumulate(
                stack.begin(), stack.end(), 0_gram,
                [](units::mass acc, const item* const& it) { return acc + it->weight(); });
            units::volume volume_sum = std::accumulate(
                stack.begin(), stack.end(), 0_ml,
                [](units::volume acc, const item* const& it) { return acc + it->volume(); });
            REQUIRE(dummy.weight_capacity() >= weight_sum + duffel_bag.weight());
            REQUIRE(dummy.volume_capacity() >= volume_sum);

            std::vector<item*> target_locations;
            std::vector<int> quantities;
            map_cursor mc(pos);
            for (item*& it : stack) {
                target_locations.push_back(it);
                quantities.push_back(0);
            }

            std::vector<pickup::pick_drop_selection> targets =
                pickup::optimize_pickup(target_locations, quantities);

            THEN("the backpack is a pickup parent and all the contents are children") {
                CHECK(targets.front().target->typeId() == backpack.typeId());
                CHECK(targets.front().children.size() + 1 == target_locations.size());
            }

            int moves_before = dummy.get_moves();
            REQUIRE(moves_before > 0);
            // TODO: Pickup doesn't need to be player-centric
            bool did_it = pickup::do_pickup(targets, true);

            THEN("it succeeds and costs only as much moves as picking the backpack itself would") {
                CHECK(did_it);
                CHECK(dummy.get_moves() == moves_before - 100);
                bool no_items_left = mc.items_with([](const item&) { return true; }).empty();
                CHECK(no_items_left);
                CHECK(dummy.carried_volume() == volume_sum);
            }
        }
    }
}

TEST_CASE("clothing links require available storage", "[activity][drop_token]") {
    clear_all_state();
    auto dummy = avatar{};
    dummy.set_body();
    dummy.set_mutation(trait_id("DEBUG_STORAGE")); // a bare avatar has no pockets
    auto& pack = *item::spawn_temporary("test_pants_fur");
    auto& other_pack = *item::spawn_temporary("test_pants_fur");
    auto& exact_fit = *item::spawn_temporary("test_amputator");
    auto& small = *item::spawn_temporary("test_1kg_cube");
    REQUIRE(pack.get_storage() == exact_fit.volume());
    pack.set_var("DROP_WITH_CLOTHING_ID", 1);
    other_pack.set_var("DROP_WITH_CLOTHING_ID", 2);

    CHECK(can_link_to_clothing(dummy, exact_fit, pack));
    CHECK_FALSE(can_link_to_clothing(dummy, pack, pack));
    CHECK_FALSE(can_link_to_clothing(dummy, pack, other_pack));
    CHECK_FALSE(can_link_to_clothing(dummy, small, exact_fit));

    auto linked = item::spawn("test_amputator");
    linked->set_var("DROP_WITH_CLOTHING_TARGET", 1);
    auto* linked_ptr = linked.get();
    dummy.i_add(std::move(linked));

    CHECK_FALSE(can_link_to_clothing(dummy, small, pack));
    CHECK(can_link_to_clothing(dummy, small, other_pack));
    CHECK(can_link_to_clothing(dummy, *linked_ptr, pack));
    CHECK(can_link_to_clothing(dummy, *linked_ptr, other_pack));
}

TEST_CASE("clothing-linked items drop with their assigned clothing", "[activity][drop_token]") {
    clear_all_state();
    avatar dummy;
    auto& backpack = *item::spawn_temporary("backpack");
    auto& duffel_bag = *item::spawn_temporary("duffelbag");
    auto& linked_item_type = *item::spawn_temporary("bottle_glass");

    REQUIRE(!dummy.wear_item(item::spawn(backpack), false));
    REQUIRE(!dummy.wear_item(item::spawn(duffel_bag), false));

    const auto backpack_iter = std::ranges::find_if(dummy.worn, [](const auto* worn) {
        return worn->typeId() == itype_id("backpack");
    });
    const auto duffel_iter = std::ranges::find_if(dummy.worn, [](const auto* worn) {
        return worn->typeId() == itype_id("duffelbag");
    });
    REQUIRE(backpack_iter != dummy.worn.end());
    REQUIRE(duffel_iter != dummy.worn.end());
    auto* backpack_item = *backpack_iter;
    auto* duffel_item = *duffel_iter;
    backpack_item->set_var("DROP_WITH_CLOTHING_ID", 1);
    duffel_item->set_var("DROP_WITH_CLOTHING_ID", 2);

    auto linked_item = item::spawn(linked_item_type);
    linked_item->set_var("DROP_WITH_CLOTHING_TARGET", 2);
    linked_item->set_var("DROP_WITH_CLOTHING_NAME", duffel_item->tname());
    dummy.i_add(std::move(linked_item));

    drop_locations drop;
    drop.emplace_back(*backpack_item, 1);
    drop.emplace_back(*duffel_item, 1);
    auto drop_list = pickup::reorder_for_dropping(dummy, drop);

    const auto linked_position = std::ranges::find_if(drop_list, [](const auto& ait) {
        return ait.loc->has_var("DROP_WITH_CLOTHING_TARGET");
    });
    REQUIRE(linked_position != drop_list.end());
    REQUIRE(linked_position != drop_list.begin());
    CHECK(std::prev(linked_position)->loc->get_var("DROP_WITH_CLOTHING_ID", 0) == 2);
    CHECK(linked_position->consumed_moves == 0);

    dummy.moves = 1000;
    auto dropped_items = pickup::obtain_and_tokenize_items(dummy, drop_list);
    const auto dropped_duffel_iter = std::ranges::find_if(dropped_items, [](const auto& it) {
        return it->get_var("DROP_WITH_CLOTHING_ID", 0) == 2;
    });
    const auto dropped_linked_iter = std::ranges::find_if(dropped_items, [](const auto& it) {
        return it->has_var("DROP_WITH_CLOTHING_TARGET");
    });
    REQUIRE(dropped_duffel_iter != dropped_items.end());
    REQUIRE(dropped_linked_iter != dropped_items.end());
    const auto* dropped_duffel = &**dropped_duffel_iter;
    const auto* dropped_linked_item = &**dropped_linked_iter;
    CHECK(dropped_linked_item->drop_token->is_child_of(*dropped_duffel->drop_token));
}

static auto iterators_in_vector(item_stack& the_stack) -> std::vector<item_stack::iterator> {
    std::vector<item_stack::iterator> unstacked;

    for (auto iter = the_stack.begin(); iter != the_stack.end(); iter++) {
        unstacked.emplace_back(iter);
    }

    return unstacked;
}

TEST_CASE("pickup_ui_stacking", "[activity][drop_token]") {
    clear_all_state();
    item& an_item = *item::spawn_temporary("bottle_glass");
    item& backpack = *item::spawn_temporary("backpack");
    item& duffel_bag = *item::spawn_temporary("duffelbag");
    const size_t per_backpack = backpack.get_total_capacity() / an_item.volume();

    drop_token_provider& token_provider = drop_token::get_provider();

    GIVEN("A mix of items on the ground, with some of them being in a backpack") {
        testing_stack the_stack;
        the_stack.insert(item::spawn(an_item));
        auto insert_parent_iter = the_stack.insert_with_return(item::spawn(backpack));
        item_drop_token parent_token = token_provider.make_next(calendar::turn);
        *(*insert_parent_iter)->drop_token = parent_token;
        for (size_t i = 0; i < per_backpack; i++) {
            auto child_iter = the_stack.insert_with_return(item::spawn(an_item));
            *(*child_iter)->drop_token = token_provider.make_next(calendar::turn);
            (*child_iter)->drop_token->parent_number = parent_token.drop_number;
        }

        the_stack.insert(item::spawn(backpack));
        the_stack.insert(item::spawn(duffel_bag));
        the_stack.insert(item::spawn(backpack));

        // Result should be sorted, so scrambling it a bit should help
        std::reverse(the_stack.begin(), the_stack.end());

        std::vector<item_stack::iterator> unstacked = iterators_in_vector(the_stack);

        auto post_scramble_parent_iter = std::find_if(
            unstacked.begin(), unstacked.end(), [&parent_token](const item_stack::iterator& it) {
                return *(*it)->drop_token == parent_token;
            });
        REQUIRE(post_scramble_parent_iter != unstacked.end());
        auto parent_iter = *post_scramble_parent_iter;
        WHEN("The items are restacked in preparation for pickup display") {
            std::vector<pickup::stacked_items> stacked_nonflat = pickup::stack_for_pickup_ui(
                unstacked);
            std::vector<std::list<item_stack::iterator>> stacked = pickup::flatten(stacked_nonflat);
            THEN("Backpack with children doesn't stack with empty ones") {
                std::list<item_stack::iterator> list_with_just_parent = {parent_iter};
                auto stacked_parent_iter =
                    std::find(stacked.begin(), stacked.end(), list_with_just_parent);
                REQUIRE(stacked_parent_iter != stacked.end());
                CHECK(stacked_parent_iter->size() == 1);
            }

            THEN("Backpack is immediately followed by all of its children") {
                std::list<item_stack::iterator> list_with_just_parent = {parent_iter};
                auto stacked_parent_iter =
                    std::find(stacked.begin(), stacked.end(), list_with_just_parent);
                REQUIRE(stacked_parent_iter != stacked.end());
                CHECK(stacked_parent_iter->size() == 1);
                CHECK((*stacked_parent_iter->front())->typeId() == backpack.typeId());
                auto child_stack_iter = std::next(stacked_parent_iter);
                REQUIRE(child_stack_iter != stacked.end());
                CHECK((*child_stack_iter->front())->typeId() == an_item.typeId());
                CHECK(child_stack_iter->size() == per_backpack);
            }
        }
    }

    GIVEN("Two backpacks with set tokens, but no contents") {
        testing_stack the_stack;
        auto first_backpack_iter = the_stack.insert_with_return(item::spawn(backpack));
        *(*first_backpack_iter)->drop_token = token_provider.make_next(calendar::turn);
        auto second_backpack_iter = the_stack.insert_with_return(item::spawn(backpack));
        *(*second_backpack_iter)->drop_token = token_provider.make_next(calendar::turn);

        std::vector<item_stack::iterator> unstacked = iterators_in_vector(the_stack);

        WHEN("The items are restacked in preparation for pickup display") {
            std::vector<pickup::stacked_items> stacked_nonflat = pickup::stack_for_pickup_ui(
                unstacked);
            std::vector<std::list<item_stack::iterator>> stacked = pickup::flatten(stacked_nonflat);
            THEN("The two backpacks stack") { CHECK(stacked.size() == 1); }
        }
    }

    GIVEN("Multiple identical items with tokens with different turns and parents set, but with no "
          "parents on the item stack") {
        testing_stack the_stack;

        item_drop_token first_parent_token = token_provider.make_next(calendar::turn);
        for (size_t i = 0; i < per_backpack; i++) {
            auto child_iter = the_stack.insert_with_return(item::spawn(an_item));
            *(*child_iter)->drop_token = token_provider.make_next(calendar::turn);
            (*child_iter)->drop_token->parent_number = first_parent_token.drop_number;
        }

        item_drop_token second_parent_token = token_provider.make_next(calendar::turn);
        for (size_t i = 0; i < per_backpack; i++) {
            auto child_iter = the_stack.insert_with_return(item::spawn(an_item));
            *(*child_iter)->drop_token = token_provider.make_next(calendar::turn);
            (*child_iter)->drop_token->parent_number = second_parent_token.drop_number;
        }

        std::vector<item_stack::iterator> unstacked = iterators_in_vector(the_stack);
        WHEN("The items are restacked in preparation for pickup display") {
            std::vector<pickup::stacked_items> stacked_nonflat = pickup::stack_for_pickup_ui(
                unstacked);
            std::vector<std::list<item_stack::iterator>> stacked = pickup::flatten(stacked_nonflat);
            THEN("All of those items stack") { CHECK(stacked.size() == 1); }
        }
    }
}
