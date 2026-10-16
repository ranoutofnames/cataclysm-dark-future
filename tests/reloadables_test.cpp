// DDA source: tests/reloadables_test.cpp (DDA commit b4b62254ae2), P1f K5.
// TEST_CASE copied: available_reloadables.
// Translated for CDF: items are plain pointers (no item_location), Character::find_reloadables
// is character_funcs::find_reloadables( Character & ), i_add takes a detached item and returns
// the item in the inventory, has_parent / parent_item() are item::parent_item().
#include "avatar.h"
#include "catch/catch.hpp"
#include "character_functions.h"
#include "detached_ptr.h"
#include "item.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "ret_val.h"
#include "type_id.h"

#include <vector>

static const itype_id itype_backpack("backpack");
static const itype_id itype_m1911("m1911");

TEST_CASE("available_reloadables", "[ammo][pockets]") {
    clear_avatar();
    avatar& guy = get_avatar();
    // CDF: DEBUG_STORAGE makes a body storage that would take the gun; DDA has no such storage
    guy.unset_mutation(trait_id("DEBUG_STORAGE"));
    guy.update_body_storage();
    item& m1911 = guy.i_add(item::spawn(itype_m1911));
    std::vector<item*> reloadables = character_funcs::find_reloadables(guy);
    REQUIRE(reloadables.size() == 1);
    REQUIRE(reloadables.front() == &m1911);
    CHECK(reloadables.front()->obtain_cost(guy) == 0);
    m1911.detach();
    REQUIRE(guy.primary_weapon().is_null());
    item& pack = guy.i_add(item::spawn(itype_backpack));
    ret_val<void> res = pack.put_in(item::spawn(itype_m1911), pocket_type::CONTAINER);
    CHECK(guy.primary_weapon().typeId() == itype_backpack);
    REQUIRE(res.success());
    reloadables = character_funcs::find_reloadables(guy);
    REQUIRE(reloadables.size() == 1);
    CHECK(reloadables.front()->typeId() == itype_m1911);
    REQUIRE(reloadables.front()->parent_item() == &pack);
    // CDF: 325, not DDA's 322: BN's m1911 is 500 ml (25 moves), DDA's is 454 ml (22),
    // data/json/items/gun/45.json:136
    CHECK(reloadables.front()->obtain_cost(guy) == 325);
}
