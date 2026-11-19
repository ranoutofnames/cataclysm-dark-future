#pragma once

#include "itype.h"

struct islot_artifact {
    art_charge charge_type;
    art_charge_req charge_req;
    std::vector<art_effect_passive> effects_wielded;
    std::vector<art_effect_active>  effects_activated;
    std::vector<art_effect_passive> effects_carried;
    std::vector<art_effect_passive> effects_worn;
    std::vector<std::string> dream_msg_unmet;
    std::vector<std::string> dream_msg_met;
    int dream_freq_unmet;
    int dream_freq_met;
};
