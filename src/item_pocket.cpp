#include "item_pocket.h"

#include <algorithm>
#include <climits>

#include "ammo.h"
#include "assign.h"
#include "debug.h"
#include "enums.h"
#include "flag.h"
#include "item.h"
#include "itype.h"
#include "json.h"
#include "locations.h"
#include "output.h"
#include "skill.h"
#include "string_formatter.h"
#include "translations.h"
#include "units_utility.h"

namespace io
{

template<>
std::string enum_to_string<pocket_type>( pocket_type data )
{
    switch( data ) {
        // *INDENT-OFF*
        case pocket_type::CONTAINER: return "CONTAINER";
        case pocket_type::MAGAZINE: return "MAGAZINE";
        case pocket_type::MAGAZINE_WELL: return "MAGAZINE_WELL";
        case pocket_type::MOD: return "MOD";
        case pocket_type::CORPSE: return "CORPSE";
        case pocket_type::SOFTWARE: return "SOFTWARE";
        case pocket_type::MIGRATION: return "MIGRATION";
        // *INDENT-ON*
        case pocket_type::LAST:
            break;
    }
    debugmsg( "Invalid pocket_type" );
    abort();
}

} // namespace io

void sealable_data::load( const JsonObject &jo )
{
    jo.read( "spoil_multiplier", spoil_multiplier );
}

pocket_data::pocket_data( pocket_type pk ) : type( pk )
{
    rigid = true;
}

const pocket_data &migration_pocket_data()
{
    static const pocket_data migration( pocket_type::MIGRATION );
    return migration;
}

const std::set<std::string> &pocket_data::ignored_keys()
{
    // DDA-only keys: accepted so DDA-format item JSON loads, but ignored (plan P1 §4 step 1).
    static const std::set<std::string> keys = {
        "extra_encumbrance", "ripoff", "activity_noise", "inherits_flags",
        "material_restriction", "allowed_speedloaders", "forbidden"
    };
    return keys;
}

std::set<std::string> &pocket_data::ignored_keys_seen()
{
    static std::set<std::string> seen;
    return seen;
}

bool pocket_data::has_item_restrictions() const
{
    return !flag_restriction.empty() || !item_restriction.empty() || !skill_restriction.empty();
}

void pocket_data::load( const JsonObject &jo )
{
    if( jo.has_string( "pocket_type" ) ) {
        type = jo.get_enum_value<pocket_type>( "pocket_type" );
        if( type == pocket_type::MIGRATION ) {
            jo.throw_error( "MIGRATION pockets can't be defined in JSON", "pocket_type" );
        }
    }

    if( jo.has_member( "ammo_restriction" ) ) {
        for( const JsonMember member : jo.get_object( "ammo_restriction" ) ) {
            ammo_restriction[ammotype( member.name() )] = member.get_int();
        }
        if( ammo_restriction.empty() ) {
            jo.throw_error( "pocket defines empty ammo_restriction", "ammo_restriction" );
        }
    }
    if( jo.has_member( "flag_restriction" ) ) {
        for( const std::string &f : jo.get_string_array( "flag_restriction" ) ) {
            flag_restriction.insert( flag_id( f ) );
        }
        if( flag_restriction.empty() ) {
            jo.throw_error( "pocket defines empty flag_restriction", "flag_restriction" );
        }
    }
    if( jo.has_member( "item_restriction" ) ) {
        for( const std::string &id : jo.get_string_array( "item_restriction" ) ) {
            item_restriction.insert( itype_id( id ) );
        }
        if( item_restriction.empty() ) {
            jo.throw_error( "pocket defines empty item_restriction", "item_restriction" );
        }
    }
    if( jo.has_member( "skill_restriction" ) ) {
        for( const std::string &id : jo.get_string_array( "skill_restriction" ) ) {
            skill_restriction.insert( skill_id( id ) );
        }
    }

    // ammo_restriction overrides the volume and weight limits (as in DDA)
    if( ammo_restriction.empty() ) {
        assign( jo, "max_contains_volume", volume_capacity );
        assign( jo, "max_contains_weight", max_contains_weight );
        units::volume max_vol = 0_ml;
        if( assign( jo, "max_item_volume", max_vol ) ) {
            max_item_volume = max_vol;
        }
        assign( jo, "min_item_volume", min_item_volume );
        // Accepted now, parsed and enforced by item length (P1b).
        if( jo.has_member( "max_item_length" ) ) {
            max_item_length = jo.get_string( "max_item_length" );
        }
        if( jo.has_member( "min_item_length" ) ) {
            min_item_length = jo.get_string( "min_item_length" );
        }
        jo.read( "volume_encumber_modifier", volume_encumber_modifier );
    }

    jo.read( "spoil_multiplier", spoil_multiplier );
    jo.read( "weight_multiplier", weight_multiplier );
    jo.read( "volume_multiplier", volume_multiplier );
    assign( jo, "magazine_well", magazine_well );
    jo.read( "moves", moves );
    jo.read( "fire_protection", fire_protection );
    jo.read( "watertight", watertight );
    jo.read( "airtight", airtight );
    jo.read( "open_container", open_container );
    jo.read( "transparent", transparent );
    jo.read( "rigid", rigid );
    jo.read( "holster", holster );
    jo.read( "ablative", ablative );
    // an ablative pocket holds exactly one plate (as in DDA)
    if( ablative ) {
        holster = true;
    }
    if( jo.has_object( "sealed_data" ) ) {
        sealable_data sd;
        sd.load( jo.get_object( "sealed_data" ) );
        sealed_data = sd;
    }
    jo.read( "description", description );
    jo.read( "name", name );

    for( const std::string &key : ignored_keys() ) {
        if( jo.has_member( key ) ) {
            // mark it visited so the loader doesn't complain
            jo.get_member( key );
            ignored_keys_seen().insert( key );
        }
    }
}

std::string pocket_data::check_definition() const
{
    std::string res;
    if( type == pocket_type::MOD || type == pocket_type::CORPSE ||
        type == pocket_type::MIGRATION || type == pocket_type::SOFTWARE ) {
        return res;
    }
    for( const itype_id &it : item_restriction ) {
        if( !it.is_valid() ) {
            res += string_format( "item_restriction has invalid item id \"%s\"\n", it.str() );
        } else if( it->phase == LIQUID && !watertight ) {
            res += string_format( "restricted to liquid item \"%s\" but not watertight\n", it.str() );
        }
    }
    for( const flag_id &f : flag_restriction ) {
        if( !f.is_valid() ) {
            res += string_format( "flag_restriction has invalid flag \"%s\"\n", f.str() );
        }
    }
    for( const skill_id &sk : skill_restriction ) {
        if( !sk.is_valid() ) {
            res += string_format( "skill_restriction has invalid skill \"%s\"\n", sk.str() );
        }
    }
    for( const auto &ammo_res : ammo_restriction ) {
        if( !ammo_res.first.is_valid() ) {
            res += string_format( "ammo_restriction has invalid ammo type \"%s\"\n", ammo_res.first.str() );
        }
        if( ammo_res.second <= 0 ) {
            res += string_format( "ammo_restriction for \"%s\" must be positive\n", ammo_res.first.str() );
        }
    }
    if( ammo_restriction.empty() ) {
        if( volume_capacity <= 0_ml ) {
            res += "has zero max volume\n";
        }
        if( max_contains_weight <= 0_gram ) {
            res += "has zero max weight\n";
        }
        if( max_item_volume && *max_item_volume < min_item_volume ) {
            res += "max_item_volume is less than min_item_volume, no item can fit\n";
        }
        if( max_item_volume && *max_item_volume > volume_capacity ) {
            res += "max_item_volume is greater than the pocket volume\n";
        }
        if( min_item_volume > volume_capacity ) {
            res += "min_item_volume is greater than the pocket volume, no item can fit\n";
        }
    }
    if( magazine_well > 0_ml && rigid ) {
        res += "rigid overrides magazine_well\n";
    }
    if( ammo_restriction.empty() && magazine_well > 0_ml && magazine_well >= volume_capacity ) {
        res += "magazine_well is not smaller than the pocket volume\n";
    }
    if( ( watertight || airtight ) && min_item_volume > 0_ml ) {
        res += "watertight or airtight is incompatible with min_item_volume\n";
    }
    if( moves < 0 ) {
        res += "moves must not be negative\n";
    }
    return res;
}

item_pocket::item_pocket( item *owner, const pocket_data *data ) :
    data( data ), contents( new contents_item_location( owner ) )
{
}

item_pocket::~item_pocket() = default;

bool item_pocket::is_standard_type() const
{
    return data->type == pocket_type::CONTAINER || data->type == pocket_type::MAGAZINE ||
           data->type == pocket_type::MAGAZINE_WELL;
}

bool item_pocket::sealed() const
{
    return data->sealed_data.has_value() && ( _sealed || data->sealed_by_type );
}

// TODO(P1c/P1d, DDA #82070): runtime sealing of authored sealed_data pockets; today seal()
// is only called from save loading, sealing at runtime is sealed_by_type.
void item_pocket::seal()
{
    if( data->sealed_data ) {
        _sealed = true;
    }
}

void item_pocket::unseal()
{
    _sealed = false;
}

units::volume item_pocket::volume_capacity() const
{
    if( !data->ammo_restriction.empty() ) {
        // capacity follows the largest accepted ammo, as in DDA
        units::volume best = 0_ml;
        for( const auto &ammo_res : data->ammo_restriction ) {
            const itype_id &def = ammo_res.first->default_ammotype();
            if( def.is_null() || !def.is_valid() || def->stack_size <= 0 ) {
                continue;
            }
            const units::volume v = def->volume * ammo_res.second / def->stack_size;
            best = std::max( best, v );
        }
        return best;
    }
    return data->volume_capacity;
}

units::mass item_pocket::weight_capacity() const
{
    return data->max_contains_weight;
}

void item_pocket::refresh_cache() const
{
    // Cache key: item count plus the sum of item pointers. This catches inserts and removals
    // by any route (location_vector access, visitable removal, item locations) without a
    // call. It does NOT catch in-place size changes: every mutation route must call
    // item_contents::contents_changed() (which invalidates this cache) for those.
    uintptr_t signature = 0;
    for( const item *it : contents ) {
        signature += reinterpret_cast<uintptr_t>( it );
    }
    const size_t count = contents.size();
    if( cache.state != size_cache::state_t::NONE && cache.count == count &&
        cache.signature == signature ) {
        return;
    }
    const bool stable = std::ranges::all_of( contents, []( const item * it ) {
        return it->size_is_stable();
    } );
    cache = size_cache();
    cache.state = stable ? size_cache::state_t::STABLE : size_cache::state_t::UNSTABLE;
    cache.count = count;
    cache.signature = signature;
}

units::volume item_pocket::contains_volume() const
{
    if( contents.empty() ) {
        return 0_ml;
    }
    refresh_cache();
    if( cache.state == size_cache::state_t::STABLE && cache.volume_known ) {
        return cache.volume;
    }
    units::volume vol = 0_ml;
    for( const item *it : contents ) {
        vol += it->volume();
    }
    if( cache.state == size_cache::state_t::STABLE ) {
        cache.volume = vol;
        cache.volume_known = true;
    }
    return vol;
}

units::mass item_pocket::contains_weight() const
{
    if( contents.empty() ) {
        return 0_gram;
    }
    refresh_cache();
    if( cache.state == size_cache::state_t::STABLE && cache.weight_known ) {
        return cache.weight;
    }
    units::mass wt = 0_gram;
    for( const item *it : contents ) {
        wt += it->weight();
    }
    if( cache.state == size_cache::state_t::STABLE ) {
        cache.weight = wt;
        cache.weight_known = true;
    }
    return wt;
}

units::volume item_pocket::remaining_volume() const
{
    return std::max( 0_ml, volume_capacity() - contains_volume() );
}

units::mass item_pocket::remaining_weight() const
{
    return std::max( 0_gram, weight_capacity() - contains_weight() );
}

int item_pocket::ammo_capacity( const ammotype &ammo ) const
{
    const auto found = data->ammo_restriction.find( ammo );
    return found == data->ammo_restriction.end() ? 0 : found->second;
}

int item_pocket::remaining_ammo_capacity( const ammotype &ammo ) const
{
    int total = 0;
    for( const item *it : contents ) {
        if( it->has_flag( flag_CASING ) ) {
            continue;
        }
        total += it->count();
    }
    return std::max( 0, ammo_capacity( ammo ) - total );
}

bool item_pocket::empty() const
{
    return contents.empty();
}

size_t item_pocket::size() const
{
    return contents.size();
}

bool item_pocket::holster_full() const
{
    return data->holster && !contents.empty();
}

bool item_pocket::is_overfull() const
{
    if( !is_standard_type() || contents.empty() ) {
        return false;
    }
    if( data->holster && contents.size() > 1 ) {
        return true;
    }
    if( !data->ammo_restriction.empty() ) {
        const item *front = nullptr;
        int total = 0;
        for( const item *it : contents ) {
            if( it->has_flag( flag_CASING ) ) {
                continue;
            }
            front = front == nullptr ? it : front;
            total += it->count();
        }
        return front != nullptr && total > ammo_capacity( front->ammo_type() );
    }
    return contains_volume() > volume_capacity() || contains_weight() > weight_capacity();
}

namespace
{

// Volume of one unit: one charge of a count-by-charges item, otherwise the whole item.
units::volume unit_volume( const item &it )
{
    if( it.count_by_charges() && it.charges > 0 ) {
        return it.volume() / it.charges;
    }
    return it.volume();
}

} // namespace

ret_val<item_pocket::contain_code> item_pocket::is_compatible( const item &it ) const
{
    using code = contain_code;
    switch( data->type ) {
        case pocket_type::MIGRATION:
        case pocket_type::CORPSE:
            // these always accept: nothing may ever be lost
            return ret_val<code>::make_success();
        case pocket_type::MOD:
            if( it.is_toolmod() || it.is_gunmod() ) {
                return ret_val<code>::make_success();
            }
            return ret_val<code>::make_failure( code::ERR_MOD, _( "only mods can go into the mod pocket" ) );
        case pocket_type::SOFTWARE:
            return ret_val<code>::make_success();
        case pocket_type::MAGAZINE_WELL:
            if( !it.is_magazine() ) {
                return ret_val<code>::make_failure( code::ERR_FLAG,
                                                    _( "only magazines can go into the magazine well" ) );
            }
            break;
        case pocket_type::CONTAINER:
        case pocket_type::MAGAZINE:
        case pocket_type::LAST:
            break;
    }

    if( data->no_active_items && it.is_active() ) {
        return ret_val<code>::make_failure( code::ERR_ACTIVE, _( "it is active" ) );
    }

    if( data->has_item_restrictions() ) {
        const bool by_item = data->item_restriction.contains( it.typeId() );
        const bool by_flag = std::ranges::any_of( data->flag_restriction, [&it]( const flag_id & f ) {
            return it.has_flag( f );
        } );
        const bool by_skill = it.is_gun() && data->skill_restriction.contains( it.gun_skill() );
        if( !by_item && !by_flag && !by_skill ) {
            return ret_val<code>::make_failure( code::ERR_FLAG,
                                                _( "it doesn't accept this kind of item" ) );
        }
    }

    // ammo restriction overrides volume, weight and phase rules
    if( !data->ammo_restriction.empty() ) {
        if( !it.is_ammo() ) {
            return ret_val<code>::make_failure( code::ERR_AMMO, _( "it only holds ammunition" ) );
        }
        if( !data->ammo_restriction.contains( it.ammo_type() ) ) {
            return ret_val<code>::make_failure( code::ERR_AMMO, _( "it doesn't hold this type of ammunition" ) );
        }
        return ret_val<code>::make_success();
    }

    if( it.made_of( LIQUID ) && !data->watertight ) {
        return ret_val<code>::make_failure( code::ERR_LIQUID, _( "it isn't watertight" ) );
    }
    if( it.made_of( GAS ) && !data->airtight ) {
        return ret_val<code>::make_failure( code::ERR_GAS, _( "it isn't airtight" ) );
    }

    // holsters compare the whole item, other pockets one charge of a stack (as in DDA)
    const units::volume vol = data->holster ? it.volume() : unit_volume( it );
    if( data->max_item_volume && vol > *data->max_item_volume && !it.made_of( LIQUID ) &&
        !it.made_of( GAS ) ) {
        return ret_val<code>::make_failure( code::ERR_TOO_BIG, _( "it is too big" ) );
    }
    if( vol < data->min_item_volume ) {
        return ret_val<code>::make_failure( code::ERR_TOO_SMALL, _( "it is too small" ) );
    }
    return ret_val<code>::make_success();
}

ret_val<item_pocket::contain_code> item_pocket::can_contain( const item &it,
        bool ignore_contents, bool ignore_sealed ) const
{
    using code = contain_code;
    ret_val<code> compatible = is_compatible( it );
    if( !compatible.success() ) {
        return compatible;
    }
    if( !is_standard_type() ) {
        return ret_val<code>::make_success();
    }

    if( sealed() && !ignore_sealed ) {
        return ret_val<code>::make_failure( code::ERR_SEALED, _( "it is sealed" ) );
    }

    if( !ignore_contents && !contents.empty() ) {
        const item &front = *contents.front();
        if( it.made_of( LIQUID ) || it.made_of( GAS ) ) {
            if( !front.stacks_with( it ) ) {
                return ret_val<code>::make_failure( code::ERR_LIQUID,
                                                    _( "it can't mix that with what it already holds" ) );
            }
        } else if( front.made_of( LIQUID ) || front.made_of( GAS ) ) {
            return ret_val<code>::make_failure( code::ERR_LIQUID, _( "it already holds a liquid" ) );
        }
    }

    if( !data->ammo_restriction.empty() ) {
        if( !ignore_contents ) {
            // BN never mixed different ammo in one magazine or bandolier
            for( const item *contained : contents ) {
                if( !contained->has_flag( flag_CASING ) && contained->typeId() != it.typeId() ) {
                    return ret_val<code>::make_failure( code::ERR_NO_SPACE,
                                                        _( "it can't mix different ammunition" ) );
                }
            }
        }
        const int room = ignore_contents ? ammo_capacity( it.ammo_type() ) :
                         remaining_ammo_capacity( it.ammo_type() );
        if( it.count() > room ) {
            return ret_val<code>::make_failure( code::ERR_NO_SPACE,
                                                _( "it only has room for %d more rounds" ), room );
        }
        return ret_val<code>::make_success();
    }

    if( data->holster && !ignore_contents && !contents.empty() ) {
        return ret_val<code>::make_failure( code::ERR_HOLSTER_FULL, _( "it already holds an item" ) );
    }

    const units::mass wt = it.weight();
    if( wt > weight_capacity() ) {
        return ret_val<code>::make_failure( code::ERR_TOO_HEAVY, _( "it is too heavy" ) );
    }
    const units::volume vol = it.volume();
    if( vol > volume_capacity() ) {
        return ret_val<code>::make_failure( code::ERR_TOO_BIG, _( "it is too big" ) );
    }
    if( ignore_contents || contents.empty() ) {
        // an empty pocket has all of its capacity left, which the checks above covered
        return ret_val<code>::make_success();
    }
    if( wt > remaining_weight() ) {
        return ret_val<code>::make_failure( code::ERR_CANNOT_SUPPORT,
                                            _( "it can't hold that much more weight" ) );
    }
    if( vol > remaining_volume() ) {
        return ret_val<code>::make_failure( code::ERR_NO_SPACE, _( "it has %s left" ),
                                            format_volume( remaining_volume() ) + " " + volume_units_abbr() );
    }
    return ret_val<code>::make_success();
}

bool item_pocket::accepts_some_ammo( const item &it ) const
{
    if( data->ammo_restriction.empty() || data->holster || !is_compatible( it ).success() ) {
        return false;
    }
    for( const item *contained : contents ) {
        if( !contained->has_flag( flag_CASING ) && contained->typeId() != it.typeId() ) {
            return false;
        }
    }
    return remaining_ammo_capacity( it.ammo_type() ) > 0;
}

bool item_pocket::type_can_holster( const itype &t, const item &it )
{
    for( const pocket_data &d : t.pockets ) {
        if( d.holster && item_pocket( nullptr, &d ).can_contain( it, true, true ).success() ) {
            return true;
        }
    }
    return false;
}

bool item_pocket::has_item_stacks_with( const item &it ) const
{
    return std::ranges::any_of( contents, [&it]( const item * e ) {
        return e->stacks_with( it );
    } );
}

float item_pocket::spoil_multiplier() const
{
    if( sealed() && data->sealed_data ) {
        return data->sealed_data->spoil_multiplier;
    }
    return data->spoil_multiplier;
}

bool item_pocket::better_pocket( const item_pocket &rhs, const item &it, bool nested ) const
{
    // Adapted from DDA item_pocket::better_pocket (#40286). Pocket settings are P2.
    const bool rhs_it_stack = rhs.has_item_stacks_with( it );
    if( has_item_stacks_with( it ) != rhs_it_stack ) {
        return rhs_it_stack;
    }

    if( data->ammo_restriction.empty() != rhs.data->ammo_restriction.empty() ) {
        // pockets restricted by ammo should try to get filled first
        return !rhs.data->ammo_restriction.empty();
    }

    if( data->has_item_restrictions() != rhs.data->has_item_restrictions() ) {
        // pockets restricted by flag (holsters, sheaths) should try to get filled first
        return rhs.data->has_item_restrictions();
    }

    if( it.is_comestible() && it.goes_bad() ) {
        // a lower spoil multiplier is better
        return rhs.spoil_multiplier() < spoil_multiplier();
    }

    if( it.made_of( SOLID ) && data->watertight != rhs.data->watertight ) {
        // keep watertight pockets free for liquids
        return !rhs.data->watertight;
    }

    // skip irrelevant properties of nested containers
    if( nested ) {
        return false;
    }

    if( data->rigid != rhs.data->rigid ) {
        // prefer rigid containers, as their fixed volume is wasted unless filled
        return rhs.data->rigid;
    }

    if( remaining_volume() != rhs.remaining_volume() ) {
        // we want the least amount of remaining volume
        return rhs.remaining_volume() < remaining_volume();
    }

    return rhs.data->moves < data->moves;
}

void item_pocket::insert( detached_ptr<item> &&it )
{
    if( !it ) {
        return;
    }
    if( it->count_by_charges() ) {
        for( item *check : contents ) {
            // NOLINTNEXTLINE(bugprone-use-after-move)
            if( check->merge_charges( std::move( it ) ) ) {
                return;
            }
        }
    }
    // NOLINTNEXTLINE(bugprone-use-after-move)
    contents.push_back( std::move( it ) );
}
