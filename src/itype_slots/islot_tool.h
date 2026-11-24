#pragma once

#include "itype.h"

struct islot_tool {
    std::set<ammotype> ammo_id;

    std::optional<itype_id> revert_to;
    std::string revert_msg;

    itype_id subtype;

    itype_id default_ammo = itype_id::NULL_ID();
    int max_charges = 0;
    int def_charges = 0;
    int charge_factor = 1;
    int charges_per_use = 0;
    int turns_per_charge = 0;
    int turns_active = 0;
    int power_draw = 0;
    int ups_eff_mult = 1;
    int ups_recharge_rate = 5;

    std::vector<int> rand_charges;

    // CDF: E1-1, DDA itype.h:172-177
    /** Electronic memory transferred per second. */
    units::ememory etransfer_rate = 0_KB;
    /** Type of e-device connection. */
    std::string e_port;
    /** E-device ports NOT supported for high speed file transfer. */
    std::vector<std::string> e_ports_banned;
};
