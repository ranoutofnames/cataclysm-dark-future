// Armor code of Character, after DDA character_attire.cpp (P1-transplant J5).
//
// DDA keeps these in its `outfit` class; CDF has no `outfit`, so they are Character members that
// walk `worn`. The absorb flow stays in character.cpp (its helpers are file-local there).
#include "character.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "bodypart.h"
#include "enums.h"
#include "faction.h"
#include "flag.h"
#include "item.h"
#include "item_pocket.h"
#include "messages.h"
#include "ret_val.h"
#include "rng.h"
#include "sounds.h"
#include "subbodypart.h"
#include "translations.h"
#include "type_id.h"

void Character::recalc_ablative_blocking( const item *removing )
{
    std::set<sub_bodypart_id> rigid_locations;

    // get all rigid locations
    // ablative pocketed armor shouldn't block adding ablative pieces
    for( const item * const &w : worn ) {
        // CDF: an item that is being taken off is still in `worn` when this is called
        if( w != removing && w->is_rigid() && !w->is_ablative() ) {
            for( const sub_bodypart_id &sbp : w->get_covered_sub_body_parts() ) {
                if( w->is_bp_rigid( sbp ) ) {
                    rigid_locations.emplace( sbp );
                }
            }
        }
    }

    bool should_warn = false;

    // pass that info to all the ablative pockets
    for( item * const &w : worn ) {
        if( w->is_ablative() ) {
            should_warn = true;
            for( item_pocket *p : w->get_ablative_pockets() ) {
                p->set_no_rigid( rigid_locations );
            }
        }
    }

    if( should_warn && !rigid_locations.empty() ) {
        add_msg_if_player( m_warning,
                           _( "You are wearing rigid armor with armor that has pockets for armor.  Until hard armor is removed inserting plates on those locations will be disabled." ) );
    }
}

namespace
{

auto rigid_test( const item &clothing, const item &worn_item,
                 const std::set<sub_bodypart_id> &to_test ) -> ret_val<void>
{
    // check each sublimb individually
    for( const sub_bodypart_id &sbp : to_test ) {
        // skip if the item doesn't currently cover the bp
        if( !worn_item.covers( sbp.id() ) ) {
            continue;
        }

        // skip if either item cares only about its layer and they don't match up
        // CDF: items have one layer, so the layers overlap when they are equal
        if( ( worn_item.is_bp_rigid_selective( sbp ) || clothing.is_bp_rigid_selective( sbp ) ) &&
            // CDF: MOLLE2, the layer of each item on this body part
            ( worn_item.has_belted_layer_for( sbp->parent.id() ) ? BELTED_LAYER : worn_item.get_layer() ) !=
            ( clothing.has_belted_layer_for( sbp->parent.id() ) ? BELTED_LAYER : clothing.get_layer() ) ) {
            continue;
        }

        // CDF: DDA lets splints go on integrated armor such as protective bark here, the
        // INTEGRATED flag is not in CDF

        if( worn_item.is_bp_rigid( sbp ) ) {
            return ret_val<void>::make_failure( _( "Can't wear more than one rigid item on %s!" ),
                                                sbp->name.translated() );
        }
    }

    return ret_val<void>::make_success();
}

} // namespace

ret_val<void> Character::check_rigid_conflicts( const item &clothing, side s ) const
{
    std::set<sub_bodypart_id> to_test;

    // if not overridden get the actual side of the item
    if( s == side::num_sides ) {
        s = clothing.get_side();
    }

    // if the clothing is sided and not on the side we want to test
    // create a copy that is swapped in side to test on
    if( clothing.is_sided() && clothing.get_side() != s ) {
        detached_ptr<item> swapped_item = item::spawn( clothing );
        swapped_item->set_side( s );
        // figure out which sublimbs need to be tested
        for( const sub_bodypart_id &sbp : swapped_item->get_covered_sub_body_parts() ) {
            if( swapped_item->is_bp_rigid( sbp ) ) {
                to_test.emplace( sbp );
            }
        }
    } else {
        // figure out which sublimbs need to be tested
        for( const sub_bodypart_id &sbp : clothing.get_covered_sub_body_parts() ) {
            if( clothing.is_bp_rigid( sbp ) ) {
                to_test.emplace( sbp );
            }
        }
    }

    // go through all worn and see if already wearing something rigid
    for( const item * const &worn_item : worn ) {
        ret_val<void> result = rigid_test( clothing, *worn_item, to_test );
        if( !result.success() ) {
            return result;
        }
    }

    return ret_val<void>::make_success();
}

ret_val<void> Character::check_rigid_conflicts( const item &clothing ) const
{
    if( !clothing.is_sided() ) {
        return check_rigid_conflicts( clothing, side::BOTH );
    }

    ret_val<void> ls = check_rigid_conflicts( clothing, side::LEFT );
    ret_val<void> rs = check_rigid_conflicts( clothing, side::RIGHT );

    if( !ls.success() && !rs.success() ) {
        return ls;
    }

    return ret_val<void>::make_success();
}

void Character::check_rigid_sidedness( item &clothing ) const
{
    if( !clothing.is_sided() ) {
        //nothing to do
        return;
    }
    const bool ls = check_rigid_conflicts( clothing, side::LEFT ).success();
    const bool rs = check_rigid_conflicts( clothing, side::RIGHT ).success();
    if( ls && rs ) {
        clothing.set_side( side::BOTH );
    } else if( ls ) {
        clothing.set_side( side::LEFT );
    } else {
        // this is a fallback option if it must be something
        clothing.set_side( side::RIGHT );
    }
}

int Character::clatter_sound() const
{
    int max_volume = 0;
    for( const item * const &i : worn ) {
        // if the item has noise making pockets we should check if they have clatered
        if( i->has_noisy_pockets() ) {
            for( const item_pocket *pocket : i->get_container_pockets() ) {
                const int noise_chance = pocket->get_pocket_data()->activity_noise.chance;
                const int volume = pocket->get_pocket_data()->activity_noise.volume;
                if( noise_chance > 0 && !pocket->empty() ) {
                    // if this pocket causes more volume and it triggers noise
                    if( volume > max_volume && rng( 1, 100 ) < noise_chance ) {
                        max_volume = volume;
                    }
                }
            }
        }
    }
    return max_volume;
}

void Character::make_clatter_sound() const
{
    const int volume = clatter_sound();
    if( volume <= 0 ) {
        return;
    }
    sound_event se;
    se.origin = bub_pos();
    se.volume = volume;
    se.category = sounds::sound_t::movement;
    se.description = _( "clattering equipment" );
    se.movement_noise = true;
    se.from_player = is_player();
    se.id = "none";
    se.variant = "none";
    // Sound of clattering equipment may awaken nearby monsters
    if( const faction *fac = get_faction() ) {
        se.faction = fac->id;
        se.monfaction = fac->mon_faction;
    }
    sounds::sound( se );
}
