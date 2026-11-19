#pragma once

#include "itype.h"

// TODO: this shares a lot with the ammo item type, merge into a separate slot type?
struct islot_gun : common_ranged_data {
    /**
     * What skill this gun uses.
     */
    skill_id skill_used = skill_id::NULL_ID();
    /**
     * What type of ammo this gun uses.
     */
    std::set<ammotype> ammo;
    /**
     * Gun durability, affects gun being damaged during shooting.
     */
    int durability = 0;
    /**
    *  Multiplier of the chance for the gun to jam.
    */
    double gun_jam_mult = 1.0;
    /**
     * For guns with an integral magazine what is the capacity?
     */
    int clip = 0;
    /**
     * Reload time, in moves.
     */
    int reload_time = 100;
    /**
     * Noise displayed when reloading the weapon.
     */
    std::string reload_noise = translate_marker( "click." );
    /**
     * Volume of the noise made when reloading this weapon.
     * Base reload volume set to 40dB spl. Below 20dB spl is effectively silent.
     */
    units::sound reload_noise_volume = 40_dB;

    /** Maximum aim achievable using base weapon sights */
    int sight_dispersion = 30;

    /** Modifies base loudness as provided by the currently loaded ammo. Measured in dB spl */
    int loudness = 0;
    /**
    * Modification to the base speed of the weapon projectile, in meters per second. Speed of sound in game is roughly 343 m/s
    * Supersonic projectiles can not be fully suppressed.
    */
    int speed = 0;

    /**
     * If this uses UPS charges, how many (per shoot), 0 for no UPS charges at all.
     */
    int ups_charges = 0;
    /**
     * If this uses electric energy, how much (per shot).
     */
    units::energy energy_drain = 0_kJ;
    /**
     * One in X chance for gun to require major cleanup after firing blackpowder shot.
     */
    int blackpowder_tolerance = 8;
    /**
     * Minimum ammo recoil for gun to be able to fire more than once per attack.
     */
    int min_cycle_recoil = 0;
    /**
     * Volume of material removed by sawing down the barrel, if left unspecified barrel can't be sawed down.
     */
    units::volume barrel_volume = 0_ml;
    /**
     * Effects that are applied to the ammo when fired.
     */
    std::set<ammo_effect_str_id> ammo_effects;
    /**
     * Location for gun mods.
     * Key is the location (untranslated!), value is the number of mods
     * that the location can have. The value should be > 0.
     */
    std::map<gunmod_location, int> valid_mod_locations;
    /**
    *Built in mods. string is id of mod. These mods will get the IRREMOVABLE flag set.
    */
    std::set<itype_id> built_in_mods;
    /**
    *Default mods, string is id of mod. These mods are removable but are default on the weapon.
    */
    std::set<itype_id> default_mods;

    /** Firing modes are supported by the gun. Always contains at least DEFAULT mode */
    std::map<gun_mode_id, gun_modifier_data> modes;

    /** Burst size for AUTO mode (legacy field for items not migrated to specify modes ) */
    int burst = 0;

    /** How easy is control of recoil? If unset value automatically derived from weapon type */
    int handling = -1;

    /**
     *  Additional recoil applied per shot before effects of handling are considered
     *  @note useful for adding recoil effect to guns which otherwise consume no ammo
     */
    int recoil = 0;

    /** How much ammo is consumed per shot. */
    int ammo_to_fire = 1;
};
