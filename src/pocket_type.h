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
    // CDF: E1-1, DDA pocket_type.h:14: electronic files (books and e-scannable items) stored on
    // an e-device. Holds files by their ememory_size, not by volume, weight or length.
    E_FILE_STORAGE,
    // CDF: LINKUP1a, DDA pocket_type.h:15: pocket for storing power/data cables and handling
    // their connections. The extension cable content arrives with LINKUP1b.
    CABLE,
    // items that have no proper home in the item's current pockets
    MIGRATION,
    // CDF: E1-1, DDA pocket_type.h:17: obsolete e-book pocket, an enum value only (no data uses it)
    EBOOK,
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
