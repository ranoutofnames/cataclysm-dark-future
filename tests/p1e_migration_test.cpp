#include "catch/catch.hpp"

#include <string>
#include <vector>

#include "calendar.h"
#include "debug.h"
#include "fstream_utils.h"
#include "item.h"
#include "item_factory.h"
#include "item_group.h"
#include "json.h"
#include "profession.h"
#include "state_helpers.h"

namespace {

// Every MIGRATION entry in the obsoletion files shipped in data/json.
auto shipped_migration_ids() -> std::vector<std::string>
{
    std::vector<std::string> ids;
    for (const char *path : {"data/json/obsoletion/migration.json",
                             "data/json/obsoletion/explosives.json",
                             "data/json/obsoletion/cdf_vest_migration.json"
                            }) {
        read_from_file_json(path, [&ids](JsonIn & jsin) {
            JsonArray ja = jsin.get_array();
            while (ja.has_more()) {
                JsonObject jo = ja.next_object();
                if (jo.get_string("type") == "MIGRATION") {
                    // "id" may be a single id or an array of ids
                    if (jo.has_array("id")) {
                        for (const std::string &arr_id : jo.get_string_array("id")) {
                            ids.push_back(arr_id);
                        }
                    } else {
                        ids.push_back(jo.get_string("id"));
                    }
                }
            }
        });
    }
    return ids;
}

} // namespace

TEST_CASE("every shipped MIGRATION id spawns its replacement without a debugmsg",
          "[item][migration][p1e]")
{
    clear_all_state();
    const std::vector<std::string> ids = shipped_migration_ids();
    // P1e step 28 sweep: 164 MIGRATION entries in the shipped obsoletion files
    REQUIRE(ids.size() > 100);

    for (const std::string &id : ids) {
        CAPTURE(id);
        const itype_id obsolete(id);
        const itype_id target = item_controller->migrate_id(obsolete);
        CHECK(target.is_valid());
        std::string dmsg;
        detached_ptr<item> spawned;
        dmsg = capture_debugmsg_during([&]() {
            spawned = item_controller->spawn_migrated(obsolete, calendar::turn);
        });
        CHECK(dmsg.empty());
        REQUIRE(spawned);
        CHECK(spawned->typeId() == target);
    }
}

TEST_CASE("an obsolete DDA vest id spawns the DDA vest, not BN's combined vest",
          "[item][migration][p1e][vest]")
{
    clear_all_state();
    // DDA's ballistic_vest_esapi is a live item again (cdf_ballistic_vests.json); the
    // BN back-migration rework (plan section 6) says a save or spawn of it stays itself.
    // migration.json still maps it to modularvestceramic, so this test is expected to fail
    // until that migration is removed.
    detached_ptr<item> vest = item_controller->spawn_migrated(itype_id("ballistic_vest_esapi"),
                              calendar::turn);
    REQUIRE(vest);
    CHECK(vest->typeId() == itype_id("ballistic_vest_esapi"));
}

TEST_CASE("a live DDA plate or vest id is never migrated away", "[item][migration][p1e]")
{
    clear_all_state();
    CHECK(item_controller->migrate_id(itype_id("esapi_plate")) == itype_id("esapi_plate"));
    CHECK(item_controller->migrate_id(itype_id("esbi_plate")) == itype_id("esbi_plate"));
    CHECK(item_controller->migrate_id(itype_id("ballistic_vest_heavy")) ==
          itype_id("ballistic_vest_heavy"));
    CHECK(item_controller->migrate_id(itype_id("xl_ballistic_vest")) ==
          itype_id("xl_ballistic_vest"));
}

TEST_CASE("an obsolete vest id spawns the DDA vest from profession, itemgroup and direct spawns",
          "[item][migration][p1e][vest]")
{
    clear_all_state();
    const itype_id vest("ballistic_vest_esapi");
    auto count_vests = [&vest](const std::vector<detached_ptr<item>> &items) {
        int found = 0;
        for (const detached_ptr<item> &it : items) {
            if (it->typeId() == vest) {
                ++found;
            }
        }
        return found;
    };

    SECTION("a profession starts with the migrated vest") {
        // legacy profession items go through item::spawn + migrate_item (profession.cpp)
        const auto items = string_id<profession>("test_p1e_vest_profession")->items(true, {});
        CHECK(count_vests(items) == 1);
    }
    SECTION("an itemgroup, as mapgen, vehicle cargo and NPC class lists use, spawns it") {
        // item_group::items_from applies the migration (item_group.cpp:121)
        const auto items = item_group::items_from(item_group_id("test_p1e_obsolete_vest_group"));
        CHECK(count_vests(items) == 1);
    }
    SECTION("a direct spawn of the obsolete id gives the DDA vest") {
        detached_ptr<item> spawned = item_controller->spawn_migrated(itype_id("ballistic_vest"),
                                     calendar::turn);
        REQUIRE(spawned);
        CHECK(spawned->typeId() == vest);
    }
}
