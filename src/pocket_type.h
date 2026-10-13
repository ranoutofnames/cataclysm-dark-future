#pragma once

#include <string>

#include "enum_conversions.h"

// The kinds of pocket an item can have (DDA #39406, adapted for CDF).
// Saved to JSON as strings, so the order may change freely.
enum class pocket_type : int {
    // general storage: bags, bottles, holsters, bandoliers
    CONTAINER,
    // ammo inside a magazine
    MAGAZINE,
    // magazines/batteries inside a gun or tool
    MAGAZINE_WELL,
    // gunmods and toolmods
    MOD,
    // items inside a corpse
    CORPSE,
    // software on a storage device
    SOFTWARE,
    // items that have no proper home in the item's current pockets
    MIGRATION,
    LAST
};

template<>
struct enum_traits<pocket_type> {
    static constexpr pocket_type last = pocket_type::LAST;
};

namespace io
{
template<>
std::string enum_to_string<pocket_type>( pocket_type data );
} // namespace io
