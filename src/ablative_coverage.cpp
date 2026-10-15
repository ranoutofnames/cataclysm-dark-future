#include "ablative_coverage.h"

#include <algorithm>
#include <utility>

#include "flag.h"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"
#include "output.h"
#include "pocket_data.h"
#include "rng.h"
#include "string_formatter.h"
#include "translations.h"

namespace
{

plate_coverage_model &installed_model()
{
    // DDA's slice model is the model in play; directional facing may replace it later
    // behind the same seam (docs/directional-facing.md)
    static plate_coverage_model model = coverage_model::dda_slices;
    return model;
}

} // namespace

namespace coverage_model
{

item_pocket *dda_slices( item &carrier, const Character &, const bodypart_id &bp,
                         const sub_bodypart_str_id &sbp, const attack_context &ctx )
{
    int roll = ctx.coverage_roll;
    if( roll <= 0 ) {
        roll = rng( 1, 100 );
    }
    for( item_pocket &pocket : carrier.contents.get_pockets() ) {
        if( !pocket.get_data().ablative || pocket.empty() ) {
            continue;
        }
        const item &plate = *pocket.items().front();
        const item::cover_type ctype = static_cast<item::cover_type>( ctx.cover );
        const int coverage = sbp.is_null() ? plate.get_coverage( bp, ctype ) :
                             plate.get_coverage( bp, sbp, ctype );
        if( roll <= coverage ) {
            return &pocket;
        }
        roll -= coverage;
    }
    return nullptr;
}

} // namespace coverage_model

namespace ablative
{

bool has_plate_slots( const item &carrier )
{
    return std::ranges::any_of( carrier.contents.get_pockets(), []( const item_pocket & pocket ) {
        return pocket.get_data().ablative;
    } );
}

std::vector<item *> plates( item &carrier )
{
    std::vector<item *> ret;
    for( item_pocket &pocket : carrier.contents.get_pockets() ) {
        if( !pocket.get_data().ablative ) {
            continue;
        }
        for( item *plate : pocket.items() ) {
            ret.push_back( plate );
        }
    }
    return ret;
}

std::vector<const item *> plates( const item &carrier )
{
    std::vector<const item *> ret;
    for( const item_pocket &pocket : carrier.contents.get_pockets() ) {
        if( !pocket.get_data().ablative ) {
            continue;
        }
        for( const item *plate : pocket.items() ) {
            ret.push_back( plate );
        }
    }
    return ret;
}

std::string slot_name( const pocket_data &slot )
{
    if( !slot.name.empty() ) {
        return slot.name.translated();
    }
    std::vector<std::string> names;
    for( const flag_id &flag : slot.get_flag_restrictions() ) {
        if( flag.is_valid() ) {
            const std::string phrase = flag->restriction();
            if( !phrase.empty() ) {
                names.push_back( phrase );
            }
        }
    }
    for( const itype_id &id : slot.item_id_restriction ) {
        if( id.is_valid() ) {
            names.push_back( id->nname( 1 ) );
        }
    }
    if( names.empty() ) {
        return _( "armor plates" );
    }
    return enumerate_as_string( names, enumeration_conjunction::or_ );
}

item_pocket *free_slot_for( item &carrier, const item &plate )
{
    for( item_pocket &pocket : carrier.contents.get_pockets() ) {
        if( pocket.get_data().ablative && pocket.can_contain( plate ).success() ) {
            return &pocket;
        }
    }
    return nullptr;
}

std::string refusal_reason( const item &carrier, const item &plate )
{
    std::vector<const item_pocket *> slots;
    for( const item_pocket &pocket : carrier.contents.get_pockets() ) {
        if( pocket.get_data().ablative ) {
            slots.push_back( &pocket );
        }
    }
    if( slots.empty() ) {
        return std::string();
    }
    const bool looks_like_plate = plate.has_flag( flag_ABLATIVE_LARGE ) ||
                                  plate.has_flag( flag_ABLATIVE_MEDIUM ) ||
                                  std::ranges::any_of( slots, [&plate]( const item_pocket * slot ) {
        return slot->get_data().item_id_restriction.contains( plate.typeId() );
    } );
    if( !looks_like_plate ) {
        return std::string();
    }
    const bool fits_a_slot = std::ranges::any_of( slots, [&plate]( const item_pocket * slot ) {
        return slot->is_compatible( plate ).success();
    } );
    if( fits_a_slot ) {
        return _( "all its matching plate slots are taken" );
    }
    std::vector<std::string> names;
    for( const item_pocket *slot : slots ) {
        const std::string name = slot_name( slot->get_data() );
        if( std::ranges::find( names, name ) == names.end() ) {
            names.push_back( name );
        }
    }
    return string_format( _( "it has no plate slot for that (its slots take %s)" ),
                          enumerate_as_string( names, enumeration_conjunction::and_ ) );
}

} // namespace ablative

void set_plate_coverage_model( plate_coverage_model model )
{
    installed_model() = std::move( model );
}

const plate_coverage_model &get_plate_coverage_model()
{
    return installed_model();
}

item_pocket *select_ablative_pocket( item &carrier, const Character &wearer,
                                     const bodypart_id &bp, const sub_bodypart_str_id &sbp,
                                     const attack_context &ctx )
{
    const plate_coverage_model &model = installed_model();
    if( !model ) {
        return nullptr;
    }
    item_pocket *chosen = model( carrier, wearer, bp, sbp, ctx );
    if( chosen == nullptr || chosen->empty() || !chosen->get_data().ablative ) {
        return nullptr;
    }
    return chosen;
}

scoped_plate_coverage_model::scoped_plate_coverage_model( plate_coverage_model model ) :
    previous( installed_model() )
{
    installed_model() = std::move( model );
}

scoped_plate_coverage_model::~scoped_plate_coverage_model()
{
    installed_model() = std::move( previous );
}
