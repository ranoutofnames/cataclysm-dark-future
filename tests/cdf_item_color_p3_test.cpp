#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "color.h"
#include "inventory.h"
#include "item.h"
#include "player_helpers.h"
#include "type_id.h"

// CDF: P3-5-5c. Ported from DDA tests/item_gun_mag_ammo_inventory_color_test.cpp:28-119
// (b4b62254ae2). The colour rule is already in CDF item.cpp color_in_inventory (gun branch,
// ammo branch, magazine branch); these cases are guards for it.

static const ammotype ammo_38("38");
static const ammotype ammo_9mm("9mm");

static const itype_id itype_38_special("38_special");
static const itype_id itype_38_speedloader("38_speedloader");
static const itype_id itype_9mm("9mm");
static const itype_id itype_backpack("backpack");
static const itype_id itype_glock_19("glock_19");
static const itype_id itype_glockmag("glockmag");
static const itype_id itype_sw_619("sw_619");

static void prepare_avatar(avatar& u) {
    // CDF: clear_avatar() already takes off the worn items, removes the primary weapon and
    // empties the inventory (player_helpers.cpp clear_character_impl), so DDA's clear_worn()
    // and remove_weapon() calls are not repeated here.
    clear_avatar();
    u.wear_item(item::spawn(itype_backpack), false);
}

TEST_CASE("full_gun_with_spare_mag_is_green", "[p3][color][item][gun][inventory][reload]") {
    avatar& u = get_avatar();
    prepare_avatar(u);

    // Put a fully-loaded gun in the inventory.
    // CDF: i_add returns item &, not item_location; the references are used before the next add.
    item& gun = u.i_add(item::spawn(itype_glock_19));
    item& mag_for_gun = u.i_add(item::spawn(itype_glockmag));
    // CDF: 9mm glockmag capacity is read from item data, not written here.
    const int mag_cap = mag_for_gun.ammo_capacity(ammo_9mm);
    item& ammo_for_gun = u.i_add(item::spawn(itype_9mm, calendar::turn, mag_cap));
    REQUIRE(mag_for_gun.reload(u, ammo_for_gun, mag_cap));
    REQUIRE(gun.reload(u, mag_for_gun, mag_cap));
    REQUIRE(gun.remaining_ammo_capacity() == 0);

    // Player also carries a second loaded magazine.
    item& spare_mag = u.i_add(item::spawn(itype_glockmag));
    item& ammo_for_spare = u.i_add(item::spawn(itype_9mm, calendar::turn, mag_cap));
    REQUIRE(spare_mag.reload(u, ammo_for_spare, mag_cap));

    // Gun is full, but the player still possesses a loaded magazine for it,
    // so the display color should signal "equipped" (green), not "missing
    // something" (light red).
    CHECK(gun.color_in_inventory(u) == c_green);
}

TEST_CASE("full_revolver_with_spare_ammo_is_green", "[p3][color][item][gun][inventory][reload]") {
    avatar& u = get_avatar();
    prepare_avatar(u);

    item& gun = u.i_add(item::spawn(itype_sw_619));
    // CDF: sw_619 has no integral cylinder. Its magazine default is 38_speedloader
    // (data/json/items/gun/38.json, "magazines"), so magazine_integral() is false
    // (item.cpp:9459-9466) and the full state is a loaded speedloader in the gun.
    item& loader = u.i_add(item::spawn(itype_38_speedloader));
    // CDF: capacity 7 from data/json/items/magazine/38.json:16.
    const int loader_cap = loader.ammo_capacity(ammo_38);
    REQUIRE(loader_cap > 0);

    // Three speedloads' worth of rounds: one to load the revolver, one to load a spare
    // speedloader, one left loose in the pack.
    item& ammo = u.i_add(item::spawn(itype_38_special, calendar::turn, loader_cap * 3));
    REQUIRE(loader.reload(u, ammo, loader_cap));
    REQUIRE(gun.reload(u, loader, loader_cap));
    REQUIRE(gun.remaining_ammo_capacity() == 0);

    // Spare loaded speedloader for the revolver; loose rounds are still in the pack.
    item& spare_loader = u.i_add(item::spawn(itype_38_speedloader));
    REQUIRE(spare_loader.reload(u, ammo, loader_cap));

    // Revolver is full, but the player still has a loaded speedloader and loose rounds
    // for it, so it should color green.
    CHECK(gun.color_in_inventory(u) == c_green);
}

TEST_CASE(
    "full_magazine_with_gun_and_spare_ammo_is_green",
    "[p3][color][item][magazine][inventory][reload]") {
    avatar& u = get_avatar();
    prepare_avatar(u);

    u.i_add(item::spawn(itype_glock_19));
    item& mag = u.i_add(item::spawn(itype_glockmag));
    const int mag_cap = mag.ammo_capacity(ammo_9mm);
    item& ammo = u.i_add(item::spawn(itype_9mm, calendar::turn, mag_cap * 2));
    REQUIRE(mag.reload(u, ammo, mag_cap));
    REQUIRE(mag.remaining_ammo_capacity() == 0);

    // Magazine is full, but the player still has a compatible gun and spare
    // loose rounds, so it should color green.
    CHECK(mag.color_in_inventory(u) == c_green);
}

TEST_CASE("empty_gun_alone_is_uncolored", "[p3][color][item][gun][inventory]") {
    avatar& u = get_avatar();
    prepare_avatar(u);

    item& gun = u.i_add(item::spawn(itype_glock_19));
    // No ammo, no magazine in inventory: neither green nor light red (will be default).
    const nc_color c = gun.color_in_inventory(u);
    CHECK(c != c_green);
    CHECK(c != c_light_red);
}

TEST_CASE("gun_with_ammo_but_no_mag_is_light_red", "[p3][color][item][gun][inventory]") {
    avatar& u = get_avatar();
    prepare_avatar(u);

    item& gun = u.i_add(item::spawn(itype_glock_19));
    u.i_add(item::spawn(itype_9mm, calendar::turn, 10));
    // Compatible loose ammo but no magazine -> light red.
    CHECK(gun.color_in_inventory(u) == c_light_red);
}

// CDF: P3-5-5c-C6, CDF-only. The no-argument overload (item.h:454) forwards to the avatar
// overload (item.cpp color_in_inventory() calls get_avatar()); it must give the same colour
// as the explicit-avatar form, both with nothing carried and with loose ammo carried.
TEST_CASE(
    "CDF: color_in_inventory without an argument forwards to the avatar",
    "[p3][color][item][gun][inventory]") {
    avatar& u = get_avatar();
    prepare_avatar(u);

    item& gun = u.i_add(item::spawn(itype_glock_19));
    CHECK(gun.color_in_inventory() == gun.color_in_inventory(u));

    u.i_add(item::spawn(itype_9mm, calendar::turn, 10));
    CHECK(gun.color_in_inventory() == gun.color_in_inventory(u));
    // CDF: same expectation as gun_with_ammo_but_no_mag_is_light_red.
    CHECK(gun.color_in_inventory() == c_light_red);
}
