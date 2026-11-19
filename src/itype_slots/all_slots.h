#pragma once

// Convenience header: includes every itype slot struct.
// itype.h only forward-declares the slots. Code that needs a slot complete includes
// that slot's header (islot_<name>.h). Code that needs all of them, e.g. because it copies an
// itype by value, includes this header after itype.h.

#include "itype.h"

#include "itype_slots/islot_tool.h"
#include "itype_slots/islot_comestible.h"
#include "itype_slots/islot_brewable.h"
#include "itype_slots/islot_container.h"
#include "itype_slots/islot_armor.h"
#include "itype_slots/islot_pet_armor.h"
#include "itype_slots/islot_book.h"
#include "itype_slots/islot_mod.h"
#include "itype_slots/islot_engine.h"
#include "itype_slots/islot_wheel.h"
#include "itype_slots/islot_fuel.h"
#include "itype_slots/islot_gun.h"
#include "itype_slots/islot_gunmod.h"
#include "itype_slots/islot_magazine.h"
#include "itype_slots/islot_battery.h"
#include "itype_slots/islot_ammo.h"
#include "itype_slots/islot_bionic.h"
#include "itype_slots/islot_seed.h"
#include "itype_slots/islot_artifact.h"
#include "itype_slots/islot_milling.h"
