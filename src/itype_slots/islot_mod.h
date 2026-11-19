#pragma once

#include "itype.h"

struct islot_mod {
    /** If non-empty restrict mod to items with those base (before modifiers) ammo types */
    std::set<ammotype> acceptable_ammo;

    /** If set modifies parent ammo to this type */
    std::set<ammotype> ammo_modifier;

    /** If non-empty replaces the compatible magazines for the parent item */
    std::map< ammotype, std::set<itype_id> > magazine_adaptor;

    /**
     * Pockets the mod will add to the item.
     * Any MAGAZINE_WELL or MAGAZINE type pockets will be overwritten,
     * and CONTAINER pockets will be added.
     */
    std::vector<pocket_data> add_pockets;

    /** Proportional adjustment of parent item ammo capacity */
    float capacity_multiplier = 1.0;

    /** Per-host-pocket consumption adjustments, targeted by pocket id. On the mod
     *  slot so gunmods and toolmods share one reader. */
    std::vector<pocket_consumption_mod> consumption_mods;

    /** Per-host-pocket ammo capacity scaling, targeted by id. Integral MAGAZINE
     *  pockets only; a MAGAZINE_WELL target is ignored. */
    std::vector<pocket_capacity_mod> capacity_mods;
};
