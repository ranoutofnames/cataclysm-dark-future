#include "bodypart.h"
#include "catch/catch.hpp"
#include "item.h"
#include "itype.h"
#include "itype_slots/islot_armor.h"

// CDF: per-portion rigidity (DDA item_factory.cpp:1138-1148); materials covering 40% or less
// of a portion do not count, covered_by_mat defaults to 100.
TEST_CASE("armor_portion_rigidity_from_materials", "[armor][rigid]") {
    CHECK(islot_armor::test_threshold == 40);

    const item jacket("jacket_leather_mod");
    REQUIRE(jacket.type->armor);
    for (const armor_portion_data& portion : jacket.type->armor->data) {
        REQUIRE(!portion.materials.empty());
        for (const part_material& m : portion.materials) { CHECK(m.cover == 100); }
        CHECK(portion.rigid);
    }

    const item shirt("tshirt");
    REQUIRE(shirt.type->armor);
    for (const armor_portion_data& portion : shirt.type->armor->data) {
        CHECK_FALSE(portion.rigid);
    }
    CHECK_FALSE(shirt.is_rigid());
}
