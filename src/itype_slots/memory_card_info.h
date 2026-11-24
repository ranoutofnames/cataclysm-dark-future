#pragma once

#include <set>
#include <string>

#include "itype.h"

class JsonObject;

// CDF: E1-1, DDA itype.h:1335-1350 (b4b62254ae2). Per-type data for item::generate_recipes and the
// memory card content generation (DDA item.cpp:3537-3560, filled by E1-3).
// DEVIATION: recipes_categories holds recipe category strings (CDF recipes use `std::string category`
// like BN, there is no crafting_category_id type).
struct memory_card_info {
    float data_chance = 1.0f;
    itype_id on_read_convert_to;  // NOLINT(cata-serialize)

    float photos_chance = 0.0f;  // NOLINT(cata-serialize)
    int photos_amount = 0;  // NOLINT(cata-serialize)

    float songs_chance = 0.0f;  // NOLINT(cata-serialize)
    int songs_amount = 0; // NOLINT(cata-serialize)

    float recipes_chance = 0.0f;
    int recipes_amount = 0;
    int recipes_level_min = 0;
    int recipes_level_max = 10;
    std::set<std::string> recipes_categories = { "CC_FOOD" };
    bool secret_recipes = false;

    bool was_loaded = false;
    /** DDA memory_card_info::deserialize. Keys that are absent keep their current value. */
    void load( const JsonObject &jo );
};
