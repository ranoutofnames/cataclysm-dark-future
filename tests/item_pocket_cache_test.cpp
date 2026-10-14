#include "calendar.h"
#include "catch/catch.hpp"
#include "flag.h"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"
#include "units.h"

#include <chrono>
#include <vector>

namespace {

// The pocket's cached sums must always equal a plain recount.
void check_sums(const item& container) {
    for (const item_pocket& pocket : container.contents.get_pockets()) {
        units::volume vol = 0_ml;
        units::mass wt = 0_gram;
        for (const item* it : pocket.items()) {
            vol += it->volume();
            wt += it->weight();
        }
        CHECK(pocket.contains_volume() == vol);
        CHECK(pocket.contains_weight() == wt);
    }
}

// the pocket that holds something, else the first one that is not worn storage
auto live_pocket(const item& container) -> const item_pocket& {
    for (const item_pocket& pocket : container.contents.get_pockets()) {
        if (!pocket.empty()) { return pocket; }
    }
    for (const item_pocket& pocket : container.contents.get_pockets()) {
        if (!pocket.is_carry()) { return pocket; }
    }
    return container.contents.get_pockets().front();
}

auto pocket_sizes(const item& container) -> std::vector<size_t> {
    std::vector<size_t> res;
    for (const item_pocket& pocket : container.contents.get_pockets()) {
        res.push_back(pocket.size());
    }
    return res;
}

} // namespace

TEST_CASE("pocket size cache follows every way of changing the contents", "[item][pocket][item_contents]") {
    detached_ptr<item> bag = item::spawn(itype_id("test_backpack"));
    check_sums(*bag);

    SECTION("insert and remove") {
        std::vector<item*> added;
        for (int i = 0; i < 5; i++) {
            detached_ptr<item> cube = item::spawn(itype_id("test_1kg_cube"));
            added.push_back(&*cube);
            bag->put_in(std::move(cube));
            check_sums(*bag);
        }
        REQUIRE(bag->contents.num_item_stacks() == 5);
        // through the visitable interface, the way crafting and unloading remove items
        detached_ptr<item> removed = bag->remove_item(*added[1]);
        check_sums(*bag);
        // through the contents API
        detached_ptr<item> removed2 = bag->contents.remove_top(added[2]);
        check_sums(*bag);
        // clearing
        std::vector<detached_ptr<item>> all = bag->contents.clear_items();
        CHECK(all.size() == 3);
        check_sums(*bag);
        CHECK(live_pocket(*bag).contains_weight() == 0_gram);
    }

    SECTION("a soft container grows with what it holds") {
        detached_ptr<item> inner = item::spawn(itype_id("test_backpack"));
        item* inner_ptr = &*inner;
        bag->put_in(std::move(inner));
        check_sums(*bag);
        const units::volume empty_inner = inner_ptr->volume();
        for (int i = 0; i < 10; i++) {
            inner_ptr->put_in(item::spawn(itype_id("test_1kg_cube")));
            check_sums(*bag);
        }
        CHECK(inner_ptr->volume() > empty_inner);
        // and shrinks again
        inner_ptr->contents.clear_items();
        check_sums(*bag);
    }

    SECTION("size flags and conversion of a contained item") {
        detached_ptr<item> cube = item::spawn(itype_id("test_1kg_cube"));
        item* cube_ptr = &*cube;
        bag->put_in(std::move(cube));
        check_sums(*bag);
        const units::mass before = live_pocket(*bag).contains_weight();
        cube_ptr->set_flag(flag_REDUCED_WEIGHT);
        check_sums(*bag);
        CHECK(live_pocket(*bag).contains_weight() < before);
        cube_ptr->unset_flag(flag_REDUCED_WEIGHT);
        check_sums(*bag);
        CHECK(live_pocket(*bag).contains_weight() == before);
        cube_ptr->convert(itype_id("test_rock"));
        check_sums(*bag);
    }

    SECTION("charges of contained items") {
        detached_ptr<item> water = item::spawn(itype_id("water_clean"), calendar::turn, 2);
        item* water_ptr = &*water;
        bag->put_in(std::move(water));
        check_sums(*bag);
        water_ptr->charges = 10;
        check_sums(*bag);
        water_ptr->charges = 1;
        check_sums(*bag);
    }

    SECTION("a copy has its own cache") {
        for (int i = 0; i < 3; i++) {
            bag->put_in(item::spawn(itype_id("test_1kg_cube")));
        }
        check_sums(*bag);
        detached_ptr<item> copy = item::spawn(*bag);
        check_sums(*copy);
        copy->contents.clear_items();
        check_sums(*copy);
        check_sums(*bag);
        CHECK(bag->contents.num_item_stacks() == 3);
    }
}

TEST_CASE("stacks_with gives up on absurdly nested containers", "[item][pocket][item_contents]") {
    const auto nested = [](int depth) {
        detached_ptr<item> outer = item::spawn(itype_id("test_backpack"));
        item* current = &*outer;
        for (int i = 0; i < depth; i++) {
            detached_ptr<item> next = item::spawn(itype_id("test_backpack"));
            item* next_ptr = &*next;
            current->put_in(std::move(next));
            current = next_ptr;
        }
        return outer;
    };
    SECTION("shallow containers stack") {
        detached_ptr<item> a = nested(3);
        detached_ptr<item> b = nested(3);
        CHECK(a->stacks_with(*b));
    }
    SECTION("deep containers do not, and do not recurse forever") {
        detached_ptr<item> a = nested(item::max_stack_depth + 5);
        detached_ptr<item> b = nested(item::max_stack_depth + 5);
        CHECK_FALSE(a->stacks_with(*b));
    }
}

TEST_CASE("insert_copies matches repeated insert_item", "[item][pocket][item_contents]") {
    const auto fill = [](const char* container, const char* what, int n, bool bulk) {
        detached_ptr<item> box = item::spawn(itype_id(container));
        if (bulk) {
            const detached_ptr<item> proto = item::spawn(itype_id(what));
            box->put_in_copies(*proto, n);
        } else {
            for (int i = 0; i < n; i++) {
                box->put_in(item::spawn(itype_id(what)));
            }
        }
        return box;
    };
    const auto compare = [&](const char* container, const char* what, int n) {
        INFO(container << " " << what << " " << n);
        detached_ptr<item> bulk = fill(container, what, n, true);
        detached_ptr<item> loop = fill(container, what, n, false);
        CHECK(pocket_sizes(*bulk) == pocket_sizes(*loop));
        CHECK(bulk->contents.num_item_stacks() == loop->contents.num_item_stacks());
        CHECK(bulk->volume() == loop->volume());
        CHECK(bulk->weight() == loop->weight());
        check_sums(*bulk);
    };
    SECTION("all fit") {
        compare("test_backpack", "test_1kg_cube", 20);
    }
    SECTION("the pocket fills up, the rest goes to migration") {
        compare("test_box", "test_1kg_cube", 150);
    }
    SECTION("nothing fits") {
        compare("test_box_small", "test_rock", 4);
    }
    SECTION("count by charges items merge") {
        detached_ptr<item> bag = item::spawn(itype_id("test_backpack"));
        const detached_ptr<item> proto = item::spawn(itype_id("water_clean"), calendar::turn, 2);
        bag->put_in_copies(*proto, 3);
        detached_ptr<item> ref = item::spawn(itype_id("test_backpack"));
        for (int i = 0; i < 3; i++) {
            ref->put_in(item::spawn(itype_id("water_clean"), calendar::turn, 2));
        }
        CHECK(bag->contents.num_item_stacks() == ref->contents.num_item_stacks());
        CHECK(bag->weight() == ref->weight());
    }
}

TEST_CASE("bench_pocket_fill", "[.][benchmark][pocket]") {
    constexpr int n = 10000;
    const auto time_it = [](auto&& fn) {
        const auto start = std::chrono::steady_clock::now();
        fn();
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };
    detached_ptr<item> loop_box = item::spawn(itype_id("test_backpack"));
    const double loop_ms = time_it([&] {
        for (int i = 0; i < n; i++) {
            loop_box->put_in(item::spawn(itype_id("test_1kg_cube")));
        }
    });
    detached_ptr<item> bulk_box = item::spawn(itype_id("test_backpack"));
    const double bulk_ms = time_it([&] {
        const detached_ptr<item> proto = item::spawn(itype_id("test_1kg_cube"));
        bulk_box->put_in_copies(*proto, n);
    });
    detached_ptr<item> query_box = item::spawn(itype_id("test_backpack"));
    {
        const detached_ptr<item> proto = item::spawn(itype_id("test_1kg_cube"));
        query_box->put_in_copies(*proto, 1000);
    }
    const detached_ptr<item> probe = item::spawn(itype_id("test_1kg_cube"));
    const double query_ms = time_it([&] {
        for (int i = 0; i < 100000; i++) {
            query_box->contents.can_contain(*probe);
        }
    });
    WARN("fill " << n << " items one by one: " << loop_ms << " ms; insert_copies: " << bulk_ms
         << " ms; 100000 can_contain on 1000 items: " << query_ms << " ms");
    CHECK(loop_box->contents.num_item_stacks() == bulk_box->contents.num_item_stacks());
}

TEST_CASE("bench_container_fill", "[.][benchmark][pocket]") {
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 2000; i++) {
        detached_ptr<item> bag = item::spawn(itype_id("test_backpack"));
        const detached_ptr<item> proto = item::spawn(itype_id("test_1kg_cube"));
        bag->put_in_copies(*proto, 50);
    }
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - start ).count();
    WARN("2000 bags of 50 items: " << ms << " ms");
}
