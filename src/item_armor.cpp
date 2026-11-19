// Armor code of item, after DDA item_armor.cpp (P1-transplant J5).
//
// Differences from DDA are tagged `// CDF:`. The main one: DDA fills `armor_portion_data` per sub
// body part for every armor, CDF only for items with `specifically_covers`, so everything here
// reads the per-body-part portions in `islot_armor::data` and uses `sub_data` for sub-location
// questions only.
#include "item.h"

#include <algorithm>
#include <set>
#include <vector>

#include "bodypart.h"
#include "damage.h"
#include "enums.h"
#include "flag.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"
#include "rng.h"
#include "subbodypart.h"
#include "type_id.h"
#include "itype_slots/islot_armor.h"

bool item::is_ablative() const
{
    const islot_armor *t = find_armor_data();
    return t ? t->ablative : false;
}

bool item::has_additional_encumbrance() const
{
    const islot_armor *t = find_armor_data();
    return t ? t->additional_pocket_enc : false;
}

bool item::has_noisy_pockets() const
{
    const islot_armor *t = find_armor_data();
    return t ? t->noisy : false;
}

std::vector<const item *> item::all_ablative_armor() const
{
    return contents.all_ablative_armor();
}

item::cover_type item::get_cover_type( damage_type type )
{
    // DDA: physical damage is covered by cover_melee when only melee attacks do it, else by
    // cover_ranged; everything else (heat, cold, acid ...) by the plain coverage
    switch( type ) {
        case DT_BASH:
        case DT_CUT:
        case DT_STAB:
            return cover_type::COVER_MELEE;
        case DT_BULLET:
            return cover_type::COVER_RANGED;
        default:
            break;
    }
    return cover_type::COVER_DEFAULT;
}

namespace
{

auto coverage_of( const armor_portion_data &portion, item::cover_type type ) -> int
{
    switch( type ) {
        case item::cover_type::COVER_DEFAULT:
            return portion.coverage;
        case item::cover_type::COVER_MELEE:
            return portion.cover_melee;
        case item::cover_type::COVER_RANGED:
            return portion.cover_ranged;
        case item::cover_type::COVER_VITALS:
            return portion.cover_vitals;
    }
    return portion.coverage;
}

/**
 * The portion that says what @p armor is like on @p sbp. CDF: items with sub-location data name
 * their sub-locations in `sub_data`; a body part nothing there names is plain and is read from
 * `data`. nullptr if the armor does not cover the sub-location.
 */
auto portion_for_sub_bodypart( const islot_armor &armor,
                               const sub_bodypart_id &sbp ) -> const armor_portion_data *
{
    bool body_part_has_sub_data = false;
    for( const armor_portion_data &portion : armor.sub_data ) {
        if( portion.covers.test( sbp->parent ) ) {
            body_part_has_sub_data = true;
        }
        if( portion.sub_coverage.contains( sbp.id() ) ) {
            return &portion;
        }
    }
    if( body_part_has_sub_data ) {
        return nullptr;
    }
    for( const armor_portion_data &portion : armor.data ) {
        if( portion.covers.test( sbp->parent ) ) {
            return &portion;
        }
    }
    return nullptr;
}

} // namespace

bool item::is_rigid() const
{
    // overrides for the item overall
    if( has_flag( flag_SOFT ) ) {
        return false;
    } else if( has_flag( flag_HARD ) ) {
        return true;
    }

    // if the item has no armor data it isn't rigid
    const islot_armor *armor = find_armor_data();
    if( armor == nullptr ) {
        return false;
    }

    bool rigid = std::ranges::any_of( armor->data, []( const armor_portion_data & portion ) {
        return portion.rigid;
    } );

    // check if ablative pieces are rigid too
    if( is_ablative() ) {
        for( const item *plate : all_ablative_armor() ) {
            rigid |= plate->is_rigid();
        }
    }

    return rigid;
}

bool item::is_comfortable() const
{
    // overrides for the item overall
    // NO_WEAR_EFFECT is there for jewelry and the like which is too small to be considered
    if( has_flag( flag_SOFT ) || has_flag( flag_PADDED ) || has_flag( flag_NO_WEAR_EFFECT ) ) {
        return true;
    } else if( has_flag( flag_HARD ) ) {
        return false;
    }

    // if the item has no armor data it isn't rigid
    const islot_armor *armor = find_armor_data();
    if( armor == nullptr ) {
        return true;
    }

    return std::ranges::any_of( armor->data, []( const armor_portion_data & portion ) {
        return portion.comfortable;
    } );
}

bool item::is_bp_rigid( const sub_bodypart_id &bp ) const
{
    // overrides for the item overall
    if( has_flag( flag_SOFT ) ) {
        return false;
    } else if( has_flag( flag_HARD ) ) {
        return true;
    }

    bool rigid = false;
    if( const islot_armor *armor = find_armor_data() ) {
        if( const armor_portion_data *portion = portion_for_sub_bodypart( *armor, bp ) ) {
            rigid |= portion->rigid;
        }
    }

    // check if ablative pieces are rigid too
    if( is_ablative() ) {
        for( const item *plate : all_ablative_armor() ) {
            rigid |= plate->is_bp_rigid( bp );
        }
    }

    return rigid;
}

bool item::is_bp_rigid( const bodypart_id &bp ) const
{
    // overrides for the item overall
    if( has_flag( flag_SOFT ) ) {
        return false;
    } else if( has_flag( flag_HARD ) ) {
        return true;
    }

    bool rigid = false;
    if( const std::optional<armor_portion_data> portion = portion_for_bodypart( bp ) ) {
        rigid |= portion->rigid;
    }

    // check if ablative pieces are rigid too
    if( is_ablative() ) {
        for( const item *plate : all_ablative_armor() ) {
            rigid |= plate->is_bp_rigid( bp );
        }
    }

    return rigid;
}

bool item::is_bp_rigid_selective( const sub_bodypart_id &bp ) const
{
    const islot_armor *armor = find_armor_data();
    if( armor == nullptr ) {
        return false;
    }
    const armor_portion_data *portion = portion_for_sub_bodypart( *armor, bp );
    if( portion == nullptr ) {
        return false;
    }

    bool rigid;
    // overrides for the item overall
    if( has_flag( flag_SOFT ) ) {
        rigid = false;
    } else if( has_flag( flag_HARD ) ) {
        rigid = true;
    } else {
        rigid = portion->rigid;
    }

    return rigid && portion->rigid_layer_only;
}

bool item::is_bp_rigid_selective( const bodypart_id &bp ) const
{
    const std::optional<armor_portion_data> portion = portion_for_bodypart( bp );
    if( !portion ) {
        return false;
    }

    bool rigid;
    // overrides for the item overall
    if( has_flag( flag_SOFT ) ) {
        rigid = false;
    } else if( has_flag( flag_HARD ) ) {
        rigid = true;
    } else {
        rigid = portion->rigid;
    }

    return rigid && portion->rigid_layer_only;
}

std::vector<sub_bodypart_id> item::get_covered_sub_body_parts() const
{
    return get_covered_sub_body_parts( get_side() );
}

std::vector<sub_bodypart_id> item::get_covered_sub_body_parts( const side s ) const
{
    std::set<sub_bodypart_id> covered;

    const auto add_from = [&covered, s]( const item & armor_item ) {
        const islot_armor *armor = armor_item.find_armor_data();
        if( armor == nullptr ) {
            return;
        }
        const bool all_sides = !armor->sided || s == side::BOTH || s == side::num_sides;
        std::set<bodypart_str_id> named_by_sub_data;
        for( const armor_portion_data &portion : armor->sub_data ) {
            for( const bodypart_str_id &bp : portion.covers ) {
                named_by_sub_data.insert( bp );
            }
            for( const sub_bodypart_str_id &sbp : portion.sub_coverage ) {
                if( all_sides || sbp->part_side == s || sbp->part_side == side::BOTH ) {
                    covered.insert( sbp.id() );
                }
            }
        }
        // CDF: DDA has sub-location data for every armor. A body part with none is covered at
        // all of its sub-locations.
        const body_part_set parts = armor_item.get_covered_body_parts( s );
        for( const armor_portion_data &portion : armor->data ) {
            for( const bodypart_str_id &bp : portion.covers ) {
                if( named_by_sub_data.contains( bp ) || !parts.test( bp ) ) {
                    continue;
                }
                for( const sub_bodypart_str_id &sbp : bp->sub_parts ) {
                    // CDF: a flat garment covers the primary locations only
                    if( !sbp->secondary ) {
                        covered.insert( sbp.id() );
                    }
                }
            }
        }
    };

    add_from( *this );
    // check for ablative armor too
    if( is_ablative() ) {
        for( const item *plate : all_ablative_armor() ) {
            add_from( *plate );
        }
    }
    return std::vector<sub_bodypart_id>( covered.begin(), covered.end() );
}

int item::get_avg_coverage( const cover_type type ) const
{
    const islot_armor *armor = find_armor_data();
    if( !armor ) {
        return 0;
    }
    int avg_coverage = 0;
    int avg_ctr = 0;
    for( const armor_portion_data &entry : armor->data ) {
        for( const bodypart_str_id &limb : entry.covers ) {
            const int coverage = get_coverage( limb.id(), type );
            if( coverage ) {
                avg_coverage += coverage;
                ++avg_ctr;
            }
        }
    }
    return avg_ctr > 0 ? avg_coverage / avg_ctr : 0;
}

int item::get_coverage( const bodypart_id &bodypart, const cover_type type ) const
{
    int coverage = 0;
    if( const std::optional<armor_portion_data> portion = portion_for_bodypart( bodypart ) ) {
        coverage = coverage_of( *portion, type );
    }
    if( is_ablative() ) {
        for( const item *plate : all_ablative_armor() ) {
            // Rationale for this kind of aggregation is that ablative armor can't be bigger than
            // an armor that contains it. But it may cover new body parts, e.g. face shield
            // attached to hard hat.
            coverage = std::max( coverage, plate->get_coverage( bodypart ) );
        }
    }
    return coverage;
}

int item::get_coverage( const bodypart_id &bodypart, const sub_bodypart_str_id &sbp,
                        const cover_type type ) const
{
    int coverage = 0;
    const islot_armor *armor = find_armor_data();
    bool body_part_has_sub_portion = false;
    if( armor != nullptr ) {
        for( const armor_portion_data &sub : armor->sub_data ) {
            if( !sub.covers.test( bodypart.id() ) ) {
                continue;
            }
            body_part_has_sub_portion = true;
            if( sub.sub_coverage.contains( sbp ) ) {
                coverage = std::max( coverage, coverage_of( sub, type ) );
            }
        }
    }
    if( !body_part_has_sub_portion ) {
        // a body part covered by a plain portion of an item that also has sub portions elsewhere
        if( const std::optional<armor_portion_data> portion = portion_for_bodypart( bodypart ) ) {
            coverage = coverage_of( *portion, type );
        }
    }
    if( is_ablative() ) {
        for( const item *plate : all_ablative_armor() ) {
            coverage = std::max( coverage, plate->get_coverage( bodypart, sbp ) );
        }
    }
    return coverage;
}

item::armor_status item::damage_armor_transforms( damage_unit &du, double enchant_multiplier ) const
{
    if( enchant_multiplier <= 0.0 ) {
        return armor_status::UNDAMAGED;
    }

    // We want armor's own resistance to this type, not the resistance it grants
    const float armors_own_resist = damage_resist( du.type, true );

    // to avoid dumb chip damage plates shouldn't ever transform if they take less than
    // 20% of their protection in damage
    if( du.amount / armors_own_resist < 0.2f ) {
        return armor_status::UNDAMAGED;
    }

    // plates are rated to survive 3 shots at the caliber they protect
    // linearly scale off the scale value to find the chance it breaks
    const float break_chance = 33.3f * ( du.amount / armors_own_resist ) * enchant_multiplier;

    const float roll_to_break = rng_float( 0.0, 100.0 );

    if( roll_to_break < break_chance ) {
        //the plate is broken
        return armor_status::TRANSFORMED;
    }
    return armor_status::UNDAMAGED;
}
