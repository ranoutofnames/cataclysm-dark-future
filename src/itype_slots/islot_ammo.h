#pragma once

#include "itype.h"

struct islot_ammo : common_ranged_data {
    struct shot_data {
        int count = 1;
        double half_angle = 0.0;
    };

    /**
     * Ammo type, basically the "form" of the ammo that fits into the gun/tool.
     */
    ammotype type;
    /**
     * Type id of casings, if any.
     */
    std::optional<itype_id> casing;

    /**
     * Control chance for and state of any items dropped at ranged target
     *@{*/
    itype_id drop = itype_id::NULL_ID();
    int drop_count = -1;
    bool drop_active = true;
    /*@}*/

    /**
     * Chance to fail to recover the ammo used.
     * As in, one_in(this_var) chance of destroying it.
     * For 100% chances, see @ref drop.
     */
    int dont_recover_one_in = 1;

    /**
     * Default charges.
     */
    int def_charges = 1;

    /**
     * See @ref ammo_effect struct.
     */
    std::set<ammo_effect_str_id> ammo_effects;
    /**
     * Base loudness of ammo (possibly modified by gun/gunmods). If unspecified an
     * appropriate value is calculated based upon the other properties of the ammo
     */
    int loudness = -1;
    /**
    * Speed of the projectile, in meters per second. Speed of sound in game is roughly 343 m/s
    * Supersonic projectiles can not be fully suppressed.
    * This is placed here so that guns and gunmods can effect projectile speed.
    */
    int speed = 1000;

    /** Recoil (per shot), roughly equivalent to kinetic energy (in Joules) */
    int recoil = 0;

    /**
     * Should this ammo explode in fire?
     * This value is cached by item_factory based on ammo_effects and item material.
     * @warning It is not read from the json directly.
     */
    bool cookoff = false;

    /**
     * Should this ammo apply a special explosion effect when in fire?
     * This value is cached by item_factory based on ammo_effects and item material.
     * @warning It is not read from the json directly.
     * */
    bool special_cookoff = false;

    /**
     * Some combat ammo might not have a damage value
     * Set this to make it show as combat ammo anyway
     */
    std::optional<bool> force_stat_display;

    /**
     * AoE shape or null if it's a projectile.
     */
    std::optional<shape_factory> shape;

    /// Shot-specific pellet pattern data.
    std::optional<shot_data> shot;

    bool was_loaded;

    void load( const JsonObject &jo );
    void deserialize( JsonIn &jsin );
};
