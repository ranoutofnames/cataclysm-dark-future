#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "coordinates.h"
#include "type_id.h"

class Character;
class Creature;
class item;
class item_pocket;
class pocket_data;

/**
 * What the armor model may know about an attack (CDF, P1e step 25).
 *
 * Filled by Creature::deal_damage and handed to absorb_hit. The DDA plate coverage model
 * ignores it. It exists so that a directional model (see docs/directional-facing.md) can
 * choose the plate from where the attack came from, with no further plumbing.
 * Everything may be unset: callers that know nothing pass an empty context.
 */
struct attack_context {
    /** The attacker, if any (null for explosions, falls, hallucinations ...). */
    const Creature *source = nullptr;
    /** Where the attack came from: the shooter's tile or the attacker's tile. */
    std::optional<tripoint_bub_ms> origin;
    /**
     * The coverage roll (1..100) drawn once per damage element before the worn-armor loop and
     * shared by every layer, the plate pass and each carrier's own coverage test, as in DDA's
     * single `roll = rng( 1, 100 )`: a model that walks plate slices (`roll -= coverage`) works on
     * a copy and must leave this value alone, because the carrier's own armor_absorb tests
     * the original. 0: not rolled (direct callers outside
     * absorb_hit), in which case each consumer rolls for itself.
     */
    int coverage_roll = 0;
    /**
     * Which coverage protects against this damage unit: an item::cover_type value (default,
     * melee, ranged or vitals), as DDA reads it from the damage unit's type.
     */
    int cover = 0;
};

/**
 * Armor plates and the plate coverage seam (CDF, P1e step 26).
 *
 * A plate carrier has ablative pockets (plate slots), each holding one armor plate. When a
 * damage unit hits a body part the carrier covers, the seam decides which slot's plate, if
 * any, takes it. DDA's model rolls stacked coverage slices (P1e step 27); a directional model
 * could replace it by choosing from attack_context. Everything else (absorption, damage,
 * shattering, encumbrance, info) is the same under every model.
 */
namespace ablative
{

/** True if @p carrier has at least one plate slot (an ablative pocket), filled or not. */
bool has_plate_slots( const item &carrier );

/** The plates currently inserted in @p carrier, in pocket order. */
std::vector<item *> plates( item &carrier );
std::vector<const item *> plates( const item &carrier );

/** Short name of what a plate slot takes ("large armor plates"). */
std::string slot_name( const pocket_data &slot );

/**
 * The plate slot of @p carrier that is free and accepts @p plate, or nullptr. Does not
 * check anything but the slot: the caller decides whether @p plate may be inserted.
 */
item_pocket *free_slot_for( item &carrier, const item &plate );

/**
 * Why @p plate can't be inserted into @p carrier, naming the slot it needs (for a refusal
 * message). Empty if @p carrier has no plate slots or @p plate isn't a plate.
 */
std::string refusal_reason( const item &carrier, const item &plate );

} // namespace ablative

/**
 * A plate coverage model: which plate slot of @p carrier takes one damage unit that hit
 * @p sbp of @p bp of @p wearer. nullptr: the hit falls in a gap between plates.
 */
using plate_coverage_model = std::function<item_pocket *( item &carrier, const Character &wearer,
                             const bodypart_id &bp, const sub_bodypart_str_id &sbp,
                             const attack_context &ctx )>;

namespace coverage_model
{

/**
 * DDA's plate coverage model (P1e step 27): stacked coverage slices of the rolled
 * sub-location, as DDA's Character::ablative_armor_absorb (character_armor.cpp).
 *
 * The carrier's plates are walked in pocket order. Each plate covers the next
 * `plate.get_coverage( bp, sbp )` slices of the 100 of `ctx.coverage_roll` (a roll is drawn
 * here when the context carries none), so a plate takes the hit when `roll <= coverage` and
 * the walk continues with `roll -= coverage` otherwise (#66874: roll == coverage is a hit).
 * Two ESAPI plates of 45 on the upper torso cover rolls 1-45 and 46-90, leaving a 10% gap.
 * Ignores the wearer and everything in the context but the roll, so it works for any
 * attacker, including none.
 */
item_pocket *dda_slices( item &carrier, const Character &wearer, const bodypart_id &bp,
                         const sub_bodypart_str_id &sbp, const attack_context &ctx );

} // namespace coverage_model

/**
 * Installs the model used by select_ablative_pocket. An empty model never selects a plate.
 * The model installed at startup is coverage_model::dda_slices.
 */
void set_plate_coverage_model( plate_coverage_model model );
/** The installed model (empty if none). */
const plate_coverage_model &get_plate_coverage_model();

/**
 * Which ablative pocket of @p carrier (if any) takes this damage unit on @p sbp.
 * Returns nullptr when the hit falls in a gap between plates, or the installed model is empty.
 * The returned pocket always holds a plate.
 */
item_pocket *select_ablative_pocket( item &carrier, const Character &wearer,
                                     const bodypart_id &bp, const sub_bodypart_str_id &sbp,
                                     const attack_context &ctx );

/** Installs a model for the lifetime of the object, then restores the previous one. */
class scoped_plate_coverage_model
{
    public:
        explicit scoped_plate_coverage_model( plate_coverage_model model );
        ~scoped_plate_coverage_model();
        scoped_plate_coverage_model( const scoped_plate_coverage_model & ) = delete;
        scoped_plate_coverage_model &operator=( const scoped_plate_coverage_model & ) = delete;
    private:
        plate_coverage_model previous;
};
