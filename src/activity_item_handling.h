#pragma once
#ifndef CATA_SRC_ACTIVITY_ITEM_HANDLING_H
#define CATA_SRC_ACTIVITY_ITEM_HANDLING_H

#include "coordinates.h"

class Character;

// CDF: MOLLE2, DDA activity_item_handling.h unload_sort_options. The rules the UNLOAD_ALL zones
// at one tile put on the sort activity. DDA's ignore_favorite and unload_corpses (STRIP_CORPSES)
// are not ported.
struct unload_sort_options {
    bool unload_mods = false;
    bool unload_molle = false;
    bool unload_always = false;
    bool unload_all = false;
    bool unload_sparse_only = false;
    int unload_sparse_threshold = 0;
};

// DDA set_unload_options: merges the options of every enabled UNLOAD_ALL zone at @p src.
unload_sort_options set_unload_options( const Character &you, const tripoint_abs_ms &src );

#endif // CATA_SRC_ACTIVITY_ITEM_HANDLING_H
