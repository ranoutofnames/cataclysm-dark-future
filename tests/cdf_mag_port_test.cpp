#include "catch/catch.hpp"
#include "json.h"

#include <fstream>
#include <set>
#include <string>

namespace {

const std::string P3_GOLDEN_FILENAME = "tests/data/dda_staged/p3_golden.json";

} // namespace

TEST_CASE("p3 golden file has format 1, the DDA hash and 1171 items", "[p3][golden]") {
    std::ifstream file(P3_GOLDEN_FILENAME, std::ios::binary);
    JsonIn jsin(file);
    JsonObject golden = jsin.get_object();

    REQUIRE(golden.get_int("format") == 1);
    REQUIRE(golden.get_string("dda") == "918f396f370ea48fad924fb45a797386a2f6ddb4");
    JsonObject items = golden.get_object("items");
    items.allow_omitted_members();
    REQUIRE(items.size() == 1171);
}

TEST_CASE("p3 golden items have a known class and action", "[p3][golden]") {
    std::ifstream file(P3_GOLDEN_FILENAME, std::ios::binary);
    JsonIn jsin(file);
    JsonObject golden = jsin.get_object();
    JsonObject items = golden.get_object("items");

    const std::set<std::string> known_classes =
        {"MAG", "GUN-INT", "TOOL-INT", "WELL", "CHARGES", "ADAPTOR", "DDA-POCKET"};
    const std::set<std::string> known_actions =
        {"COPY-TWIN",
         "COPY-TWIN-KEEP-CDF-AMMO",
         "AUTHOR",
         "AUTHOR-KEEP-CDF-WELL",
         "AUTHOR-KEEP-CDF-INTEGRAL",
         "HAND-FALLBACK",
         "KEEP-REVIEW"};

    for (const JsonMember member : items) {
        JsonObject item = member.get_object();
        item.allow_omitted_members();
        REQUIRE(known_classes.count(item.get_string("class")) == 1);
        REQUIRE(known_actions.count(item.get_string("action")) == 1);
    }
}

TEST_CASE("p3 golden: only ADAPTOR rows carry an adaptor block", "[p3][golden]") {
    std::ifstream file(P3_GOLDEN_FILENAME, std::ios::binary);
    JsonIn jsin(file);
    JsonObject golden = jsin.get_object();
    JsonObject items = golden.get_object("items");

    size_t adaptor_rows = 0;
    for (const JsonMember member : items) {
        JsonObject item = member.get_object();
        item.allow_omitted_members();
        const bool has_adaptor = item.has_member("adaptor");
        if (item.get_string("action") == "KEEP-REVIEW") {
            REQUIRE(item.get_string("class") == "ADAPTOR");
            REQUIRE(has_adaptor);
        } else {
            REQUIRE_FALSE(has_adaptor);
        }
        if (has_adaptor) { adaptor_rows++; }
    }
    REQUIRE(adaptor_rows == 6);
}
