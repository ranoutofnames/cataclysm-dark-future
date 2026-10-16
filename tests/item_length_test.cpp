#include "assign.h"
#include "catch/catch.hpp"
#include "item.h"
#include "item_factory.h"
#include "item_pocket.h"
#include "itype.h"
#include "json.h"
#include "options_helpers.h"
#include "pocket_data.h"
#include "string_formatter.h"
#include "units.h"
#include "units_serde.h"
#include "units_utility.h"

#include <sstream>
#include <string>

namespace {

auto parse_length(const std::string& json) -> units::length {
    std::istringstream buffer(json);
    JsonIn jsin(buffer);
    return read_from_json_string<units::length>(jsin, units::length_units);
}

auto assign_length(const std::string& json) -> units::length {
    std::istringstream buffer(json);
    JsonIn jsin(buffer);
    JsonObject jo = jsin.get_object();
    units::length result = 7_cm;
    assign(jo, "len", result);
    jo.allow_omitted_members();
    return result;
}

} // namespace

TEST_CASE("units::length literals and arithmetic", "[item][length][units]") {
    CHECK(1_cm == 10_mm);
    CHECK(1_m == 100_cm);
    CHECK(1_km == 1000_m);
    CHECK(1.5_cm == 15.0_mm);
    CHECK(units::to_millimeter(2_cm) == 20);
    CHECK(units::to_centimeter(2_m) == 200);
    CHECK(units::to_meter(3_km) == 3000);
    CHECK(units::to_kilometer(3_km) == 3);
    CHECK(10_cm + 5_cm == 15_cm);
    CHECK(10_cm - 5_cm == 5_cm);
    CHECK(10_cm * 3 == 30_cm);
    CHECK(10_cm < 1_m);
    CHECK(std::max(10_cm, 1_m) == 1_m);
}

TEST_CASE("units::length default from volume is the cube side", "[item][length][units]") {
    CHECK(units::default_length_from_volume(1_liter) == 10_cm);
    CHECK(units::default_length_from_volume(8_liter) == 20_cm);
    CHECK(units::default_length_from_volume(250_ml) == 6_cm);
    CHECK(units::default_length_from_volume(0_ml) == 0_cm);
    CHECK(units::default_length_from_volume(1000_liter) == 1_m);
}

TEST_CASE("units::length parsing from JSON", "[item][length][units]") {
    CHECK_THROWS(parse_length("\"\""));
    CHECK_THROWS(parse_length("27"));
    CHECK_THROWS(parse_length("\"    \""));
    CHECK_THROWS(parse_length("\"27\""));
    CHECK_THROWS(parse_length("\"27 parsecs\""));

    CHECK(parse_length("\"40 cm\"") == 40_cm);
    CHECK(parse_length("\"5 mm\"") == 5_mm);
    CHECK(parse_length("\"2 m\"") == 2_m);
    CHECK(parse_length("\"2 meter\"") == 2_m);
    CHECK(parse_length("\"3 km\"") == 3_km);
    CHECK(parse_length("\"+1 m 20 cm\"") == 120_cm);
    CHECK(assign_length("{ \"len\": \"84 cm\" }") == 84_cm);
    CHECK(assign_length("{ \"other\": \"84 cm\" }") == 7_cm);
}

TEST_CASE("units::length JSON round trip", "[item][length][units]") {
    for (const units::length l : {0_mm, 5_mm, 40_cm, 2_m, 3_km}) {
        std::ostringstream out;
        JsonOut jsout(out);
        jsout.write(l);
        CHECK(parse_length(out.str()) == l);
    }
}

TEST_CASE("units::length display", "[item][length][units]") {
    SECTION("metric") {
        override_option opt("DISTANCE_UNITS", "metric");
        CHECK(string_format_length(84_cm) == "84 cm");
        CHECK(string_format_length(84_cm, true) == "84cm");
        CHECK(string_format_length(2_m) == "2 m");
        CHECK(string_format_length(5_mm) == "5 mm");
        CHECK(string_format_length(150_cm) == "150 cm");
        CHECK(convert_length(84_cm) == 84);
        CHECK(length_units(2_m) == "m");
        CHECK(convert_length_cm_in(84_cm) == Approx(84.0));
    }
    SECTION("imperial") {
        override_option opt("DISTANCE_UNITS", "imperial");
        CHECK(string_format_length(254_mm) == "10 in");
        CHECK(string_format_length(914_mm) == "1 yd");
        CHECK(convert_length_cm_in(254_mm) == Approx(10.0));
        CHECK(length_units(254_mm) == "in.");
    }
}

TEST_CASE("item length resolution", "[item][length]") {
    // category floor (rifle skill) or overlay: an assault rifle is long
    CHECK(itype_id("m4a1")->longest_side >= 80_cm);
    // overlay table
    CHECK(itype_id("2x4")->longest_side == 122_cm);
    // derived from volume
    const itype& jar = *itype_id("jar_glass");
    CHECK(jar.longest_side == units::default_length_from_volume(jar.volume));
    CHECK(item::spawn_temporary(itype_id("2x4"))->length() == 122_cm);
}

TEST_CASE("derived item length never exceeds the default pocket limit", "[item][length]") {
    int checked = 0;
    for (const itype* t : item_controller->all()) {
        const units::volume eff =
            t->count_by_charges() && t->stack_size > 0 ? t->volume / t->stack_size : t->volume;
        if (t->longest_side != units::default_length_from_volume(eff)) {
            continue; // overlay or floor: allowed to be refused
        }
        for (const itype* c : item_controller->all()) {
            for (const pocket_data& pd : c->pockets) {
                if (pd.type != pocket_type::CONTAINER || pd.holster) {
                    continue;
                }
                if (t->volume <= pd.raw_volume_capacity) {
                    // DDA: the loader default is cbrt( capacity ) * sqrt( 2 ), or the explicit
                    // value
                    if (pd.max_item_length <= 0_mm) { continue; }
                    // CDF: DDA data states explicit limits (a 1 mm liquid-only hat pocket); only
                    // the loader-derived default is promised to fit every derived length.
                    pocket_data derived = pd;
                    derived.apply_default_length();
                    if (derived.max_item_length != pd.max_item_length) { continue; }
                    CAPTURE(t->get_id(), c->get_id(), t->longest_side, pd.max_item_length);
                    REQUIRE(t->longest_side <= pd.max_item_length);
                    ++checked;
                }
            }
        }
    }
    CHECK(checked > 0);
}

TEST_CASE("long items are refused by short containers", "[item][length][pocket]") {
    item& jar = *item::spawn_temporary(itype_id("jar_3l_glass"));
    item& plank = *item::spawn_temporary(itype_id("2x4"));
    const auto res = jar.contents.can_contain(plank, pocket_type::CONTAINER);
    REQUIRE_FALSE(res.success());
    CHECK(res.value() == item_pocket::contain_code::ERR_TOO_BIG);
    CHECK(res.str().find("too long") != std::string::npos);
}

TEST_CASE("magazine wells have an explicit length limit", "[item][length][pocket]") {
    // DDA item_factory.cpp:4086: magazine wells get 2 km, so no item is too long for them
    bool saw_well = false;
    for (const itype* t : item_controller->all()) {
        for (const pocket_data& pd : t->pockets) {
            if (pd.type == pocket_type::MAGAZINE_WELL) {
                CHECK(pd.max_item_length >= 2_km);
                saw_well = true;
            }
        }
    }
    CHECK(saw_well);
}
