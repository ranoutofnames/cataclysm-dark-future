#pragma once

#include "itype.h"

struct islot_magazine {
    /** What type of ammo this magazine can be loaded with */
    std::set<ammotype> type;

    /** Capacity of magazine (in equivalent units to ammo charges) */
    int capacity = 0;

    /** Default amount of ammo contained by a magazine (often set for ammo belts) */
    int count = 0;

    /** Default type of ammo contained by a magazine (often set for ammo belts) */
    itype_id default_ammo = itype_id::NULL_ID();

    /**
     * How reliable this magazine on a range of 0 to 10?
     * @see doc/GAME_BALANCE.md
     */
    int reliability = 0;

    /** How long it takes to load each unit of ammo into the magazine */
    int reload_time = 100;

    /** Multiplier for the gun jamming from physical damage */
    double mag_jam_mult = 1.0;

    /** For ammo belts one linkage (of given type) is dropped for each unit of ammo consumed */
    std::optional<itype_id> linkage;

    /** If false, ammo will cook off if this mag is affected by fire */
    bool protects_contents = false;
};
