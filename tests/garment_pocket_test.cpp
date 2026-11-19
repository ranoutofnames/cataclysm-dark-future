#include "calendar.h"
#include "catch/catch.hpp"
#include "item.h"
#include "item_contents.h"
#include "item_factory.h"
#include "item_pocket.h"
#include "itype.h"
#include "pocket_data.h"
#include "units.h"
#include "itype_slots/islot_armor.h"

#include <algorithm>
#include <vector>

namespace {

auto carry_pockets(const itype& t) -> std::vector<const pocket_data*> {
    std::vector<const pocket_data*> ret;
    for (const pocket_data& pd : t.pockets) {
        if (pd.carry) { ret.push_back(&pd); }
    }
    return ret;
}

auto container_volume(const std::vector<const pocket_data*>& pockets) -> units::volume {
    units::volume total = 0_ml;
    for (const pocket_data* pd : pockets) {
        if (!pd->holster) { total += pd->raw_volume_capacity; }
    }
    return total;
}

} // namespace

TEST_CASE("garment layout overlays give DDA's pockets verbatim", "[item][pocket][garment]") {
    SECTION("jeans have DDA's four pockets, 4.66 L") {
        const itype& t = *itype_id("jeans");
        const auto pockets = carry_pockets(t);
        CHECK(pockets.size() == 4);
        CHECK(container_volume(pockets) == 4660_ml);
        CHECK(t.armor->storage == 500_ml);
        detached_ptr<item> jeans = item::spawn(itype_id("jeans"));
        // storage is the sum of the worn pockets since P1c step 20
        CHECK(jeans->get_storage() == 4660_ml);
        CHECK(jeans->get_pocket_storage_sum() == 4660_ml);
    }
    SECTION("rucksack has the main bag plus straps") {
        const itype& t = *itype_id("rucksack");
        const auto pockets = carry_pockets(t);
        CHECK(pockets.size() > 1);
        CHECK(std::ranges::any_of(t.pockets, [](const pocket_data& pd) { return pd.holster; }));
        CHECK(container_volume(pockets) == 54250_ml);
        detached_ptr<item> bag = item::spawn(itype_id("rucksack"));
        CHECK(bag->get_storage() == bag->get_pocket_storage_sum());
        CHECK(bag->get_pocket_storage_sum() > 0_ml);
        CHECK(std::ranges::all_of(pockets, [](const pocket_data* pd) {
            return pd->type == pocket_type::CONTAINER && !pd->ablative;
        }));
    }
}

TEST_CASE("garments without a layout get pockets derived from storage", "[item][pocket][garment]") {
    // rm13_armor's DDA layout has no container pocket, so it is derived from its 8 L storage
    const itype& t = *itype_id("rm13_armor");
    const auto pockets = carry_pockets(t);
    REQUIRE(pockets.size() == 1);
    CHECK(pockets.front()->raw_volume_capacity == t.armor->storage);
    CHECK(pockets.front()->max_contains_weight == pocket_data::max_weight_for_container);
    CHECK(pockets.front()->moves == 100);
    CHECK(pockets.front()->rigid == t.rigid);

    // every other garment with storage has pockets, and a derived one totals its storage
    for (const itype* it : item_controller->all()) {
        if (!it->armor || it->armor->storage <= 0_ml || it->explicit_pockets) {
            continue;
        }
        const auto carried = carry_pockets(*it);
        INFO(it->get_id().str());
        REQUIRE(!carried.empty());
        CHECK(std::ranges::all_of(carried, [](const pocket_data* pd) {
            return pd->type == pocket_type::CONTAINER && !pd->ablative;
        }));
    }
}

TEST_CASE("worn storage pockets are live", "[item][pocket][garment]") {
    detached_ptr<item> jeans = item::spawn(itype_id("jeans"));
    detached_ptr<item> bolt = item::spawn(itype_id("pebble"), calendar::turn, 1);
    CHECK(jeans->contents.can_contain(*bolt).success());
    CHECK(jeans->contents.best_pocket(*bolt, nullptr).pocket != nullptr);
    jeans->put_in(std::move(bolt));
    bool held = false;
    for (const item_pocket& pocket : jeans->contents.get_pockets()) {
        if (pocket.is_carry() && !pocket.empty()) {
            held = true;
        }
    }
    CHECK(held);
}
