#include "../src/map/map.h"
#include "activity_actor_definitions.h"
#include "activity_handlers.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_utility.h"
#include "catch/catch.hpp"
#include "character.h"
#include "coordinates.h"
#include "enums.h"
#include "game.h"
#include "itype.h"
#include "iuse_actor.h"
#include "detached_ptr.h"
#include "item.h"
#include "item_group.h"
#include "pocket_type.h"
#include "map_helpers.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units_angle.h"
#include "units_volume.h"
#include "vehicle/veh_type.h"
#include "vehicle/vehicle.h"
#include "vehicle/vehicle_part.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <set>
#include <vector>

static const activity_id ACT_NULL = activity_id::NULL_ID();
static const activity_id ACT_BOLTCUTTING("ACT_BOLTCUTTING");
static const activity_id ACT_HACKSAW("ACT_HACKSAW");
static const activity_id ACT_OXYTORCH("ACT_OXYTORCH");

static const furn_str_id furn_t_test_f_boltcut1("test_f_boltcut1");
static const furn_str_id furn_t_test_f_boltcut2("test_f_boltcut2");
static const furn_str_id furn_t_test_f_boltcut3("test_f_boltcut3");
static const furn_str_id furn_t_test_f_hacksaw1("test_f_hacksaw1");
static const furn_str_id furn_t_test_f_hacksaw2("test_f_hacksaw2");
static const furn_str_id furn_t_test_f_hacksaw3("test_f_hacksaw3");
static const furn_str_id furn_t_test_f_oxytorch1("test_f_oxytorch1");
static const furn_str_id furn_t_test_f_oxytorch2("test_f_oxytorch2");
static const furn_str_id furn_t_test_f_oxytorch3("test_f_oxytorch3");

static const item_group_id Item_spawn_data_test_edevices_compat("test_edevices_compat");
static const item_group_id Item_spawn_data_test_edevices_incompat("test_edevices_incompat");
static const item_group_id Item_spawn_data_test_edevices_power("test_edevices_power");
static const item_group_id Item_spawn_data_test_edevices_recipes("test_edevices_recipes");
static const item_group_id Item_spawn_data_test_edevices_standard("test_edevices_standard");

static const itype_id itype_oxyacetylene("oxyacetylene");
static const itype_id itype_test_backpack("test_backpack");
static const itype_id itype_test_efile_copiable("test_efile_copiable");
static const itype_id itype_test_efile_etd("test_efile_etd");
static const itype_id itype_test_boltcutter("test_boltcutter");
static const itype_id itype_test_boltcutter_elec("test_boltcutter_elec");
static const itype_id itype_test_hacksaw("test_hacksaw");
static const itype_id itype_test_hacksaw_elec("test_hacksaw_elec");
static const itype_id itype_test_oxytorch("test_oxytorch");

static const skill_id skill_computer("computer");

static const quality_id qual_SAW_M("SAW_M");
static const quality_id qual_WELD("WELD");

static const ter_str_id ter_test_t_oxytorch1("test_t_oxytorch1");
static const ter_str_id ter_test_t_oxytorch2("test_t_oxytorch2");

static const ter_str_id ter_test_t_boltcut1("test_t_boltcut1");
static const ter_str_id ter_test_t_boltcut2("test_t_boltcut2");
static const ter_str_id ter_test_t_hacksaw1("test_t_hacksaw1");
static const ter_str_id ter_test_t_hacksaw2("test_t_hacksaw2");

TEST_CASE(
    "fill_liquid_from_infinite_water_respects_ground_pour_amount", "[activity][fluid_regression]") {
    clear_all_state();
    const auto cleanup = on_out_of_scope([]() { clear_all_state(); });
    auto& here = get_map();
    auto& you = get_avatar();
    const auto source = tripoint_bub_ms(60, 60, 0);
    const auto target = tripoint_bub_ms(62, 60, 0);
    const auto water_field = field_type_id("fd_water");
    g->place_player(tripoint_bub_ms(61, 60, 0));
    here.ter_set(source, ter_str_id("t_water_sh"));
    REQUIRE(here.water_from(source));
    const auto charges_per_turn =
        std::max(1, here.water_from(source)->charges_per_volume(units::from_liter(4.0F / 6.0F)));
    const auto requested = charges_per_turn + 1;

    auto activity = player_activity(activity_id("ACT_FILL_LIQUID"));
    activity.values = {LST_INFINITE_MAP, 0, LTT_MAP, requested};
    activity.coords = {bub_to_abs(source), bub_to_abs(target)};

    activity_handlers::fill_liquid_do_turn(&activity, &you);
    CHECK_FALSE(activity.is_null());
    CHECK(activity.values[3] == 1);
    CHECK(here.get_field(target, water_field) != nullptr);

    activity_handlers::fill_liquid_do_turn(&activity, &you);
    CHECK(activity.is_null());

    here.remove_field(target, water_field);
    auto zero_activity = player_activity(activity_id("ACT_FILL_LIQUID"));
    zero_activity.values = {LST_INFINITE_MAP, 0, LTT_MAP, 0};
    zero_activity.coords = {bub_to_abs(source), bub_to_abs(target)};
    activity_handlers::fill_liquid_do_turn(&zero_activity, &you);
    CHECK(zero_activity.is_null());
    CHECK(here.get_field(target, water_field) == nullptr);

    auto legacy_activity = player_activity(activity_id("ACT_FILL_LIQUID"));
    legacy_activity.values = {LST_INFINITE_MAP, 0, LTT_MAP};
    legacy_activity.coords = {bub_to_abs(source), bub_to_abs(target)};
    activity_handlers::fill_liquid_do_turn(&legacy_activity, &you);
    CHECK_FALSE(legacy_activity.is_null());
    CHECK(here.get_field(target, water_field) != nullptr);
}

TEST_CASE(
    "fill_liquid_from_vehicle_respects_ground_pour_amount_and_exhaustion",
    "[activity][fluid_regression]") {
    clear_all_state();
    const auto cleanup = on_out_of_scope([]() { clear_all_state(); });
    auto& here = get_map();
    auto& you = get_avatar();
    const auto source = tripoint_bub_ms(60, 60, 0);
    const auto target = tripoint_bub_ms(62, 60, 0);
    const auto water_field = field_type_id("fd_water");
    g->place_player(tripoint_bub_ms(61, 60, 0));
    auto* vehicle = here.add_vehicle(vproto_id("none"), source, 0_degrees, 0, 0);
    REQUIRE(vehicle);
    REQUIRE(vehicle->install_part(tripoint_mnt_veh::zero(), vpart_id("frame_vertical"), true) >= 0);
    const auto tank_index =
        vehicle->install_part(tripoint_mnt_veh::zero(), vpart_id("tank_small"), true);
    REQUIRE(tank_index >= 0);
    here.add_vehicle_to_cache(vehicle);
    here.build_map_cache(source.z(), true);
    const auto vehicle_source = vehicle->bub_part_location(0);
    REQUIRE(here.veh_at(vehicle_source));
    auto& tank = vehicle->part(tank_index);
    const auto water = itype_id("water_clean");
    REQUIRE(tank.ammo_set(water, 20) > 0);
    const auto initial_charges = tank.ammo_remaining();
    const auto charges_per_turn = std::
        max(1, tank.get_base().contents.back().charges_per_volume(units::from_liter(4.0F / 6.0F)));
    const auto requested = charges_per_turn + 1;

    auto activity = player_activity(activity_id("ACT_FILL_LIQUID"));
    activity.values = {LST_VEHICLE, tank_index, LTT_MAP, requested};
    activity.coords = {bub_to_abs(vehicle_source), bub_to_abs(target)};
    activity_handlers::fill_liquid_do_turn(&activity, &you);
    CHECK_FALSE(activity.is_null());
    CHECK(activity.values[3] == 1);
    CHECK(tank.ammo_remaining() == initial_charges - charges_per_turn);
    CHECK(here.get_field(target, water_field) != nullptr);

    activity_handlers::fill_liquid_do_turn(&activity, &you);
    CHECK(activity.is_null());
    CHECK(tank.ammo_remaining() == initial_charges - requested);

    here.remove_field(target, water_field);
    auto zero_activity = player_activity(activity_id("ACT_FILL_LIQUID"));
    zero_activity.values = {LST_VEHICLE, tank_index, LTT_MAP, 0};
    zero_activity.coords = {bub_to_abs(vehicle_source), bub_to_abs(target)};
    activity_handlers::fill_liquid_do_turn(&zero_activity, &you);
    CHECK(zero_activity.is_null());
    CHECK(tank.ammo_remaining() == initial_charges - requested);
    CHECK(here.get_field(target, water_field) == nullptr);

    REQUIRE(tank.ammo_set(water, 1) > 0);
    auto exhausted_activity = player_activity(activity_id("ACT_FILL_LIQUID"));
    exhausted_activity.values = {LST_VEHICLE, tank_index, LTT_MAP, 3};
    exhausted_activity.coords = {bub_to_abs(vehicle_source), bub_to_abs(target)};
    activity_handlers::fill_liquid_do_turn(&exhausted_activity, &you);
    CHECK(exhausted_activity.is_null());
    CHECK(exhausted_activity.values[3] == 2);
    CHECK(tank.ammo_remaining() == 0);
    CHECK(here.get_field(target, water_field) != nullptr);
}

TEST_CASE("boltcut", "[activity][boltcut]") {
    map& mp = get_map();
    avatar& dummy = get_avatar();

    auto setup_dummy = [&dummy]() -> item& {
        item& cutter = dummy.i_add(item::spawn(itype_test_boltcutter));
        dummy.wield(cutter);

        REQUIRE(dummy.primary_weapon().typeId() == itype_test_boltcutter);

        return cutter;
    };

    auto setup_activity = [&dummy](item& cutter) -> void {
        auto act = std::make_unique<
            boltcutting_activity_actor>(tripoint_bub_ms::zero(), safe_reference<item>(cutter));
        act->testing = true;
        dummy.assign_activity(std::make_unique<player_activity>(std::move(act)));
    };

    SECTION("boltcut start checks") {
        GIVEN("a tripoint_bub_ms with nothing") {
            clear_map();
            clear_avatar();

            mp.ter_set(tripoint_bub_ms::zero(), t_null);
            REQUIRE(mp.ter(tripoint_bub_ms::zero()) == t_null);

            item& boltcutter = setup_dummy();
            setup_activity(boltcutter);

            THEN("boltcutting activity can't start") { CHECK(dummy.activity->id() == ACT_NULL); }
        }

        GIVEN("a tripoint_bub_ms with invalid terrain") {
            clear_map();
            clear_avatar();

            mp.ter_set(tripoint_bub_ms::zero(), t_dirt);
            REQUIRE(mp.ter(tripoint_bub_ms::zero()) == t_dirt);

            item& boltcutter = setup_dummy();
            setup_activity(boltcutter);

            THEN("boltcutting activity can't start") { CHECK(dummy.activity->id() == ACT_NULL); }
        }

        GIVEN("a tripoint_bub_ms with valid terrain") {
            clear_map();
            clear_avatar();

            mp.ter_set(tripoint_bub_ms::zero(), ter_test_t_boltcut1);
            REQUIRE(mp.ter(tripoint_bub_ms::zero()) == ter_test_t_boltcut1);

            item& boltcutter = setup_dummy();
            setup_activity(boltcutter);

            THEN("boltcutting activity can start") {
                CHECK(dummy.activity->id() == ACT_BOLTCUTTING);
            }
        }

        GIVEN("a tripoint_bub_ms with valid furniture") {
            clear_map();
            clear_avatar();

            mp.furn_set(tripoint_bub_ms::zero(), furn_t_test_f_boltcut1);
            REQUIRE(mp.furn(tripoint_bub_ms::zero()) == furn_t_test_f_boltcut1);

            item& boltcutter = setup_dummy();
            setup_activity(boltcutter);

            THEN("boltcutting activity can start") {
                CHECK(dummy.activity->id() == ACT_BOLTCUTTING);
            }
        }

        GIVEN("a tripoint_bub_ms with valid terrain") {
            clear_map();
            clear_avatar();

            mp.ter_set(tripoint_bub_ms::zero(), ter_test_t_boltcut1);
            REQUIRE(mp.ter(tripoint_bub_ms::zero()) == ter_test_t_boltcut1);

            item& boltcutter = setup_dummy();
            setup_activity(boltcutter);
            REQUIRE(dummy.activity->id() == ACT_BOLTCUTTING);

            WHEN("terrain has a duration of 10 seconds") {
                REQUIRE(ter_test_t_boltcut1->boltcut->duration() == 10_seconds);
                THEN("moves_left is equal to 10 seconds") {
                    CHECK(dummy.activity->get_moves_left() == to_moves<int>(10_seconds));
                }
            }
        }

        GIVEN("a tripoint_bub_ms with valid furniture") {
            clear_map();
            clear_avatar();

            mp.furn_set(tripoint_bub_ms::zero(), furn_t_test_f_boltcut1);
            REQUIRE(mp.furn(tripoint_bub_ms::zero()) == furn_t_test_f_boltcut1);

            item& boltcutter = setup_dummy();
            setup_activity(boltcutter);
            REQUIRE(dummy.activity->id() == ACT_BOLTCUTTING);

            WHEN("furniture has a duration of 5 seconds") {
                REQUIRE(furn_t_test_f_boltcut1->boltcut->duration() == 5_seconds);
                THEN("moves_left is equal to 5 seconds") {
                    CHECK(dummy.activity->get_moves_left() == to_moves<int>(5_seconds));
                }
            }
        }
    }

    SECTION("boltcut turn checks") {
        GIVEN("player is in mid activity") {
            clear_map();
            clear_avatar();

            mp.furn_set(tripoint_bub_ms::zero(), furn_t_test_f_boltcut3);
            REQUIRE(mp.furn(tripoint_bub_ms::zero()) == furn_t_test_f_boltcut3);

            item& boltcutter_elec = dummy.i_add(
                item::spawn(itype_test_boltcutter_elec, calendar::start_of_cataclysm, 2));
            dummy.wield(boltcutter_elec);

            REQUIRE(dummy.primary_weapon().typeId() == itype_test_boltcutter_elec);

            setup_activity(boltcutter_elec);
            REQUIRE(dummy.activity->id() == ACT_BOLTCUTTING);
            process_activity(dummy);

            WHEN("player runs out of charges") {
                REQUIRE(dummy.activity->id() == ACT_NULL);

                THEN("player recharges with fuel") {
                    boltcutter_elec.ammo_set(boltcutter_elec.ammo_default(), -1);

                    AND_THEN("player can resume the activity") {
                        setup_activity(boltcutter_elec);
                        dummy.moves = dummy.get_speed();
                        dummy.activity->do_turn(dummy);
                        CHECK(dummy.activity->id() == ACT_BOLTCUTTING);
                        CHECK(dummy.activity->get_moves_left()
                              < to_moves<int>(furn_t_test_f_boltcut3->boltcut->duration()));
                    }
                }
            }
        }
    }

    SECTION("boltcut finish checks") {
        GIVEN("a tripoint_bub_ms with valid terrain") {
            clear_map();
            clear_avatar();

            mp.ter_set(tripoint_bub_ms::zero(), ter_test_t_boltcut1);
            REQUIRE(mp.ter(tripoint_bub_ms::zero()) == ter_test_t_boltcut1);

            item& boltcutter = setup_dummy();
            setup_activity(boltcutter);

            REQUIRE(dummy.activity->id() == ACT_BOLTCUTTING);
            process_activity(dummy);
            REQUIRE(dummy.activity->id() == ACT_NULL);

            THEN("terrain gets converted to new terrain type") {
                CHECK(mp.ter(tripoint_bub_ms::zero()) == t_dirt);
            }
        }

        GIVEN("a tripoint_bub_ms with valid furniture") {
            clear_map();
            clear_avatar();

            mp.furn_set(tripoint_bub_ms::zero(), furn_t_test_f_boltcut1);
            REQUIRE(mp.furn(tripoint_bub_ms::zero()) == furn_t_test_f_boltcut1);

            item& boltcutter = setup_dummy();
            setup_activity(boltcutter);

            REQUIRE(dummy.activity->id() == ACT_BOLTCUTTING);
            process_activity(dummy);
            REQUIRE(dummy.activity->id() == ACT_NULL);

            THEN("furniture gets converted to new furniture type") {
                CHECK(mp.furn(tripoint_bub_ms::zero()) == f_null);
            }
        }

        GIVEN("a tripoint_bub_ms with valid furniture") {
            clear_map();
            clear_avatar();

            mp.furn_set(tripoint_bub_ms::zero(), furn_t_test_f_boltcut2);
            REQUIRE(mp.furn(tripoint_bub_ms::zero()) == furn_t_test_f_boltcut2);

            item& boltcutter = setup_dummy();
            setup_activity(boltcutter);

            REQUIRE(dummy.activity->id() == ACT_BOLTCUTTING);
            process_activity(dummy);
            REQUIRE(dummy.activity->id() == ACT_NULL);

            THEN("furniture gets converted to new furniture type") {
                CHECK(mp.furn(tripoint_bub_ms::zero()) == furn_t_test_f_boltcut1);
            }
        }


        GIVEN("a tripoint_bub_ms with a valid furniture with byproducts") {
            clear_map();
            clear_avatar();

            mp.ter_set(tripoint_bub_ms::zero(), ter_test_t_boltcut2);
            REQUIRE(mp.ter(tripoint_bub_ms::zero()) == ter_test_t_boltcut2);

            item& boltcutter = setup_dummy();
            setup_activity(boltcutter);

            REQUIRE(ter_test_t_boltcut2->boltcut->byproducts().size() == 2);

            REQUIRE(dummy.activity->id() == ACT_BOLTCUTTING);
            process_activity(dummy);
            REQUIRE(dummy.activity->id() == ACT_NULL);

            const itype_id test_amount("test_rock");
            const itype_id test_random("test_2x4");

            WHEN("boltcut acitivy finishes") {
                CHECK(dummy.activity->id() == ACT_NULL);

                THEN("player receives the items") {
                    int count_amount = 0;
                    int count_random = 0;
                    for (const auto& it : get_map().i_at(tripoint_bub_ms::zero())) {
                        // can't use switch here
                        const itype_id it_id = it->typeId();
                        if (it_id == test_amount) {
                            count_amount += it->charges;
                        } else if (it_id == test_random) {
                            count_random += 1;
                        }
                    }

                    CHECK(count_amount == 3);
                    CHECK((7 <= count_random && count_random <= 9));
                }
            }
        }
    }
}

TEST_CASE("hacksaw", "[activity][hacksaw]") {
    map& mp = get_map();
    avatar& dummy = get_avatar();

    auto setup_dummy = [&dummy]() -> item& {
        item& saw = dummy.i_add(item::spawn(itype_test_hacksaw));
        dummy.wield(saw);

        REQUIRE(dummy.primary_weapon().typeId() == itype_test_hacksaw);
        REQUIRE(dummy.max_quality(qual_SAW_M) == 10);

        return saw;
    };

    auto setup_activity = [&dummy](item& saw) -> void {
        auto act = std::make_unique<
            hacksaw_activity_actor>(tripoint_bub_ms::zero(), safe_reference<item>(saw));
        act->testing = true;
        dummy.assign_activity(std::make_unique<player_activity>(std::move(act)));
    };

    SECTION("hacksaw start checks") {
        GIVEN("a tripoint_bub_ms with nothing") {
            clear_map();
            clear_avatar();

            mp.ter_set(tripoint_bub_ms::zero(), t_null);
            REQUIRE(mp.ter(tripoint_bub_ms::zero()) == t_null);

            item& hacksaw = setup_dummy();
            setup_activity(hacksaw);

            THEN("hacksaw activity can't start") { CHECK(dummy.activity->id() == ACT_NULL); }
        }

        GIVEN("a tripoint_bub_ms with invalid terrain") {
            clear_map();
            clear_avatar();

            mp.ter_set(tripoint_bub_ms::zero(), t_dirt);
            REQUIRE(mp.ter(tripoint_bub_ms::zero()) == t_dirt);

            item& hacksaw = setup_dummy();
            setup_activity(hacksaw);

            THEN("hacksaw activity can't start") { CHECK(dummy.activity->id() == ACT_NULL); }
        }

        GIVEN("a tripoint_bub_ms with valid terrain") {
            clear_map();
            clear_avatar();

            mp.ter_set(tripoint_bub_ms::zero(), ter_test_t_hacksaw1);
            REQUIRE(mp.ter(tripoint_bub_ms::zero()) == ter_test_t_hacksaw1);

            item& hacksaw = setup_dummy();
            setup_activity(hacksaw);

            THEN("hacksaw activity can start") { CHECK(dummy.activity->id() == ACT_HACKSAW); }
        }

        GIVEN("a tripoint_bub_ms with valid furniture") {
            clear_map();
            clear_avatar();

            mp.furn_set(tripoint_bub_ms::zero(), furn_t_test_f_hacksaw1);
            REQUIRE(mp.furn(tripoint_bub_ms::zero()) == furn_t_test_f_hacksaw1);

            item& hacksaw = setup_dummy();
            setup_activity(hacksaw);

            THEN("hacksaw activity can start") { CHECK(dummy.activity->id() == ACT_HACKSAW); }
        }

        GIVEN("a tripoint_bub_ms with valid terrain") {
            clear_map();
            clear_avatar();

            mp.ter_set(tripoint_bub_ms::zero(), ter_test_t_hacksaw1);
            REQUIRE(mp.ter(tripoint_bub_ms::zero()) == ter_test_t_hacksaw1);

            item& hacksaw = setup_dummy();
            setup_activity(hacksaw);
            REQUIRE(dummy.activity->id() == ACT_HACKSAW);

            WHEN("terrain has a duration of 10 minutes") {
                REQUIRE(ter_test_t_hacksaw1->hacksaw->duration() == 10_minutes);
                THEN("moves_left is equal to 10 minutes") {
                    CHECK(dummy.activity->get_moves_left() == to_moves<int>(10_minutes));
                }
            }
        }

        GIVEN("a tripoint_bub_ms with valid furniture") {
            clear_map();
            clear_avatar();

            mp.furn_set(tripoint_bub_ms::zero(), furn_t_test_f_hacksaw1);
            REQUIRE(mp.furn(tripoint_bub_ms::zero()) == furn_t_test_f_hacksaw1);

            item& hacksaw = setup_dummy();
            setup_activity(hacksaw);
            REQUIRE(dummy.activity->id() == ACT_HACKSAW);

            WHEN("furniture has a duration of 5 minutes") {
                REQUIRE(furn_t_test_f_hacksaw1->hacksaw->duration() == 5_minutes);
                THEN("moves_left is equal to 5 minutes") {
                    CHECK(dummy.activity->get_moves_left() == to_moves<int>(5_minutes));
                }
            }
        }
    }

    SECTION("hacksaw turn checks") {
        GIVEN("player is in mid activity") {
            clear_map();
            clear_avatar();

            mp.furn_set(tripoint_bub_ms::zero(), furn_t_test_f_hacksaw3);
            REQUIRE(mp.furn(tripoint_bub_ms::zero()) == furn_t_test_f_hacksaw3);

            item& hacksaw_elec = dummy.i_add(
                item::spawn(itype_test_hacksaw_elec, calendar::start_of_cataclysm, 1));
            dummy.wield(hacksaw_elec);

            REQUIRE(dummy.primary_weapon().typeId() == itype_test_hacksaw_elec);
            REQUIRE(dummy.max_quality(qual_SAW_M) == 10);

            setup_activity(hacksaw_elec);
            REQUIRE(dummy.activity->id() == ACT_HACKSAW);
            process_activity(dummy);

            WHEN("player runs out of charges") {
                REQUIRE(dummy.activity->id() == ACT_NULL);

                THEN("player recharges with fuel") {
                    hacksaw_elec.ammo_set(hacksaw_elec.ammo_default(), -1);

                    AND_THEN("player can resume the activity") {
                        setup_activity(hacksaw_elec);
                        dummy.moves = dummy.get_speed();
                        dummy.activity->do_turn(dummy);
                        CHECK(dummy.activity->id() == ACT_HACKSAW);
                        CHECK(dummy.activity->get_moves_left()
                              < to_moves<int>(furn_t_test_f_hacksaw3->hacksaw->duration()));
                    }
                }
            }
        }
    }

    SECTION("hacksaw finish checks") {
        GIVEN("a tripoint_bub_ms with valid terrain") {
            clear_map();
            clear_avatar();

            mp.ter_set(tripoint_bub_ms::zero(), ter_test_t_hacksaw1);
            REQUIRE(mp.ter(tripoint_bub_ms::zero()) == ter_test_t_hacksaw1);

            item& hacksaw = setup_dummy();
            setup_activity(hacksaw);

            REQUIRE(dummy.activity->id() == ACT_HACKSAW);
            process_activity(dummy);
            REQUIRE(dummy.activity->id() == ACT_NULL);

            THEN("terrain gets converted to new terrain type") {
                CHECK(mp.ter(tripoint_bub_ms::zero()) == t_dirt);
            }
        }

        GIVEN("a tripoint_bub_ms with valid furniture") {
            clear_map();
            clear_avatar();

            mp.furn_set(tripoint_bub_ms::zero(), furn_t_test_f_hacksaw1);
            REQUIRE(mp.furn(tripoint_bub_ms::zero()) == furn_t_test_f_hacksaw1);

            item& hacksaw = setup_dummy();
            setup_activity(hacksaw);

            REQUIRE(dummy.activity->id() == ACT_HACKSAW);
            process_activity(dummy);
            REQUIRE(dummy.activity->id() == ACT_NULL);

            THEN("furniture gets converted to new furniture type") {
                CHECK(mp.furn(tripoint_bub_ms::zero()) == f_null);
            }
        }

        GIVEN("a tripoint_bub_ms with valid furniture") {
            clear_map();
            clear_avatar();

            mp.furn_set(tripoint_bub_ms::zero(), furn_t_test_f_hacksaw2);
            REQUIRE(mp.furn(tripoint_bub_ms::zero()) == furn_t_test_f_hacksaw2);

            item& hacksaw = setup_dummy();
            setup_activity(hacksaw);

            REQUIRE(dummy.activity->id() == ACT_HACKSAW);
            process_activity(dummy);
            REQUIRE(dummy.activity->id() == ACT_NULL);

            THEN("furniture gets converted to new furniture type") {
                CHECK(mp.furn(tripoint_bub_ms::zero()) == furn_t_test_f_hacksaw1);
            }
        }


        GIVEN("a tripoint_bub_ms with a valid furniture with byproducts") {
            clear_map();
            clear_avatar();

            mp.ter_set(tripoint_bub_ms::zero(), ter_test_t_hacksaw2);
            REQUIRE(mp.ter(tripoint_bub_ms::zero()) == ter_test_t_hacksaw2);

            item& hacksaw = setup_dummy();
            setup_activity(hacksaw);

            REQUIRE(ter_test_t_hacksaw2->hacksaw->byproducts().size() == 2);

            REQUIRE(dummy.activity->id() == ACT_HACKSAW);
            process_activity(dummy);
            REQUIRE(dummy.activity->id() == ACT_NULL);

            const itype_id test_amount("test_rock");
            const itype_id test_random("test_2x4");

            WHEN("hacksaw acitivy finishes") {
                CHECK(dummy.activity->id() == ACT_NULL);

                THEN("player receives the items") {
                    int count_amount = 0;
                    int count_random = 0;
                    for (const auto& it : get_map().i_at(tripoint_bub_ms::zero())) {
                        // can't use switch here
                        const itype_id it_id = it->typeId();
                        if (it_id == test_amount) {
                            count_amount += it->charges;
                        } else if (it_id == test_random) {
                            count_random += 1;
                        }
                    }

                    CHECK(count_amount == 3);
                    CHECK((7 <= count_random && count_random <= 9));
                }
            }
        }
    }
}

TEST_CASE("oxytorch", "[activity][oxytorch]") {
    map& mp = get_map();
    avatar& dummy = get_avatar();

    auto setup_dummy = [&dummy]() -> item& {
        item& torch = dummy.i_add(item::spawn(itype_test_oxytorch));
        torch.ammo_set(itype_oxyacetylene, -1);
        dummy.wield(torch);

        REQUIRE(dummy.primary_weapon().typeId() == itype_test_oxytorch);
        REQUIRE(dummy.max_quality(qual_WELD) == 10);

        return torch;
    };

    auto setup_activity = [&dummy](item& torch) -> void {
        auto act = std::make_unique<
            oxytorch_activity_actor>(tripoint_bub_ms::zero(), safe_reference<item>(torch));
        act->testing = true;
        dummy.assign_activity(std::make_unique<player_activity>(std::move(act)));
    };

    SECTION("oxytorch start checks") {
        GIVEN("a tripoint_bub_ms with nothing") {
            clear_map();
            clear_avatar();

            mp.ter_set(tripoint_bub_ms::zero(), t_null);
            REQUIRE(mp.ter(tripoint_bub_ms::zero()) == t_null);

            item& welding_torch = setup_dummy();
            setup_activity(welding_torch);

            THEN("oxytorch activity can't start") { CHECK(dummy.activity->id() == ACT_NULL); }
        }

        GIVEN("a tripoint_bub_ms with invalid terrain") {
            clear_map();
            clear_avatar();

            mp.ter_set(tripoint_bub_ms::zero(), t_dirt);
            REQUIRE(mp.ter(tripoint_bub_ms::zero()) == t_dirt);

            item& welding_torch = setup_dummy();
            setup_activity(welding_torch);

            THEN("oxytorch activity can't start") { CHECK(dummy.activity->id() == ACT_NULL); }
        }

        GIVEN("a tripoint_bub_ms with valid terrain") {
            clear_map();
            clear_avatar();

            mp.ter_set(tripoint_bub_ms::zero(), ter_test_t_oxytorch1);
            REQUIRE(mp.ter(tripoint_bub_ms::zero()) == ter_test_t_oxytorch1);

            item& welding_torch = setup_dummy();
            setup_activity(welding_torch);

            THEN("oxytorch activity can start") { CHECK(dummy.activity->id() == ACT_OXYTORCH); }
        }

        GIVEN("a tripoint_bub_ms with valid furniture") {
            clear_map();
            clear_avatar();

            mp.furn_set(tripoint_bub_ms::zero(), furn_t_test_f_oxytorch1);
            REQUIRE(mp.furn(tripoint_bub_ms::zero()) == furn_t_test_f_oxytorch1);

            item& welding_torch = setup_dummy();
            setup_activity(welding_torch);

            THEN("oxytorch activity can start") { CHECK(dummy.activity->id() == ACT_OXYTORCH); }
        }

        GIVEN("a tripoint_bub_ms with valid terrain") {
            clear_map();
            clear_avatar();

            mp.ter_set(tripoint_bub_ms::zero(), ter_test_t_oxytorch1);
            REQUIRE(mp.ter(tripoint_bub_ms::zero()) == ter_test_t_oxytorch1);

            item& welding_torch = setup_dummy();
            setup_activity(welding_torch);
            REQUIRE(dummy.activity->id() == ACT_OXYTORCH);

            WHEN("terrain has a duration of 10 seconds") {
                REQUIRE(ter_test_t_oxytorch1->oxytorch->duration() == 10_seconds);
                THEN("moves_left is equal to 10 seconds") {
                    CHECK(dummy.activity->get_moves_left() == to_moves<int>(10_seconds));
                }
            }
        }

        GIVEN("a tripoint_bub_ms with valid furniture") {
            clear_map();
            clear_avatar();

            mp.furn_set(tripoint_bub_ms::zero(), furn_t_test_f_oxytorch1);
            REQUIRE(mp.furn(tripoint_bub_ms::zero()) == furn_t_test_f_oxytorch1);

            item& welding_torch = setup_dummy();
            setup_activity(welding_torch);
            REQUIRE(dummy.activity->id() == ACT_OXYTORCH);

            WHEN("furniture has a duration of 5 seconds") {
                REQUIRE(furn_t_test_f_oxytorch1->oxytorch->duration() == 5_seconds);
                THEN("moves_left is equal to 5 seconds") {
                    CHECK(dummy.activity->get_moves_left() == to_moves<int>(5_seconds));
                }
            }
        }
    }

    SECTION("oxytorch turn checks") {
        GIVEN("player is in mid activity") {
            clear_map();
            clear_avatar();

            mp.furn_set(tripoint_bub_ms::zero(), furn_t_test_f_oxytorch3);
            REQUIRE(mp.furn(tripoint_bub_ms::zero()) == furn_t_test_f_oxytorch3);

            item& welding_torch = setup_dummy();
            setup_activity(welding_torch);
            REQUIRE(dummy.activity->id() == ACT_OXYTORCH);
            process_activity(dummy);

            WHEN("player runs out of fuel") {
                REQUIRE(dummy.activity->id() == ACT_NULL);

                THEN("player recharges with fuel") {
                    welding_torch.ammo_set(itype_oxyacetylene, -1);

                    AND_THEN("player can resume the activity") {
                        setup_activity(welding_torch);
                        dummy.moves = dummy.get_speed();
                        dummy.activity->do_turn(dummy);
                        CHECK(dummy.activity->id() == ACT_OXYTORCH);
                        CHECK(dummy.activity->get_moves_left()
                              < to_moves<int>(furn_t_test_f_oxytorch3->oxytorch->duration()));
                    }
                }
            }
        }
    }

    SECTION("oxytorch finish checks") {
        GIVEN("a tripoint_bub_ms with valid terrain") {
            clear_map();
            clear_avatar();

            mp.ter_set(tripoint_bub_ms::zero(), ter_test_t_oxytorch1);
            REQUIRE(mp.ter(tripoint_bub_ms::zero()) == ter_test_t_oxytorch1);

            item& welding_torch = setup_dummy();
            setup_activity(welding_torch);

            REQUIRE(dummy.activity->id() == ACT_OXYTORCH);
            process_activity(dummy);
            REQUIRE(dummy.activity->id() == ACT_NULL);

            THEN("terrain gets converted to new terrain type") {
                CHECK(mp.ter(tripoint_bub_ms::zero()) == t_dirt);
            }
        }

        GIVEN("a tripoint_bub_ms with valid furniture") {
            clear_map();
            clear_avatar();

            mp.furn_set(tripoint_bub_ms::zero(), furn_t_test_f_oxytorch1);
            REQUIRE(mp.furn(tripoint_bub_ms::zero()) == furn_t_test_f_oxytorch1);

            item& welding_torch = setup_dummy();
            setup_activity(welding_torch);

            REQUIRE(dummy.activity->id() == ACT_OXYTORCH);
            process_activity(dummy);
            REQUIRE(dummy.activity->id() == ACT_NULL);

            THEN("furniture gets converted to new furniture type") {
                CHECK(mp.furn(tripoint_bub_ms::zero()) == f_null);
            }
        }

        GIVEN("a tripoint_bub_ms with valid furniture") {
            clear_map();
            clear_avatar();

            mp.furn_set(tripoint_bub_ms::zero(), furn_t_test_f_oxytorch2);
            REQUIRE(mp.furn(tripoint_bub_ms::zero()) == furn_t_test_f_oxytorch2);

            item& welding_torch = setup_dummy();
            setup_activity(welding_torch);

            REQUIRE(dummy.activity->id() == ACT_OXYTORCH);
            process_activity(dummy);
            REQUIRE(dummy.activity->id() == ACT_NULL);

            THEN("furniture gets converted to new furniture type") {
                CHECK(mp.furn(tripoint_bub_ms::zero()) == furn_t_test_f_oxytorch1);
            }
        }


        GIVEN("a tripoint_bub_ms with a valid furniture with byproducts") {
            clear_map();
            clear_avatar();

            mp.ter_set(tripoint_bub_ms::zero(), ter_test_t_oxytorch2);
            REQUIRE(mp.ter(tripoint_bub_ms::zero()) == ter_test_t_oxytorch2);

            item& welding_torch = setup_dummy();
            setup_activity(welding_torch);

            REQUIRE(ter_test_t_oxytorch2->oxytorch->byproducts().size() == 2);

            REQUIRE(dummy.activity->id() == ACT_OXYTORCH);
            process_activity(dummy);
            REQUIRE(dummy.activity->id() == ACT_NULL);

            const itype_id test_amount("test_rock");
            const itype_id test_random("test_2x4");

            WHEN("oxytorch acitivy finishes") {
                CHECK(dummy.activity->id() == ACT_NULL);

                THEN("player receives the items") {
                    int count_amount = 0;
                    int count_random = 0;
                    for (const auto& it : get_map().i_at(tripoint_bub_ms::zero())) {
                        // can't use switch here
                        const itype_id it_id = it->typeId();
                        if (it_id == test_amount) {
                            count_amount += it->charges;
                        } else if (it_id == test_random) {
                            count_random += 1;
                        }
                    }

                    CHECK(count_amount == 3);
                    CHECK((7 <= count_random && count_random <= 9));
                }
            }
        }
    }
}

// CDF: E1-5b. DDA tests/player_activities_test.cpp:1736-1940 (b4b62254ae2), the `edevice` test
// case, transplanted. Differences, all forced by CDF:
//  - item_location becomes item* (CDF has no item_location, the actor keeps safe_references).
//  - Catch2 tags a whole test case ([!shouldfail]), not a section: E1-5b split the sections that
//    need E1-5c features (port compatibility with the slow and the external-transfer rate, the
//    recipe catalog merge) into test cases of their own with [!shouldfail]; E1-5c removed the
//    marks, the cases stay separate.
//  - The DDA memory_card (external transfer device) is the TEST_DATA item test_efile_etd.
//  - process_activity( dummy, true ) of the DDA test is a copy with a turn limit, so a broken
//    state machine fails the test instead of hanging the run.
namespace
{
struct edevice_scene {
    avatar& dummy;
    std::vector<item*> edevice_locs;
    std::vector<item*> efile_locs;
    std::vector<item*> copiable_efile_locs;
    item* laptop_with_files = nullptr;
    item* edevice_without_files = nullptr;
    std::vector<item*> vector_laptop_with_files;
    std::vector<item*> vector_edevice_without_files;

    static auto prepared_avatar() -> avatar& {
        clear_all_state();
        // a ten-minute boundary: the battery rule is one charge per 4 minutes
        set_time(calendar::turn_zero + 12_hours);
        avatar& you = get_avatar();
        clear_avatar();
        you.set_skill_level(skill_computer, 1);
        return you;
    }

    edevice_scene() : dummy(prepared_avatar()) {}

    // DDA process_activity( dummy, pass_time ) with a turn limit
    void run(const bool pass_time = true, const int max_turns = 40000) {
        int turns = 0;
        while (!dummy.activity->is_null() && turns < max_turns) {
            dummy.moves += dummy.get_speed();
            while (dummy.moves > 0 && !dummy.activity->is_null() && turns < max_turns) {
                dummy.activity->do_turn(dummy);
                if (pass_time) {
                    calendar::turn += 1_seconds;
                }
                ++turns;
            }
        }
        REQUIRE(dummy.activity->is_null());
    }

    void update_efiles() {
        efile_locs.clear();
        copiable_efile_locs.clear();
        for (item* edevice : edevice_locs) {
            for (item* efile : edevice->efiles()) {
                efile_locs.push_back(efile);
            }
        }
        for (item* loc : efile_locs) {
            if (loc->is_ecopiable()) {
                copiable_efile_locs.push_back(loc);
            }
        }
    }

    void assign(item* used_edevice, const std::vector<item*>& target_edevices,
                const std::vector<item*>& selected_efiles, const efile_action action,
                const efile_combo combo = efile_combo::COMBO_NONE) {
        dummy.assign_activity(std::make_unique<player_activity>(
            std::make_unique<efile_activity_actor>(used_edevice, target_edevices, selected_efiles,
                                                   action, combo)));
    }

    void do_activity(item* used_edevice, const std::vector<item*>& target_edevices,
                     const std::vector<item*>& selected_efiles, const efile_action action,
                     const bool pass_time = true) {
        assign(used_edevice, target_edevices, selected_efiles, action);
        run(pass_time);
    }

    void add_edevices(const item_group_id& igroup, const bool add_etd = true) {
        clear_avatar();
        dummy.set_skill_level(skill_computer, 1);
        detached_ptr<item> rejected = dummy.wear_item(item::spawn(itype_test_backpack));
        REQUIRE(!rejected);
        edevice_locs.clear();
        for (detached_ptr<item>& i : item_group::items_from(igroup)) {
            REQUIRE(!i->is_browsed());
            edevice_locs.push_back(&dummy.i_add(std::move(i)));
        }
        // every test item group is only two devices; the first one with files is the laptop
        REQUIRE(edevice_locs.size() == 2);
        laptop_with_files = edevice_locs.front();
        for (item* edevice : edevice_locs) {
            if (!edevice->efiles().empty()) {
                laptop_with_files = edevice;
                break;
            }
        }
        edevice_without_files = laptop_with_files == edevice_locs.front() ? edevice_locs.back()
                                                                         : edevice_locs.front();
        vector_laptop_with_files = {laptop_with_files};
        vector_edevice_without_files = {edevice_without_files};
        for (item* edevice : edevice_locs) {
            // the group's charges reach the battery well
            REQUIRE(edevice->ammo_remaining() > 0);
        }
        if (add_etd) {
            item& etd = dummy.i_add(item::spawn(itype_test_efile_etd));
            etd.set_browsed(true);
        }

        update_efiles();
        // browse required before any further efile operations
        do_activity(laptop_with_files, edevice_locs, efile_locs, EF_BROWSE);
        for (item* i : edevice_locs) {
            REQUIRE(i->is_browsed());
        }
    }
};
} // namespace

TEST_CASE("edevice", "[activity][edevice]") {
    edevice_scene scene;
    avatar& dummy = scene.dummy;

    // CDF: item::get_item_with skips the E_FILE pocket, the files are listed by efiles()
    const auto find_copied_efile = [](item& edevice) -> item* {
        for (item* efile : edevice.efiles()) {
            if (efile->typeId() == itype_test_efile_copiable) {
                return efile;
            }
        }
        return nullptr;
    };

    SECTION("move to / move from") {
        scene.add_edevices(Item_spawn_data_test_edevices_standard);
        REQUIRE(scene.laptop_with_files->efiles().size() == 3);
        REQUIRE(scene.edevice_without_files->efiles().empty());
        scene.do_activity(scene.edevice_without_files, scene.vector_laptop_with_files,
                          scene.efile_locs, EF_MOVE_ONTO_THIS);
        REQUIRE(scene.edevice_without_files->efiles().size() == 3);
        REQUIRE(scene.laptop_with_files->efiles().empty());
        scene.update_efiles();
        REQUIRE(scene.efile_locs.size() == 3);
        REQUIRE(scene.efile_locs.front() != nullptr);
        scene.do_activity(scene.edevice_without_files, scene.vector_laptop_with_files,
                          scene.efile_locs, EF_MOVE_FROM_THIS);
        REQUIRE(scene.laptop_with_files->efiles().size() == 3);
        REQUIRE(scene.edevice_without_files->efiles().empty());
    }
    SECTION("copy onto") {
        scene.add_edevices(Item_spawn_data_test_edevices_standard);
        scene.do_activity(scene.edevice_without_files, scene.vector_laptop_with_files,
                          scene.copiable_efile_locs, EF_COPY_ONTO_THIS);
        REQUIRE(scene.laptop_with_files->efiles().size() == 3);
        REQUIRE(scene.edevice_without_files->efiles().size() == 1);
        REQUIRE(scene.edevice_without_files->efiles().front()->typeId() ==
                itype_test_efile_copiable);
        item* copied_efile = find_copied_efile(*scene.laptop_with_files);
        REQUIRE(copied_efile != nullptr);
        REQUIRE(copied_efile->typeId() == itype_test_efile_copiable);
    }
    SECTION("copy from") {
        scene.add_edevices(Item_spawn_data_test_edevices_standard);
        scene.do_activity(scene.laptop_with_files, scene.vector_edevice_without_files,
                          scene.copiable_efile_locs, EF_COPY_FROM_THIS);
        REQUIRE(scene.laptop_with_files->efiles().size() == 3);
        REQUIRE(scene.edevice_without_files->efiles().size() == 1);
        REQUIRE(scene.edevice_without_files->efiles().front()->typeId() ==
                itype_test_efile_copiable);
        item* copied_efile = find_copied_efile(*scene.laptop_with_files);
        REQUIRE(copied_efile != nullptr);
        REQUIRE(copied_efile->typeId() == itype_test_efile_copiable);
    }
    SECTION("wipe") {
        scene.add_edevices(Item_spawn_data_test_edevices_standard);
        scene.do_activity(scene.edevice_without_files, scene.vector_laptop_with_files,
                          scene.efile_locs, EF_WIPE);
        REQUIRE(scene.edevice_without_files->efiles().empty());
        REQUIRE(scene.laptop_with_files->efiles().empty());
    }
    SECTION("empty parameters test") {
        scene.efile_locs.clear();
        const std::vector<item*> vector_empty_loc;
        scene.assign(nullptr, vector_empty_loc, scene.efile_locs, EF_MOVE_ONTO_THIS);
        dummy.activity->do_turn(dummy);
        REQUIRE(dummy.activity->is_null());
    }
    // power tests
    SECTION("move to used edevice runs out of power") {
        scene.add_edevices(Item_spawn_data_test_edevices_power);
        REQUIRE(scene.edevice_without_files->ammo_remaining() == 10);
        scene.do_activity(scene.edevice_without_files, scene.vector_laptop_with_files,
                          scene.efile_locs, EF_MOVE_ONTO_THIS);
        // the operation failed and the file has not been moved
        REQUIRE(scene.edevice_without_files->efiles().empty());
        REQUIRE(scene.laptop_with_files->efiles().size() == 1);
    }
    SECTION("move from target edevice runs out of power") {
        scene.add_edevices(Item_spawn_data_test_edevices_power);
        REQUIRE(scene.edevice_without_files != nullptr);
        REQUIRE(scene.edevice_without_files->ammo_remaining() == 10);
        scene.do_activity(scene.laptop_with_files, scene.vector_edevice_without_files,
                          scene.efile_locs, EF_MOVE_FROM_THIS);
        // the operation fails and the file has not been moved
        REQUIRE(scene.edevice_without_files->efiles().empty());
        REQUIRE(scene.laptop_with_files->efiles().size() == 1);
    }
    SECTION("move to used edevice disappears") {
        scene.add_edevices(Item_spawn_data_test_edevices_power);
        REQUIRE(scene.edevice_without_files != nullptr);
        scene.assign(scene.edevice_without_files, scene.vector_laptop_with_files, scene.efile_locs,
                     EF_MOVE_ONTO_THIS);
        for (int i = 0; i < 20; i++) {
            calendar::turn += 1_seconds;
            dummy.activity->do_turn(dummy);
        }
        {
            detached_ptr<item> gone = scene.edevice_without_files->detach();
        }
        dummy.activity->do_turn(dummy);
        // the operation immediately fails
        REQUIRE(dummy.activity->is_null());
        // the operation failed and the file has not been moved
        REQUIRE(scene.laptop_with_files->efiles().size() == 1);
    }
    SECTION("move from target edevice disappears") {
        scene.add_edevices(Item_spawn_data_test_edevices_power);
        scene.assign(scene.laptop_with_files, scene.vector_edevice_without_files, scene.efile_locs,
                     EF_MOVE_FROM_THIS);
        for (int i = 0; i < 20; i++) {
            calendar::turn += 1_seconds;
            dummy.activity->do_turn(dummy);
        }
        {
            detached_ptr<item> gone = scene.edevice_without_files->detach();
        }
        scene.run(false);
        // the operation fails and the file has not been moved
        REQUIRE(scene.laptop_with_files->efiles().size() == 1);
    }
    SECTION("fast move between compatible devices") {
        scene.add_edevices(Item_spawn_data_test_edevices_compat);
        time_point before = calendar::turn;
        scene.do_activity(scene.laptop_with_files, scene.vector_edevice_without_files,
                          scene.efile_locs, EF_MOVE_FROM_THIS);
        REQUIRE(calendar::turn - (before + 1_seconds) ==
                810_seconds); // 24GB / 30MB per sec + 10 second boot
    }
}

// CDF: E1-5c (port compatibility: DDA activity_actor.cpp:4187-4216, the slow 1 MB/s rate of the
// same-port case).
TEST_CASE("edevice_slow_move_between_incompatible_devices", "[activity][edevice]") {
    edevice_scene scene;
    scene.add_edevices(Item_spawn_data_test_edevices_incompat, false);
    time_point before = calendar::turn;
    int battery_start = scene.laptop_with_files->ammo_remaining();
    scene.do_activity(scene.laptop_with_files, scene.vector_edevice_without_files,
                      scene.efile_locs, EF_MOVE_FROM_THIS);
    REQUIRE(battery_start - scene.laptop_with_files->ammo_remaining() ==
            100); // 400 minutes / 1 charge per 4 min
    REQUIRE(calendar::turn - (before + 1_seconds) == 24010_seconds); // 24GB / 1MB per sec
}

// CDF: E1-5c (external transfer storage, DDA activity_actor.cpp:4156-4180).
TEST_CASE("edevice_fast_move_between_incompatible_devices_with_etd", "[activity][edevice]") {
    edevice_scene scene;
    scene.add_edevices(Item_spawn_data_test_edevices_incompat);
    time_point before = calendar::turn;
    scene.do_activity(scene.laptop_with_files, scene.vector_edevice_without_files,
                      scene.efile_locs, EF_MOVE_FROM_THIS);
    REQUIRE(calendar::turn - (before + 1_seconds) == 4010_seconds); // 24GB / (12 / 2)MB per sec
}

// CDF: E1-5c (merging the recipe catalog into the target's catalog on a move, DDA
// activity_actor.cpp:4036-4045). The first check (one catalog file on the target, not two) is
// added by CDF; DDA only compares the recipe counts.
TEST_CASE("edevice_recipe_combination", "[activity][edevice]") {
    edevice_scene scene;
    scene.add_edevices(Item_spawn_data_test_edevices_recipes);
    std::set<recipe_id> recipes1 = scene.laptop_with_files->get_saved_recipes();
    std::set<recipe_id> recipes2 = scene.edevice_without_files->get_saved_recipes();
    std::set<recipe_id> combined = recipes1;
    for (const recipe_id& recipe : recipes2) {
        combined.insert(recipe);
    }
    REQUIRE(!recipes1.empty());
    REQUIRE(!recipes2.empty());
    const std::vector<item*> lwf_locs = scene.laptop_with_files->efiles();
    scene.do_activity(scene.laptop_with_files, scene.vector_edevice_without_files, lwf_locs,
                      EF_MOVE_FROM_THIS);
    REQUIRE(scene.edevice_without_files->efiles().size() == 1);
    REQUIRE(scene.laptop_with_files->get_saved_recipes().empty());
    REQUIRE(combined.size() == scene.edevice_without_files->get_saved_recipes().size());
}
