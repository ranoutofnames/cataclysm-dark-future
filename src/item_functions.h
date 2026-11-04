#pragma once

#include "coordinates.h"
#include "units.h"

class item;
class Character;

namespace item_funcs
{

/** Returns whether given item (or one of its gunmods) can be unloaded. */
bool can_be_unloaded( const item &itm );

/**
 * Whether @p itm is a gunmod installed in a gun that is not listed as a row of its own in item pickers.
 * Per DDA item_location.cpp:1181-1191 (item_location::is_invisible_installed_gunmod); the mods that stay
 * visible have is_visible_when_installed set (mounted flashlights and similar).
 */
auto is_invisible_installed_gunmod( const item &itm ) -> bool;

/** Number of shots left, considers if a gun uses ups, ammo, or both */
int shots_remaining( const Character &who, const item &it );

/**
 * Bills `qty` charges of a firing-requirements tool as whole uses via consume_tool_uses.
 * Returns charges consumed (uses * legacy_charges_per_use_factor), 0 on a bad request.
 * CDF: factored into a helper shared by item::use_charges and Character::use_charges so each
 * tool is billed in exactly one scan (DDA inlines this in item::use_charges, item.cpp:4016-4030)
 */
auto use_charges_via_tool_uses( item &it, int qty, const tripoint_bub_ms &pos,
                                Character *carrier ) -> int;

} // namespace item_funcs


