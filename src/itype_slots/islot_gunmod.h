#pragma once

#include "itype.h"

struct islot_gunmod : common_ranged_data {
    /** Where is this gunmod installed (e.g. "stock", "rail")? */
    gunmod_location location;

    /** What kind of weapons can this gunmod be used with (e.g. "rifle", "crossbow")? */
    std::unordered_set<itype_id> usable;
    std::vector<std::unordered_set<weapon_category_id>> usable_category;
    std::unordered_set<itype_id> exclusion;
    std::vector<std::unordered_set<weapon_category_id>> exclusion_category;

    /** If this value is set (non-negative), this gunmod functions as a sight. A sight is only usable to aim by a character whose current @ref Character::recoil is at or below this value. */
    int sight_dispersion = -1;

    /**
     *  For sights (see @ref sight_dispersion), this value affects time cost of aiming.
     *  Higher is better. In case of multiple usable sights,
     *  the one with highest aim speed is used.
     */
    int aim_speed = -1;

    /** Modifies base loudness as provided by the currently loaded ammo. Measured in dB spl */
    int loudness = 0;
    /**
    * Modification to the base speed of the weapon projectile, in meters per second. Speed of sound in game is roughly 343 m/s
    * Supersonic projectiles can not be fully suppressed.
    */
    int speed = 0;

    /** How many moves does this gunmod take to install? */
    int install_time = 0;

    /** Increases base gun UPS consumption by this many times per shot */
    float ups_charges_multiplier = 1.0f;

    /** Increases base gun UPS consumption by this value per shot */
    int ups_charges_modifier = 0;

    /** Increases base gun energy consumption by this many times per shot */
    float energy_drain_multiplier = 1.0f;

    /** Increases base gun energy consumption by this value per shot */
    units::energy energy_drain_modifier = 0_kJ;

    /** Increases base gun ammo to fire by this many times per shot */
    float ammo_to_fire_multiplier = 1.0f;

    /** Increases base gun ammo to fire by this value per shot */
    int ammo_to_fire_modifier = 0;

    /** Increases gun weight by this many times */
    float weight_multiplier = 1.0f;

    /** Increases gun volume by this many times (replaces COLLAPSIBLE_STOCK flag) */
    float volume_multiplier = 1.0f;

    /** Firing modes added to or replacing those of the base gun */
    std::map<gun_mode_id, gun_modifier_data> mode_modifier;

    /** Per-pocket cost a gunmod imposes on its host for the modes it adds via
     *  mode_modifier. Loaded from key "mode_firing_requirements". */
    firing_requirement_set firing_requirements;

    /** Modes removed from the final merged mode set when this mod is installed. */
    std::set<gun_mode_id> hide_modes;

    std::set<ammo_effect_str_id> ammo_effects;

    /** Relative adjustment to base gun handling */
    int handling = 0;

    /** Percentage value change to the gun's loading time. Higher is slower */
    int reload_modifier = 0;

    /** Percentage value change to the gun's loading time. Higher is less likely */
    int consume_chance = 10000;

    /** Divsor to scale back gunmod consumption damage. lower is more damaging. Affected by ammo loudness and recoil, see ranged.cpp for how much. */
    int consume_divisor = 1;

    /** Modifies base strength required */
    int min_str_required_mod = 0;

    /** If the installed mod is visible and selectable in the inventory menu
     *  (used by mounted flashlights and similar), per DDA itype.h:1074 */
    bool is_visible_when_installed = false;

    /** Additional gunmod slots to add to the gun */
    std::map<gunmod_location, int> add_mod;

    /** Not compatable on weapons that have this mod slot */
    std::set<gunmod_location> blacklist_mod;
};
