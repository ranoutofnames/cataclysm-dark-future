#pragma once

// Transplanted from DDA src/test_data.h b4b62254ae2
// CDF: only the container spawn and pocket mod parts are carried for now.
// Other DDA test_data sections (bash, boarding, demographics, ...) arrive with their own jobs.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "type_id.h"

class JsonObject;
enum class pocket_type : int;

enum class spawn_type : int {
    none = 0,
    item_group,
    recipe,
    vehicle,
    profession,
    map
};

struct container_spawn_test_data {
    std::string given;
    item_group_id group;
    recipe_id recipe;
    vproto_id vehicle;
    profession_id profession;
    itype_id item;
    int charges = 0;
    int expected_amount = 0; // CDF: initialised (DDA leaves it uninitialised)

    void deserialize( const JsonObject &jo );
};

struct pocket_mod_test_data {
    itype_id base_item;
    itype_id mod_item;
    std::map<pocket_type, std::vector<uint64_t>> expected_pockets;

    void deserialize( const JsonObject &jo );
};

class test_data
{
    public:
        static std::map<spawn_type, std::vector<container_spawn_test_data>> container_spawn_data;
        static std::map<std::string, pocket_mod_test_data> pocket_mod_data;

        static void load( const JsonObject &jo );
};
