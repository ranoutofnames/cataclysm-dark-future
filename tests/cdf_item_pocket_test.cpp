#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "debug.h"
#include "item.h"
#include "item_contents.h"
#include "item_group.h"
#include "item_pocket.h"
#include "iteminfo_query.h"
#include "itype.h"
#include "json.h"
#include "map/map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "ret_val.h"
#include "test_data.h"
#include "units.h"

#include <sstream>
#include <string>

namespace {

auto load_pocket(const std::string& json) -> pocket_data {
    std::istringstream buffer(json);
    JsonIn jsin(buffer);
    JsonObject jo = jsin.get_object();
    pocket_data data;
    data.load(jo);
    return data;
}

auto pocket_type_of(const item& container, const item* it) -> pocket_type {
    const item_pocket* pocket = container.contents.pocket_of(it);
    REQUIRE(pocket != nullptr);
    return pocket->get_type();
}

} // namespace

TEST_CASE("pocket_data loads DDA-format JSON", "[item][pocket]") {
    SECTION("defaults") {
        const pocket_data data = load_pocket(R"({ })");
        CHECK(data.type == pocket_type::CONTAINER);
        CHECK(data.raw_volume_capacity == pocket_data::max_volume_for_container);
        CHECK(data.max_contains_weight == pocket_data::max_weight_for_container);
        CHECK_FALSE(data.max_item_volume.has_value());
        CHECK(data.min_item_volume == 0_ml);
        CHECK(data.moves == 100);
        CHECK_FALSE(data.watertight);
        CHECK_FALSE(data.holster);
        CHECK_FALSE(data.sealed_data.has_value());
    }

    SECTION("container fields") {
        const pocket_data data = load_pocket(R"({
            "pocket_type": "CONTAINER",
            "max_contains_volume": "2 L",
            "max_contains_weight": "3 kg",
            "max_item_volume": "500 ml",
            "max_item_length": "30 cm",
            "min_item_length": "5 cm",
            "watertight": true,
            "airtight": true,
            "rigid": true,
            "open_container": true,
            "transparent": true,
            "fire_protection": true,
            "moves": 250,
            "spoil_multiplier": 0.5,
            "weight_multiplier": 0.75,
            "volume_multiplier": 0.25,
            "volume_encumber_modifier": 0.3,
            "sealed_data": { "spoil_multiplier": 0.0 },
            "description": "A test pocket.",
            "name": "test pocket"
        })");
        CHECK(data.raw_volume_capacity == 2_liter);
        CHECK(data.max_contains_weight == 3_kilogram);
        REQUIRE(data.max_item_volume.has_value());
        CHECK(*data.max_item_volume == 500_ml);
        CHECK(data.max_item_length == 30_cm);
        CHECK(data.min_item_length == 5_cm);
        CHECK(data.watertight);
        CHECK(data.airtight);
        CHECK(data.rigid);
        CHECK(data.open_container);
        CHECK(data.transparent);
        CHECK(data.fire_protection);
        CHECK(data.moves == 250);
        CHECK(data.spoil_multiplier == Approx(0.5f));
        CHECK(data.weight_multiplier == Approx(0.75f));
        CHECK(data.volume_multiplier == Approx(0.25f));
        CHECK(data.volume_encumber_modifier == Approx(0.3f));
        REQUIRE(data.sealed_data.has_value());
        CHECK(data.sealed_data->spoil_multiplier == Approx(0.0f));
        CHECK(data.description.translated() == "A test pocket.");
        CHECK(data.check_definition().empty());
    }

    SECTION("holster with restrictions") {
        const pocket_data data = load_pocket(R"({
            "holster": true,
            "max_contains_volume": "1 L",
            "min_item_volume": "100 ml",
            "flag_restriction": [ "SHEATH_KNIFE" ],
            "item_restriction": [ "rock" ],
            "skill_restriction": [ "pistol" ]
        })");
        CHECK(data.holster);
        CHECK(data.min_item_volume == 100_ml);
        CHECK(data.get_flag_restrictions().contains(flag_id("SHEATH_KNIFE")));
        CHECK(data.item_id_restriction.contains(itype_id("rock")));
        CHECK(data.skill_restriction.contains(skill_id("pistol")));
        CHECK(data.has_item_restrictions());
    }

    SECTION("ablative pockets are holsters") {
        const pocket_data data = load_pocket(
            R"({ "ablative": true, "max_contains_volume": "2 L" })");
        CHECK(data.ablative);
        CHECK(data.holster);
    }

    SECTION("magazine with ammo restriction") {
        const pocket_data data = load_pocket(R"({
            "pocket_type": "MAGAZINE",
            "ammo_restriction": { "223": 30 }
        })");
        CHECK(data.type == pocket_type::MAGAZINE);
        REQUIRE(data.ammo_restriction.size() == 1);
        CHECK(data.ammo_restriction.at(ammotype("223")) == 30);
    }

    SECTION("magazine well") {
        const pocket_data data = load_pocket(R"({
            "pocket_type": "MAGAZINE_WELL",
            "max_contains_volume": "1 L",
            "magazine_well": "250 ml",
            "item_restriction": [ "stanag30" ]
        })");
        CHECK(data.type == pocket_type::MAGAZINE_WELL);
        CHECK(data.magazine_well == 250_ml);
    }

    SECTION("DDA keys that used to be ignored are loaded") {
        const pocket_data data = load_pocket(R"({
            "max_contains_volume": "1 L",
            "extra_encumbrance": 3,
            "ripoff": 2,
            "activity_noise": { "volume": 8, "chance": 60 },
            "insulation": 0.5,
            "inherits_flags": true,
            "material_restriction": [ "steel" ],
            "allowed_speedloaders": [ "rock" ],
            "default_magazine": "rock",
            "forbidden": true,
            "id": "test_pocket_id",
            "name": "test pocket name"
        })");
        CHECK(data.raw_volume_capacity == 1_liter);
        CHECK(data.volume_capacity() == 1_liter);
        CHECK(data.extra_encumbrance == 3);
        CHECK(data.ripoff == 2);
        CHECK(data.activity_noise.volume == 8);
        CHECK(data.activity_noise.chance == 60);
        CHECK(data.insulation == Approx(0.5f));
        CHECK(data.inherits_flags);
        CHECK(data.forbidden);
        CHECK(data.material_restriction.contains(material_id("steel")));
        CHECK(data.allowed_speedloaders.contains(itype_id("rock")));
        CHECK(data.default_magazine == itype_id("rock"));
        CHECK(data.pocket_id == "test_pocket_id");
        CHECK(data.pocket_name.translated() == "test pocket name");
    }

    SECTION("unknown pocket types are rejected") {
        CHECK_THROWS(load_pocket(R"({ "pocket_type": "NOT_A_POCKET" })"));
        CHECK_THROWS(load_pocket(R"({ "pocket_type": "MIGRATION" })"));
    }

    SECTION("inconsistent data is reported") {
        CHECK_FALSE(
            load_pocket(R"({ "max_contains_volume": "1 L", "max_item_volume": "2 L" })")
                .check_definition()
                .empty());
        CHECK_FALSE(
            load_pocket(R"({ "max_contains_volume": "1 L", "min_item_volume": "2 L" })")
                .check_definition()
                .empty());
        CHECK_FALSE(
            load_pocket(R"({ "watertight": true, "min_item_volume": "10 ml" })")
                .check_definition()
                .empty());
    }
}

TEST_CASE("contents are routed to the right pocket", "[item][pocket]") {
    SECTION("gun: magazine to the well, mods to the mod pocket") {
        detached_ptr<item> gun = item::spawn(itype_id("m4a1"));
        detached_ptr<item> mag = item::spawn(itype_id("stanag30"));
        detached_ptr<item> mod = item::spawn(itype_id("red_dot_sight"));
        item* mag_ptr = &*mag;
        item* mod_ptr = &*mod;
        // DDA num_item_stacks() leaves out the MOD, MAGAZINE_WELL and CORPSE pockets, so count all
        const size_t before = gun->contents.all_items_top().size();
        gun->put_in(std::move(mag));
        gun->put_in(std::move(mod));
        CHECK(gun->contents.all_items_top().size() == before + 2);
        CHECK(pocket_type_of(*gun, mag_ptr) == pocket_type::MAGAZINE_WELL);
        CHECK(pocket_type_of(*gun, mod_ptr) == pocket_type::MOD);
        CHECK(gun->magazine_current() == mag_ptr);
        const std::vector<item*> mods = gun->gunmods();
        CHECK(std::ranges::find(mods, mod_ptr) != mods.end());
        CHECK(gun->contents.migration_pocket() == nullptr);
    }

    SECTION("magazine: ammo to the magazine pocket") {
        detached_ptr<item> mag = item::spawn(itype_id("stanag30"));
        mag->contents.clear_items();
        detached_ptr<item> ammo = item::spawn(itype_id("556"), calendar::turn, 10);
        item* ammo_ptr = &*ammo;
        mag->put_in(std::move(ammo));
        CHECK(pocket_type_of(*mag, ammo_ptr) == pocket_type::MAGAZINE);
        CHECK(mag->ammo_remaining() == 10);
    }

    SECTION("canteen: water to the container") {
        detached_ptr<item> canteen = item::spawn(itype_id("canteen"));
        detached_ptr<item> water = item::spawn(itype_id("water_clean"), calendar::turn, 2);
        item* water_ptr = &*water;
        canteen->put_in(std::move(water));
        CHECK(pocket_type_of(*canteen, water_ptr) == pocket_type::CONTAINER);
        CHECK(&canteen->contents.front() == water_ptr);
    }

    SECTION("holster: pistol to the holster pocket") {
        detached_ptr<item> holster = item::spawn(itype_id("holster"));
        detached_ptr<item> pistol = item::spawn(itype_id("glock_19"));
        item* pistol_ptr = &*pistol;
        holster->put_in(std::move(pistol));
        const item_pocket* pocket = holster->contents.pocket_of(pistol_ptr);
        REQUIRE(pocket != nullptr);
        CHECK(pocket->is_holster());
    }

    SECTION("two-slot holster fills both slots, then nothing is lost") {
        detached_ptr<item> holster = item::spawn(itype_id("test_holster_two"));
        detached_ptr<item> p1 = item::spawn(itype_id("glock_19"));
        detached_ptr<item> p2 = item::spawn(itype_id("glock_19"));
        detached_ptr<item> p3 = item::spawn(itype_id("glock_19"));
        item* p1_ptr = &*p1;
        item* p2_ptr = &*p2;
        item* p3_ptr = &*p3;
        holster->put_in(std::move(p1));
        holster->put_in(std::move(p2));
        holster->put_in(std::move(p3));
        CHECK(holster->contents.pocket_of(p1_ptr) != holster->contents.pocket_of(p2_ptr));
        CHECK(holster->contents.pocket_of(p1_ptr)->is_holster());
        CHECK(holster->contents.pocket_of(p2_ptr)->is_holster());
        CHECK(pocket_type_of(*holster, p3_ptr) == pocket_type::MIGRATION);
        CHECK(holster->contents.num_item_stacks() == 3);
    }

    SECTION("item without pockets: contents go to MIGRATION and stay visible") {
        detached_ptr<item> rock = item::spawn(itype_id("rock"));
        detached_ptr<item> other = item::spawn(itype_id("rock"));
        item* other_ptr = &*other;
        rock->put_in(std::move(other));
        CHECK(pocket_type_of(*rock, other_ptr) == pocket_type::MIGRATION);
        CHECK(rock->contents.num_item_stacks() == 1);
        CHECK(&rock->contents.front() == other_ptr);
        CHECK_FALSE(rock->contents.empty());
    }

    SECTION("count-by-charges items merge into one stack") {
        detached_ptr<item> band = item::spawn(itype_id("bandolier_shotgun"));
        band->put_in(item::spawn(itype_id("shot_00"), calendar::turn, 5));
        band->put_in(item::spawn(itype_id("shot_00"), calendar::turn, 3));
        REQUIRE(band->contents.num_item_stacks() == 1);
        CHECK(band->contents.front().charges == 8);
        CHECK(pocket_type_of(*band, &band->contents.front()) == pocket_type::CONTAINER);
    }

    SECTION("non-charge items never merge") {
        detached_ptr<item> bag = item::spawn(itype_id("test_pocket_two"));
        bag->put_in(item::spawn(itype_id("knife_combat")));
        bag->put_in(item::spawn(itype_id("knife_combat")));
        CHECK(bag->contents.num_item_stacks() == 2);
    }
}

TEST_CASE("rebuild_pockets after convert keeps every item", "[item][pocket]") {
    SECTION("same layout: sealed can opened") {
        detached_ptr<item> can = item::spawn(itype_id("can_drink"));
        can->contents.clear_items();
        detached_ptr<item> water = item::spawn(itype_id("water_clean"), calendar::turn, 1);
        item* water_ptr = &*water;
        can->put_in(std::move(water));
        const item_pocket* before = can->contents.pocket_of(water_ptr);
        can->convert(itype_id("can_drink_unsealed"));
        CHECK(can->contents.pocket_of(water_ptr) == before);
        CHECK(can->contents.num_item_stacks() == 1);
    }

    SECTION("different layout: everything is kept") {
        detached_ptr<item> bag = item::spawn(itype_id("test_pocket_two"));
        bag->put_in(item::spawn(itype_id("knife_combat")));
        bag->put_in(item::spawn(itype_id("knife_combat")));
        bag->put_in(item::spawn(itype_id("glock_19")));
        bag->convert(itype_id("rock"));
        CHECK(bag->contents.num_item_stacks() == 3);
        REQUIRE(bag->contents.migration_pocket() != nullptr);
        CHECK(bag->contents.migration_pocket()->size() == 3);
        bag->convert(itype_id("test_pocket_two"));
        CHECK(bag->contents.num_item_stacks() == 3);
        CHECK(bag->contents.migration_pocket() == nullptr);
    }
}

namespace {

using code = item_pocket::contain_code;

auto contain_code_of(const item_pocket& pocket, const item& it) -> code {
    return pocket.can_contain(it).value();
}

} // namespace

TEST_CASE("can_contain reports why an item doesn't fit", "[item][pocket]") {
    detached_ptr<item> owner = item::spawn(itype_id("test_box"));
    detached_ptr<item> rock = item::spawn(itype_id("test_rock"), calendar::turn, 1);
    detached_ptr<item> pipe = item::spawn(itype_id("test_pipe"));
    detached_ptr<item> cube = item::spawn(itype_id("test_1kg_cube"));
    detached_ptr<item> water = item::spawn(itype_id("water_clean"), calendar::turn, 1);

    SECTION("too big for the pocket") {
        pocket_data data;
        data.max_item_length = 2_km; // J2: C++-built pockets have no default length
        data.raw_volume_capacity = 100_ml;
        item_pocket pocket(&*owner, &data);
        const auto res = pocket.can_contain(*rock);
        CHECK(res.value() == code::ERR_TOO_BIG);
        CHECK_FALSE(res.str().empty());
    }
    SECTION("too heavy for the pocket") {
        pocket_data data;
        data.max_item_length = 2_km; // J2: C++-built pockets have no default length
        data.max_contains_weight = 100_gram;
        item_pocket pocket(&*owner, &data);
        CHECK(contain_code_of(pocket, *rock) == code::ERR_TOO_HEAVY);
    }
    SECTION("no space left") {
        pocket_data data;
        data.max_item_length = 2_km; // J2: C++-built pockets have no default length
        data.raw_volume_capacity = 1300_ml;
        item_pocket pocket(&*owner, &data);
        pocket.insert(item::spawn(itype_id("test_pipe")));
        const auto res = pocket.can_contain(*pipe);
        CHECK(res.value() == code::ERR_NO_SPACE);
        // DDA wording (item_pocket.cpp:1549); the BN message named the volume left
        CHECK(res.str().find("not enough space") != std::string::npos);
        CHECK(contain_code_of(pocket, *rock) == code::SUCCESS);
    }
    SECTION("not enough weight capacity left") {
        pocket_data data;
        data.max_item_length = 2_km; // J2: C++-built pockets have no default length
        data.max_contains_weight = 1500_gram;
        item_pocket pocket(&*owner, &data);
        pocket.insert(item::spawn(itype_id("test_1kg_cube")));
        CHECK(contain_code_of(pocket, *cube) == code::ERR_CANNOT_SUPPORT);
    }
    SECTION("min and max item volume") {
        pocket_data data;
        data.max_item_length = 2_km; // J2: C++-built pockets have no default length
        data.min_item_volume = 500_ml;
        data.max_item_volume = 1_liter;
        item_pocket pocket(&*owner, &data);
        CHECK(contain_code_of(pocket, *rock) == code::ERR_TOO_SMALL);
        CHECK(contain_code_of(pocket, *pipe) == code::SUCCESS);
        detached_ptr<item> big = item::spawn(itype_id("test_2x4"));
        CHECK(contain_code_of(pocket, *big) == code::ERR_TOO_BIG);
    }
    SECTION("liquid needs a watertight pocket") {
        pocket_data data;
        data.max_item_length = 2_km; // J2: C++-built pockets have no default length
        item_pocket pocket(&*owner, &data);
        CHECK(contain_code_of(pocket, *water) == code::ERR_LIQUID);
        pocket_data tight;
        tight.max_item_length = 2_km; // J2: C++-built pockets have no default length
        tight.watertight = true;
        item_pocket tight_pocket(&*owner, &tight);
        CHECK(contain_code_of(tight_pocket, *water) == code::SUCCESS);
        tight_pocket.insert(item::spawn(itype_id("water_clean"), calendar::turn, 1));
        CHECK(contain_code_of(tight_pocket, *rock) == code::ERR_LIQUID);
    }
    SECTION("flag and skill restrictions") {
        pocket_data data;
        data.max_item_length = 2_km; // J2: C++-built pockets have no default length
        data.add_flag_restriction(flag_id("SHEATH_KNIFE"));
        data.skill_restriction.insert(skill_id("pistol"));
        item_pocket pocket(&*owner, &data);
        CHECK(contain_code_of(pocket, *rock) == code::ERR_FLAG);
        detached_ptr<item> pistol = item::spawn(itype_id("glock_19"));
        CHECK(contain_code_of(pocket, *pistol) == code::SUCCESS);
        detached_ptr<item> knife = item::spawn(itype_id("knife_combat"));
        CHECK(knife->has_flag(flag_id("SHEATH_KNIFE")));
        CHECK(contain_code_of(pocket, *knife) == code::SUCCESS);
    }
    SECTION("item restriction") {
        pocket_data data;
        data.max_item_length = 2_km; // J2: C++-built pockets have no default length
        data.item_id_restriction.insert(itype_id("test_pipe"));
        item_pocket pocket(&*owner, &data);
        CHECK(contain_code_of(pocket, *rock) == code::ERR_FLAG);
        CHECK(contain_code_of(pocket, *pipe) == code::SUCCESS);
    }
    SECTION("ammo restriction") {
        pocket_data data(pocket_type::MAGAZINE);
        data.max_item_length = 2_km; // J2: C++-built pockets have no default length
        data.ammo_restriction[ammotype("223")] = 30;
        item_pocket pocket(&*owner, &data);
        detached_ptr<item> shot = item::spawn(itype_id("shot_00"), calendar::turn, 5);
        CHECK(contain_code_of(pocket, *shot) == code::ERR_AMMO);
        CHECK(contain_code_of(pocket, *pipe) == code::ERR_AMMO);
        detached_ptr<item> rounds = item::spawn(itype_id("556"), calendar::turn, 31);
        CHECK(contain_code_of(pocket, *rounds) == code::ERR_NO_SPACE);
        rounds->charges = 30;
        CHECK(contain_code_of(pocket, *rounds) == code::SUCCESS);
    }
    SECTION("sealed pockets are skipped by callers, can_contain has no sealed test (DDA, D23)") {
        pocket_data data;
        data.max_item_length = 2_km; // J2: C++-built pockets have no default length
        data.sealed_data = sealable_data();
        item_pocket pocket(&*owner, &data);
        CHECK(contain_code_of(pocket, *rock) == code::SUCCESS);
        // DDA: sealing an empty pocket does nothing
        CHECK_FALSE(pocket.seal());
        CHECK_FALSE(pocket.sealed());
        pocket.insert(item::spawn(itype_id("test_rock")));
        CHECK(pocket.seal());
        CHECK(pocket.sealed());
        CHECK(contain_code_of(pocket, *rock) == code::SUCCESS);
        pocket.unseal();
        CHECK_FALSE(pocket.sealed());
    }
    SECTION("a holster holds one item") {
        pocket_data data;
        data.max_item_length = 2_km; // J2: C++-built pockets have no default length
        data.holster = true;
        item_pocket pocket(&*owner, &data);
        CHECK(contain_code_of(pocket, *pipe) == code::SUCCESS);
        pocket.insert(item::spawn(itype_id("test_screwdriver")));
        CHECK(pocket.holster_full());
        CHECK(contain_code_of(pocket, *pipe) == code::ERR_NO_SPACE);
    }
    SECTION("mod pockets only take mods") {
        pocket_data data(pocket_type::MOD);
        data.max_item_length = 2_km; // J2: C++-built pockets have no default length
        item_pocket pocket(&*owner, &data);
        CHECK(contain_code_of(pocket, *rock) == code::ERR_MOD);
        detached_ptr<item> mod = item::spawn(itype_id("red_dot_sight"));
        CHECK(contain_code_of(pocket, *mod) == code::SUCCESS);
    }
}

TEST_CASE("best_pocket picks the right pocket", "[item][pocket]") {
    SECTION("a pistol goes in the holster, a can in the bag") {
        detached_ptr<item> rig = item::spawn(itype_id("test_pocket_rig"));
        detached_ptr<item> pistol = item::spawn(itype_id("glock_19"));
        detached_ptr<item> can = item::spawn(itype_id("can_food"));
        const auto for_pistol = rig->contents.best_pocket(*pistol);
        REQUIRE(for_pistol.pocket != nullptr);
        CHECK(for_pistol.container == &*rig);
        CHECK(for_pistol.pocket->is_holster());
        const auto for_can = rig->contents.best_pocket(*can);
        REQUIRE(for_can.pocket != nullptr);
        CHECK_FALSE(for_can.pocket->is_holster());
    }
    SECTION("no descent into a nested box while a top-level pocket fits") {
        detached_ptr<item> box = item::spawn(itype_id("test_box"));
        box->put_in(item::spawn(itype_id("test_box_small")));
        item* small = &box->contents.front();
        detached_ptr<item> screwdriver = item::spawn(itype_id("test_screwdriver"));
        const auto top = box->contents.best_pocket(*screwdriver);
        CHECK(top.container == &*box);
        // fill the top level, then the nested box is used
        box->put_in(item::spawn(itype_id("test_screwdriver")));
        REQUIRE(box->contents.get_pockets().front().remaining_volume() < screwdriver->volume());
        const auto nested = box->contents.best_pocket(*screwdriver);
        CHECK(nested.container == small);
        REQUIRE(nested.pocket != nullptr);
    }
    SECTION("sealed pockets are skipped") {
        detached_ptr<item> can = item::spawn(itype_id("can_drink"));
        detached_ptr<item> water = item::spawn(itype_id("water_clean"), calendar::turn, 1);
        CHECK(can->contents.best_pocket(*water).pocket == nullptr);
        // the wrapper skips sealed pockets itself and says why (D23)
        CHECK_FALSE(can->contents.can_contain(*water, pocket_type::CONTAINER).success());
    }
}

TEST_CASE("player inserts are refused when the item doesn't fit", "[item][pocket]") {
    detached_ptr<item> box = item::spawn(itype_id("test_box_small"));
    detached_ptr<item> pipe = item::spawn(itype_id("test_pipe"));
    const ret_val<bool> refused = box->try_put_in(std::move(pipe));
    CHECK_FALSE(refused.success());
    // nothing moved, nothing lost
    REQUIRE(pipe);
    CHECK(box->contents.empty());
    CHECK(refused.str().find("won't fit") != std::string::npos);

    detached_ptr<item> screwdriver = item::spawn(itype_id("test_screwdriver"));
    CHECK(box->try_put_in(std::move(screwdriver)).success());
    CHECK_FALSE(screwdriver);
    CHECK(box->contents.num_item_stacks() == 1);
    CHECK(box->can_contain(*item::spawn_temporary(itype_id("test_screwdriver"))).success());
    CHECK_FALSE(box->can_contain(*item::spawn_temporary(itype_id("test_pipe"))).success());

    SECTION("force insert never fails: overfull goes to MIGRATION") {
        box->put_in(std::move(pipe));
        CHECK(box->contents.num_item_stacks() == 2);
        REQUIRE(box->contents.migration_pocket() != nullptr);
        CHECK(box->contents.migration_pocket()->size() == 1);
    }
}

TEST_CASE("restacking on insert merges charges only", "[item][pocket]") {
    detached_ptr<item> box = item::spawn(itype_id("test_box"));
    box->put_in(item::spawn(itype_id("test_rock"), calendar::turn, 1));
    box->put_in(item::spawn(itype_id("test_rock"), calendar::turn, 2));
    REQUIRE(box->contents.num_item_stacks() == 1);
    CHECK(box->contents.front().charges == 3);
    box->put_in(item::spawn(itype_id("test_screwdriver")));
    box->put_in(item::spawn(itype_id("test_screwdriver")));
    CHECK(box->contents.num_item_stacks() == 3);
}

namespace {

auto overfull_small_box() -> detached_ptr<item> {
    detached_ptr<item> small = item::spawn(itype_id("test_box_small"));
    small->put_in(item::spawn(itype_id("test_screwdriver")));
    small->put_in(item::spawn(itype_id("test_screwdriver")));
    // force insert: no room left, so it waits in MIGRATION
    small->put_in(item::spawn(itype_id("test_screwdriver")));
    return small;
}

} // namespace

TEST_CASE("overflow goes to the parent, then the character, then the ground", "[item][pocket]") {
    clear_map();
    clear_avatar();
    avatar& you = get_avatar();
    const tripoint_bub_ms pos = you.bub_pos();

    SECTION("force insert alone never overflows") {
        detached_ptr<item> small = overfull_small_box();
        CHECK(small->contents.num_item_stacks() == 3);
        CHECK(small->contents.needs_overflow());
        CHECK_FALSE(small->contents.overflow_pending());
    }

    SECTION("parent container first") {
        detached_ptr<item> box = item::spawn(itype_id("test_box"));
        detached_ptr<item> small = overfull_small_box();
        CHECK(small->contents.overflow(&you, pos, &*box) == 1);
        CHECK(small->contents.num_item_stacks() == 2);
        CHECK((small->contents.migration_pocket() == nullptr
               || small->contents.migration_pocket()->empty()));
        REQUIRE(box->contents.num_item_stacks() == 1);
        CHECK(box->contents.front().typeId() == itype_id("test_screwdriver"));
        CHECK(you.i_at(0).is_null());
    }

    SECTION("then the carrying character") {
        detached_ptr<item> small = overfull_small_box();
        CHECK(small->contents.overflow(&you, pos, nullptr) == 1);
        CHECK(small->contents.num_item_stacks() == 2);
        CHECK(you.has_amount(itype_id("test_screwdriver"), 1));
        CHECK(get_map().i_at(pos).empty());
    }

    SECTION("then the ground, and nothing is lost") {
        detached_ptr<item> small = overfull_small_box();
        CHECK(small->contents.overflow(nullptr, pos, nullptr) == 1);
        CHECK(small->contents.num_item_stacks() == 2);
        map_stack ground = get_map().i_at(pos);
        REQUIRE(ground.size() == 1);
        CHECK(ground.only_item().typeId() == itype_id("test_screwdriver"));
    }

    SECTION("items that never had a pocket stay") {
        detached_ptr<item> rock = item::spawn(itype_id("test_pipe"));
        rock->put_in(item::spawn(itype_id("test_screwdriver")));
        CHECK_FALSE(rock->contents.needs_overflow());
        CHECK(rock->contents.overflow(nullptr, pos, nullptr) == 0);
        CHECK(rock->contents.num_item_stacks() == 1);
        CHECK(get_map().i_at(pos).empty());
    }

    SECTION("a type change marks overflow and processing runs it") {
        detached_ptr<item> box = item::spawn(itype_id("test_box"));
        box->put_in(item::spawn(itype_id("test_screwdriver")));
        box->put_in(item::spawn(itype_id("test_screwdriver")));
        box->put_in(item::spawn(itype_id("test_screwdriver")));
        box->convert(itype_id("test_box_small"));
        CHECK(box->contents.num_item_stacks() == 3);
        CHECK(box->contents.overflow_pending());
        CHECK(box->needs_processing());
        detached_ptr<item> processed = item::process(std::move(box), nullptr, pos, false, 1);
        REQUIRE(processed);
        CHECK_FALSE(processed->contents.overflow_pending());
        CHECK(processed->contents.num_item_stacks() == 2);
        CHECK(get_map().i_at(pos).size() == 1);
    }
}

TEST_CASE("sealed pockets are skipped and unsealed on removal", "[item][pocket]") {
    detached_ptr<item> can = item::spawn(itype_id("can_drink"));
    can->put_in(item::spawn(itype_id("water_clean"), calendar::turn, 1));
    REQUIRE(can->contents.num_item_stacks() == 1);
    CHECK(can->contents.get_pockets().front().sealed());
    detached_ptr<item> more = item::spawn(itype_id("water_clean"), calendar::turn, 1);
    CHECK_FALSE(can->try_put_in(std::move(more)).success());
    REQUIRE(more);

    detached_ptr<item> water = can->contents.front().detach();
    REQUIRE(water);
    CHECK(can->typeId() == itype_id("can_drink_unsealed"));
    CHECK_FALSE(can->contents.get_pockets().front().sealed());
    CHECK(can->try_put_in(std::move(water)).success());
}

TEST_CASE("holsters and bandoliers decide through their pockets", "[item][pocket][holster]") {
    SECTION("a holster pocket takes one pistol, then reports full") {
        detached_ptr<item> rig = item::spawn(itype_id("test_pocket_rig"));
        detached_ptr<item> pistol = item::spawn(itype_id("glock_19"));
        detached_ptr<item> second = item::spawn(itype_id("glock_19"));
        CHECK(rig->can_holster(*pistol));
        CHECK(rig->has_free_holster_pocket());
        item* stored = &*pistol;
        rig->put_in(std::move(pistol));
        CHECK(pocket_type_of(*rig, stored) == pocket_type::CONTAINER);
        CHECK(rig->contents.pocket_of(stored)->is_holster());
        CHECK_FALSE(rig->has_free_holster_pocket());
        CHECK_FALSE(rig->can_holster(*second));
        // ignoring "already full" still reports the holster would take it
        CHECK(rig->can_holster(*second, true));
    }
    SECTION("size limits are the pocket's") {
        detached_ptr<item> rig = item::spawn(itype_id("test_pocket_rig"));
        detached_ptr<item> rifle = item::spawn(itype_id("m4a1"));
        CHECK_FALSE(rig->can_holster(*rifle, true));
    }
    SECTION("drawing costs the pocket's moves") {
        detached_ptr<item> rig = item::spawn(itype_id("test_pocket_rig"));
        const item_pocket* holster = nullptr;
        for (const item_pocket& pocket : rig->contents.get_pockets()) {
            if (pocket.is_holster()) { holster = &pocket; }
        }
        REQUIRE(holster != nullptr);
        CHECK(holster->get_data().moves == 50);
    }
    SECTION("a bandolier takes its ammo and nothing else") {
        detached_ptr<item> bandolier = item::spawn(itype_id("bandolier_shotgun"));
        detached_ptr<item> shells = item::spawn(itype_id("shot_00"), calendar::turn, 6);
        detached_ptr<item> pistol = item::spawn(itype_id("glock_19"));
        CHECK(bandolier->can_put_in_bandolier(*shells));
        CHECK_FALSE(bandolier->can_put_in_bandolier(*pistol));
    }
}

TEST_CASE("a magazine_adaptor mod rebuilds the magazine well", "[item][pocket][magazine]") {
    detached_ptr<item> gun = item::spawn(itype_id("m4a1"));
    detached_ptr<item> mod = item::spawn(itype_id("retool_ar15_300blk"));
    REQUIRE(mod->type->mod);
    REQUIRE_FALSE(mod->type->mod->magazine_adaptor.empty());

    const auto well_of = [](const item& it) -> const item_pocket* {
        for (const item_pocket& pocket : it.contents.get_pockets()) {
            if (pocket.is_type(pocket_type::MAGAZINE_WELL)) { return &pocket; }
        }
        return nullptr;
    };
    REQUIRE(well_of(*gun) != nullptr);

    gun->put_in(std::move(mod));
    const item_pocket* well = well_of(*gun);
    REQUIRE(well != nullptr);
    // the well takes exactly what item::magazine_compatible reports
    // (the adaptor's magazines for the ammo the gun now fires)
    CHECK(std::set<itype_id>(well->get_data().item_id_restriction.begin(),
                             well->get_data().item_id_restriction.end())
          == gun->magazine_compatible());
    CHECK_FALSE(well->get_data().item_id_restriction.empty());

    detached_ptr<item> mag = item::spawn(itype_id("stanag30"));
    CHECK(well->can_contain(*mag).success());
    item* mag_ptr = &*mag;
    gun->put_in(std::move(mag));
    CHECK(pocket_type_of(*gun, mag_ptr) == pocket_type::MAGAZINE_WELL);
    CHECK(gun->magazine_current() == mag_ptr);
    // the mod stays where it was
    REQUIRE(gun->gunmods().size() == 1);
    CHECK(pocket_type_of(*gun, gun->gunmods().front()) == pocket_type::MOD);
}

// CDF: rewritten for J6 (the text is DDA's, item_contents::info replaces item::container_info)
TEST_CASE("item info lists each pocket", "[item][pocket][iteminfo]") {
    const iteminfo_query query(std::vector<iteminfo_parts>{iteminfo_parts::DESCRIPTION_POCKETS});
    SECTION("a two-pocket rig") {
        detached_ptr<item> rig = item::spawn(itype_id("test_pocket_rig"));
        const std::string info = rig->info_string(query, 1);
        CAPTURE(info);
        CHECK(info.find("Pocket 1") != std::string::npos);
        CHECK(info.find("Pocket 2") != std::string::npos);
        CHECK(info.find("holster") != std::string::npos);
    }
    SECTION("a plain jug is a single container with a total capacity") {
        detached_ptr<item> jug = item::spawn(itype_id("test_jug_plastic"));
        const std::string info = jug->info_string(query, 1);
        CAPTURE(info);
        CHECK(info.find("Total capacity") != std::string::npos);
        CHECK(info.find("contain a liquid") != std::string::npos);
        CHECK(info.find("Pocket 1") == std::string::npos);
    }
}

// CDF: deliberate rewrite for J6 (owner decision D13, DDA item_group.cpp:716-748): an item that
// does not fit the entry-level container-item / contents-item target is lost and reported with a
// debugmsg, it is no longer spawned next to the container.
TEST_CASE("spawned items too long for a container are lost with a debugmsg", "[item][pocket]") {
    SECTION("container-item") {
        std::vector<detached_ptr<item>> spawned;
        const std::string msg = capture_debugmsg_during([&spawned]() {
            spawned = item_group::items_from(item_group_id("test_long_rod_in_pouch"));
        });
        CHECK_FALSE(msg.empty());
        REQUIRE(spawned.size() == 1);
        CHECK(spawned[0]->typeId() == itype_id("test_pocket_dda"));
        CHECK(spawned[0]->contents.empty());
    }
    SECTION("contents-item") {
        std::vector<detached_ptr<item>> spawned;
        const std::string msg = capture_debugmsg_during([&spawned]() {
            spawned = item_group::items_from(item_group_id("test_pouch_with_long_rod"));
        });
        CHECK_FALSE(msg.empty());
        REQUIRE(spawned.size() == 1);
        CHECK(spawned[0]->contents.empty());
    }
}

// CDF: ret_val<void> case, written for the transplant (DDA ret_val.h b4b62254ae2)
TEST_CASE("ret_val<void> reports success, failure and message", "[ret_val]") {
    const auto ok = ret_val<void>::make_success("fine %d", 3);
    CHECK(ok.success());
    CHECK(ok.str() == "fine 3");
    CHECK(std::string(ok.c_str()) == "fine 3");

    const auto bad = ret_val<void>::make_failure("broken");
    CHECK_FALSE(bad.success());
    CHECK(bad.str() == "broken");

    const auto silent = ret_val<void>::make_success();
    CHECK(silent.success());
    CHECK(silent.str().empty());
}

TEST_CASE("test_data loads pocket mod test blocks", "[item][pocket][test_data]") {
    std::istringstream buffer(R"({ "type": "test_data", "pocket_mod_data": {
        "cdf_load_check": { "base_item": "rock", "mod_item": "rock",
                             "expected_pockets": { "CONTAINER": [ 1 ] } } } })");
    JsonIn jsin(buffer);
    JsonObject jo = jsin.get_object();
    test_data::load(jo);
    REQUIRE(test_data::pocket_mod_data.count("cdf_load_check") == 1);
    CHECK(test_data::pocket_mod_data.at("cdf_load_check").base_item == itype_id("rock"));
    test_data::pocket_mod_data.erase("cdf_load_check");
}

// J3 (item_container.cpp): the DDA cases that cover the same ground need fixtures CDF doesn't
// have (see tests/dda_item_pocket_contents_test.cpp).

TEST_CASE( "a default container is sealed and whitelists what it holds",
           "[item][pocket][item_spawn]" )
{
    const bool edited = GENERATE( false, true );
    CAPTURE( edited );
    const itype_id water_id( "water_clean" );
    detached_ptr<item> spawned = item::in_its_container( item::spawn( water_id, calendar::turn, 1 ) );
    item &container = *spawned;
    REQUIRE( container.typeId() == *water_id->default_container );
    REQUIRE( container.is_container() );
    REQUIRE( container.has_item_with_id( water_id ) );

    const std::vector<item_pocket *> pockets = container.get_container_pockets();
    REQUIRE( pockets.size() == 1 );
    // DDA item_container.cpp:91: one contained type becomes the pocket's whitelist
    CHECK( pockets.front()->settings.get_item_whitelist().contains( water_id ) );
    CHECK( pockets.front()->settings.priority() == 100 );

    // emptied: the whitelist goes with the contents, unless the player edited the settings
    if( edited ) {
        pockets.front()->settings.set_was_edited();
    }
    container.contents.clear_items();
    container.on_contents_changed();
    REQUIRE( container.empty_container() );
    CHECK( pockets.front()->settings.get_item_whitelist().empty() == !edited );
}

TEST_CASE( "item contents default state and sealed pockets", "[item][pocket]" )
{
    SECTION( "a fresh container is in its default state" ) {
        const item &canteen = *item::spawn_temporary( itype_id( "canteen" ) );
        CHECK( canteen.contents.empty_with_no_mods() );
        CHECK( canteen.empty() );
        CHECK( canteen.empty_container() );
    }
    SECTION( "mods are not contents" ) {
        item &gun = *item::spawn_temporary( itype_id( "m4a1" ) );
        REQUIRE( gun.put_in( item( itype_id( "red_dot_sight" ) ), pocket_type::MOD ).success() );
        CHECK( gun.empty() );
        CHECK_FALSE( gun.contents.empty_with_no_mods() );
        CHECK( gun.num_item_stacks() == 0 );
    }
}
