#include "catch/catch.hpp"
#include "item.h"
#include "itype.h"
#include "json.h"

#include <sstream>
#include <string>

namespace {

template <typename T>
auto load_struct(const std::string& json) -> T {
    std::istringstream buffer(json);
    JsonIn jsin(buffer);
    JsonObject jo = jsin.get_object();
    T out;
    out.deserialize(jo);
    return out;
}

auto load_firing_requirements(const std::string& json) -> firing_requirement_set {
    std::istringstream buffer(json);
    JsonIn jsin(buffer);
    JsonObject jo = jsin.get_object();
    firing_requirement_set out;
    out.deserialize_firing_requirements(jo, "fr");
    return out;
}

auto load_consumption_per_use(const std::string& json) -> firing_requirement_set {
    std::istringstream buffer(json);
    JsonIn jsin(buffer);
    JsonObject jo = jsin.get_object();
    firing_requirement_set out;
    out.deserialize_consumption_per_use(jo, "cpu");
    return out;
}

} // namespace

TEST_CASE("pocket_consumption_entry loads DDA-format JSON", "[p3][loader]") {
    const pocket_consumption_entry entry =
        load_struct<pocket_consumption_entry>(R"({"pocket":"mag","qty":2})");
    CHECK(entry.pocket == "mag");
    CHECK(entry.qty == 2);

    CHECK_THROWS_AS(load_struct<pocket_consumption_entry>(R"({"pocket":"mag","qty":0})"),
                    JsonError);
    CHECK_THROWS_AS(load_struct<pocket_consumption_entry>(R"({"pocket":"mag","qty":-1})"),
                    JsonError);
    CHECK_THROWS_AS(load_struct<pocket_consumption_entry>(R"({"qty":2})"), JsonError);
    CHECK_THROWS_AS(load_struct<pocket_consumption_entry>(R"({"pocket":"mag"})"), JsonError);
}

TEST_CASE("pocket_consumption_mod loads DDA-format JSON", "[p3][loader]") {
    const pocket_consumption_mod mod =
        load_struct<pocket_consumption_mod>(R"({"pocket":"mag","set":3,"multiply":0.5})");
    CHECK(mod.pocket == "mag");
    REQUIRE(mod.set.has_value());
    CHECK(*mod.set == 3);
    CHECK(mod.multiply == Approx(0.5f));

    const pocket_consumption_mod plain =
        load_struct<pocket_consumption_mod>(R"({"pocket":"mag"})");
    CHECK_FALSE(plain.set.has_value());
    CHECK(plain.multiply == Approx(1.0f));

    CHECK_THROWS_AS(load_struct<pocket_consumption_mod>(R"({"pocket":"mag","set":0})"),
                    JsonError);
    CHECK_THROWS_AS(load_struct<pocket_consumption_mod>(R"({"pocket":"mag","multiply":0})"),
                    JsonError);
    CHECK_THROWS_AS(load_struct<pocket_consumption_mod>(R"({"pocket":"mag","multiply":-1})"),
                    JsonError);
}

TEST_CASE("pocket_capacity_mod loads DDA-format JSON", "[p3][loader]") {
    const pocket_capacity_mod mod =
        load_struct<pocket_capacity_mod>(R"({"pocket":"mag","multiply":2})");
    CHECK(mod.pocket == "mag");
    CHECK(mod.multiply == Approx(2.0f));

    const pocket_capacity_mod plain =
        load_struct<pocket_capacity_mod>(R"({"pocket":"mag"})");
    CHECK(plain.multiply == Approx(1.0f));

    CHECK_THROWS_AS(load_struct<pocket_capacity_mod>(R"({"pocket":"mag","multiply":0})"),
                    JsonError);
    CHECK_THROWS_AS(load_struct<pocket_capacity_mod>(R"({"pocket":"mag","multiply":-1})"),
                    JsonError);
}

TEST_CASE("firing_requirement_set deserialize_firing_requirements", "[p3][loader]") {
    const firing_requirement_set fr = load_firing_requirements(
        R"({"fr":{"DEFAULT":[{"pocket":"a","qty":1}],"AUTO":[{"pocket":"a","qty":3},{"pocket":"b","qty":1}]}})");
    CHECK(fr.per_mode.size() == 2);
    REQUIRE(fr.for_mode(gun_mode_id("AUTO")) != nullptr);
    CHECK(fr.for_mode(gun_mode_id("AUTO"))->size() == 2);
    CHECK(fr.for_mode(gun_mode_id("BURST")) == nullptr);
    CHECK_FALSE(fr.empty());

    const firing_requirement_set commented = load_firing_requirements(
        R"({"fr":{"DEFAULT":[{"pocket":"a","qty":1}],"//":"x"}})");
    CHECK(commented.per_mode.size() == 1);

    CHECK_THROWS_AS(load_firing_requirements(R"({"fr":{}})"), JsonError);
    CHECK_THROWS_AS(load_firing_requirements(R"({"fr":{"//":"x"}})"), JsonError);
    CHECK_THROWS_AS(load_firing_requirements(R"({"fr":{"DEFAULT":[]}})"), JsonError);
    CHECK_THROWS_AS(load_firing_requirements(
                        R"({"fr":{"DEFAULT":{"pocket":"a","qty":1}}})"),
                    JsonError);
}

TEST_CASE("firing_requirement_set deserialize_consumption_per_use", "[p3][loader]") {
    const firing_requirement_set fr =
        load_consumption_per_use(R"({"cpu":[{"pocket":"a","qty":2}]})");
    REQUIRE(fr.for_mode(gun_mode_id("DEFAULT")) != nullptr);
    CHECK(fr.for_mode(gun_mode_id("DEFAULT"))->front().qty == 2);
}

// CDF: cdf_loader_p3_test.cpp has no Item_factory harness, so per the 1-b brief this checks the
// itype-level member indirectly: build an itype through the 1-a helpers with the same member
// names load_basic_info passes, and assert nothing is lost.
TEST_CASE("itype firing_requirements member", "[p3][loader]") {
    const itype bare;
    CHECK(bare.firing_requirements.empty());

    itype gun;
    {
        std::istringstream buffer(
            R"({"firing_requirements":{"DEFAULT":[{"pocket":"a","qty":1}]}})");
        JsonIn jsin(buffer);
        JsonObject jo = jsin.get_object();
        gun.firing_requirements.deserialize_firing_requirements(jo, "firing_requirements");
    }
    REQUIRE(gun.firing_requirements.for_mode(gun_mode_id("DEFAULT")) != nullptr);
    CHECK(gun.firing_requirements.for_mode(gun_mode_id("DEFAULT"))->size() == 1);
    CHECK_FALSE(gun.firing_requirements.empty());

    itype tool;
    {
        std::istringstream buffer(
            R"({"consumption_per_use":[{"pocket":"a","qty":2}]})");
        JsonIn jsin(buffer);
        JsonObject jo = jsin.get_object();
        tool.firing_requirements.deserialize_consumption_per_use(jo, "consumption_per_use");
    }
    REQUIRE(tool.firing_requirements.for_mode(gun_mode_id("DEFAULT")) != nullptr);
    CHECK(tool.firing_requirements.for_mode(gun_mode_id("DEFAULT"))->front().qty == 2);

    CHECK_THROWS_AS(load_consumption_per_use(R"({"cpu":[{"pocket":"a","qty":0}]})"),
                    JsonError);
}

TEST_CASE("magazine mag_jam_mult default", "[p3][loader]") {
    CHECK(item("stanag30").type->magazine->mag_jam_mult == 1.0);
}
