#include "ablative_coverage.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "item.h"
#include "item_contents.h"
#include "item_factory.h"
#include "item_group.h"
#include "itype.h"
#include "state_helpers.h"

#include <string>
#include <vector>

namespace {

// How many plates of @p id sit in the plate slots of @p carrier.
auto plates_of(const item &carrier, const std::string &id) -> int {
    int found = 0;
    for (const item *plate : ablative::plates(carrier)) {
        if (plate->typeId() == itype_id(id)) {
            ++found;
        }
    }
    return found;
}

const itype_id old_carrier("test_old_plate_carrier");

} // namespace

TEST_CASE("a migration spawns its replacement with counted contents", "[item][migration]") {
    clear_all_state();

    REQUIRE(item_controller->migrate_id(old_carrier) == itype_id("test_plate_carrier"));

    SECTION("spawn_migrated applies the replacement and the counted contents") {
        detached_ptr<item> carrier = item_controller->spawn_migrated(old_carrier, calendar::turn);
        REQUIRE(carrier);
        CHECK(carrier->typeId() == itype_id("test_plate_carrier"));
        // a string and an object entry of the same id add up
        CHECK(plates_of(*carrier, "test_slice_plate_large") == 2);
        CHECK(plates_of(*carrier, "test_slice_plate_medium") == 2);
        // and the router put them into plate slots
        CHECK(ablative::plates(static_cast<const item &>(*carrier)).size() == 4);
    }
    SECTION("applying the migration again never doubles the contents") {
        detached_ptr<item> carrier = item_controller->spawn_migrated(old_carrier, calendar::turn);
        item_controller->migrate_item(old_carrier, *carrier);
        item_controller->migrate_item(old_carrier, *carrier);
        CHECK(plates_of(*carrier, "test_slice_plate_large") == 2);
        CHECK(plates_of(*carrier, "test_slice_plate_medium") == 2);
    }
    SECTION("an item that already holds one plate gets the missing one, not two") {
        detached_ptr<item> carrier = item::spawn(itype_id("test_plate_carrier"));
        carrier->put_in(item::spawn(itype_id("test_slice_plate_large")));
        REQUIRE(plates_of(*carrier, "test_slice_plate_large") == 1);
        item_controller->migrate_item(old_carrier, *carrier);
        CHECK(plates_of(*carrier, "test_slice_plate_large") == 2);
        CHECK(plates_of(*carrier, "test_slice_plate_medium") == 2);
    }
    SECTION("an id without a migration spawns as item::spawn does") {
        detached_ptr<item> carrier = item_controller->spawn_migrated(itype_id("test_plate_carrier"),
                                     calendar::turn);
        REQUIRE(carrier);
        CHECK(carrier->typeId() == itype_id("test_plate_carrier"));
        CHECK(ablative::plates(static_cast<const item &>(*carrier)).empty());
    }
}

TEST_CASE("itemgroups spawn a migrated id with the migration's contents", "[item][migration]") {
    clear_all_state();

    SECTION("a plain entry") {
        std::vector<detached_ptr<item>> spawned =
            item_group::items_from(item_group_id("test_old_carrier_group"));
        REQUIRE(spawned.size() == 1);
        CHECK(spawned.front()->typeId() == itype_id("test_plate_carrier"));
        CHECK(plates_of(*spawned.front(), "test_slice_plate_large") == 2);
        CHECK(plates_of(*spawned.front(), "test_slice_plate_medium") == 2);
    }
    SECTION("the entry's own contents are added after the migration's") {
        std::vector<detached_ptr<item>> spawned =
            item_group::items_from(item_group_id("test_old_carrier_with_protector_group"));
        REQUIRE(spawned.size() == 1);
        CHECK(plates_of(*spawned.front(), "test_slice_plate_large") == 2);
        CHECK(plates_of(*spawned.front(), "test_slice_plate_medium") == 2);
        CHECK(plates_of(*spawned.front(), "test_plate_protector") == 1);
    }
}
