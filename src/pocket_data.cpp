// Pocket definitions moved out of item_pocket.cpp (DDA src/item_pocket.cpp b4b62254ae2
// pocket_data::load / check_definition / operator==, adapted to CDF; CDF differences are
// marked // CDF:).

#include "pocket_data.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "ammo.h"
#include "assign.h"
#include "cata_utility.h"
#include "flag.h"
#include "generic_factory.h"
#include "generic_readers.h"
#include "item.h"
#include "item_factory.h"
#include "itype.h"
#include "json.h"
#include "skill.h"
#include "string_formatter.h"
#include "translations.h"
#include "units_utility.h"

void sealable_data::load( const JsonObject &jo )
{
    optional( jo, was_loaded, "spoil_multiplier", spoil_multiplier, 1.0f );
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

bool pocket_data::has_item_restrictions() const
{
    return !flag_restrictions.empty() || !item_id_restriction.empty() || !skill_restriction.empty();
}

void pocket_data::load( const JsonObject &jo )
{
    // per DDA item_pocket.cpp:140 pocket_data::load; CDF additions are marked // CDF:
    optional( jo, was_loaded, "pocket_type", type, pocket_type::CONTAINER );
    // CDF: MIGRATION pockets are built in C++ only
    if( type == pocket_type::MIGRATION ) {
        jo.throw_error( "MIGRATION pockets can't be defined in JSON", "pocket_type" );
    }
    optional( jo, was_loaded, "ammo_restriction", ammo_restriction );
    optional( jo, was_loaded, "flag_restriction", flag_restrictions );
    optional( jo, was_loaded, "item_restriction", item_id_restriction );
    optional( jo, was_loaded, "material_restriction", material_restriction );
    optional( jo, was_loaded, "allowed_speedloaders", allowed_speedloaders );
    optional( jo, was_loaded, "default_magazine", default_magazine );
    optional( jo, was_loaded, "id", pocket_id );
    optional( jo, was_loaded, "description", description );
    optional( jo, was_loaded, "name", pocket_name );
    if( jo.has_member( "ammo_restriction" ) && ammo_restriction.empty() ) {
        jo.throw_error( "pocket defines empty ammo_restriction" );
    }
    // CDF: DDA checks "flag_restrictions" here (a typo that never fires); the real key is checked
    if( jo.has_member( "flag_restriction" ) && flag_restrictions.empty() ) {
        jo.throw_error( "pocket defines empty flag_restriction" );
    }
    if( jo.has_member( "item_restriction" ) && item_id_restriction.empty() ) {
        jo.throw_error( "pocket defines empty item_restriction" );
    }
    if( !item_id_restriction.empty() ) {
        // item_id_restriction is a sorted set, so the first id in JSON order comes from here
        std::vector<itype_id> item_restriction;
        mandatory( jo, was_loaded, "item_restriction", item_restriction );
        default_magazine = item_restriction.front();
    }
    // CDF: BN holster skills and the carry flag of worn storage
    if( jo.has_member( "skill_restriction" ) ) {
        for( const std::string &id : jo.get_string_array( "skill_restriction" ) ) {
            skill_restriction.insert( skill_id( id ) );
        }
    }
    optional( jo, was_loaded, "carry", carry, false );
    // ammo_restriction is a type of override, making the mandatory members not mandatory and superfluous
    // putting it in an if statement like this should allow for report_unvisited_member to work here
    if( ammo_restriction.empty() ) {
        optional( jo, was_loaded, "min_item_volume", min_item_volume, volume_reader(), 0_ml );
        units::volume temp = -1_ml;
        optional( jo, was_loaded, "max_item_volume", temp, volume_reader(), temp );
        if( temp != -1_ml ) {
            max_item_volume = temp;
        }
        optional( jo, was_loaded, "max_contains_volume", raw_volume_capacity, volume_reader(),
                  max_volume_for_container );
        optional( jo, was_loaded, "max_contains_weight", max_contains_weight, mass_reader(),
                  max_weight_for_container );
        // CDF: the default is applied by apply_default_length() (see pocket_length_factor)
        if( jo.has_member( "max_item_length" ) ) {
            optional( jo, was_loaded, "max_item_length", max_item_length );
        } else {
            apply_default_length();
        }
        optional( jo, was_loaded, "min_item_length", min_item_length );
        optional( jo, was_loaded, "extra_encumbrance", extra_encumbrance, 0 );
        optional( jo, was_loaded, "volume_encumber_modifier", volume_encumber_modifier, 1.0f );
        optional( jo, was_loaded, "ripoff", ripoff, 0 );
        // CDF: BN's JsonObject can't read a type with a load() member through optional()
        if( jo.has_object( "activity_noise" ) ) {
            activity_noise.load( jo.get_object( "activity_noise" ) );
        }
    }
    optional( jo, was_loaded, "spoil_multiplier", spoil_multiplier, 1.0f );
    optional( jo, was_loaded, "insulation", insulation, 1.0f );
    optional( jo, was_loaded, "weight_multiplier", weight_multiplier, 1.0f );
    optional( jo, was_loaded, "volume_multiplier", volume_multiplier, 1.0f );
    optional( jo, was_loaded, "magazine_well", magazine_well, volume_reader(), 0_ml );
    optional( jo, was_loaded, "moves", moves, 100 );
    optional( jo, was_loaded, "fire_protection", fire_protection, false );
    optional( jo, was_loaded, "watertight", watertight, false );
    optional( jo, was_loaded, "airtight", airtight, false );
    optional( jo, was_loaded, "open_container", open_container, false );
    optional( jo, was_loaded, "transparent", transparent, false );
    optional( jo, was_loaded, "rigid", rigid, false );
    optional( jo, was_loaded, "forbidden", forbidden, false );
    optional( jo, was_loaded, "holster", holster );
    optional( jo, was_loaded, "ablative", ablative );
    optional( jo, was_loaded, "inherits_flags", inherits_flags );
    // CDF: units::ememory does not exist yet (E1); the key is only marked visited
    if( jo.has_member( "ememory_max" ) ) {
        jo.get_member( "ememory_max" );
    }
    // if ablative also flag as a holster so it only holds 1 item
    if( ablative ) {
        holster = true;
    }
    // CDF: plate sector of an ablative slot
    if( jo.has_string( "sector" ) ) {
        const std::string sector_name = jo.get_string( "sector" );
        if( sector_name == "any" ) {
            sector = plate_sector::ANY;
        } else if( sector_name == "front" ) {
            sector = plate_sector::FRONT;
        } else if( sector_name == "back" ) {
            sector = plate_sector::BACK;
        } else if( sector_name == "left" ) {
            sector = plate_sector::LEFT;
        } else if( sector_name == "right" ) {
            sector = plate_sector::RIGHT;
        } else {
            jo.throw_error( "sector must be any, front, back, left or right", "sector" );
        }
    }
    // CDF: sealed_data is a std::optional (json.h has no value_ptr reader)
    if( jo.has_object( "sealed_data" ) ) {
        sealable_data sd;
        sd.load( jo.get_object( "sealed_data" ) );
        sealed_data = sd;
    }
}

namespace
{
// Factor on DDA's default maximum item length (D3: the 1.5 factor is dropped, DDA's value is
// used; the owner decision is still open, so a later change is this one constant).
constexpr double pocket_length_factor = 1.0;
} // namespace

void pocket_data::apply_default_length()
{
    const double cm = static_cast<double>( units::to_centimeter( units::default_length_from_volume(
            raw_volume_capacity ) ) );
    max_item_length = units::from_millimeter<std::int64_t>( std::llround( cm * 10.0 * M_SQRT2 *
                      pocket_length_factor ) );
}

std::string pocket_data::check_definition() const
{
    std::string res;
    if( type == pocket_type::MOD || type == pocket_type::CORPSE ||
        type == pocket_type::MIGRATION || type == pocket_type::SOFTWARE ) {
        return res;
    }
    for( const itype_id &it : item_id_restriction ) {
        if( !it.is_valid() ) {
            res += string_format( "item_restriction has invalid item id \"%s\"\n", it.str() );
        } else if( it->phase == LIQUID && !watertight ) {
            res += string_format( "restricted to liquid item \"%s\" but not watertight\n", it.str() );
        } else if( it->phase == GAS && !airtight ) {
            res += string_format( "restricted to gas item \"%s\" but not airtight\n", it.str() );
        }
    }
    for( const flag_id &f : flag_restrictions ) {
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
        // per DDA item_pocket.cpp:105: the default ammo item must exist and fit the pocket
        if( ammo_res.first.is_valid() ) {
            const itype_id &default_ammo = ammo_res.first->default_ammotype();
            if( default_ammo.is_null() || !default_ammo.is_valid() ) {
                res += string_format( "ammo type \"%s\" has an undefined default ammo item \"%s\"\n",
                                      ammo_res.first.str(), default_ammo.str() );
            }
            // CDF: DDA also requires watertight/airtight for liquid/gas ammo here; CDF data
            // (napalm tanks) doesn't set them, so that part is left out
        }
        if( ammo_res.second <= 0 ) {
            res += string_format( "ammo_restriction for \"%s\" must be positive\n", ammo_res.first.str() );
        }
    }
    if( ammo_restriction.empty() ) {
        if( raw_volume_capacity <= 0_ml ) {
            res += "has zero max volume\n";
        }
        if( max_contains_weight <= 0_gram ) {
            res += "has zero max weight\n";
        }
        if( max_item_volume && *max_item_volume < min_item_volume ) {
            res += "max_item_volume is less than min_item_volume, no item can fit\n";
        }
        if( max_item_volume && *max_item_volume > raw_volume_capacity ) {
            res += "max_item_volume is greater than the pocket volume\n";
        }
        if( min_item_volume > raw_volume_capacity ) {
            res += "min_item_volume is greater than the pocket volume, no item can fit\n";
        }
        if( max_item_length > 0_mm && max_item_length < min_item_length ) {
            res += "max_item_length is less than min_item_length, no item can fit\n";
        }
    }
    if( ablative && flag_restrictions.empty() && item_id_restriction.empty() ) {
        res += "ablative pocket needs a flag_restriction or item_restriction\n";
    }
    if( ablative && carry ) {
        res += "ablative pocket can't be a carry pocket\n";
    }
    if( magazine_well > 0_ml && rigid ) {
        res += "rigid overrides magazine_well\n";
    }
    if( ammo_restriction.empty() && magazine_well > 0_ml && magazine_well >= raw_volume_capacity ) {
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

void pocket_noise::load( const JsonObject &jo )
{
    optional( jo, was_loaded, "volume", volume, 0 );
    optional( jo, was_loaded, "chance", chance, 0 );
}

const pocket_data::FlagsSetType &pocket_data::get_flag_restrictions() const
{
    return flag_restrictions;
}

void pocket_data::add_flag_restriction( const flag_id &flag )
{
    flag_restrictions.insert( flag );
}

units::volume pocket_data::volume_capacity() const
{
    if( ammo_restriction.empty() ) {
        return raw_volume_capacity;
    }
    // per DDA item_pocket.cpp:2559: an ammo pocket's capacity is derived from its ammo capacity.
    // Find all valid ammo itypes
    const std::vector<const itype *> ammo_types = item_controller->find( [this]( const itype & t ) {
        return t.ammo && ammo_restriction.count( t.ammo->type );
    } );
    // Figure out which has the greatest volume and calculate on that basis
    units::volume max_total_volume = 0_ml;
    for( const itype *ammo_type : ammo_types ) {
        const int stack_size = ammo_type->stack_size ? ammo_type->stack_size : 1;
        const int max_count = ammo_restriction.at( ammo_type->ammo->type );
        const units::volume this_volume =
            1_ml * divide_round_up( static_cast<int>( ammo_type->volume * max_count / 1_ml ), stack_size );
        max_total_volume = std::max( max_total_volume, this_volume );
    }
    return max_total_volume;
}

bool pocket_data::operator==( const pocket_data &rhs ) const
{
    // per DDA item_pocket.cpp:227
    return rigid == rhs.rigid &&
           watertight == rhs.watertight &&
           airtight == rhs.airtight &&
           fire_protection == rhs.fire_protection &&
           get_flag_restrictions() == rhs.get_flag_restrictions() &&
           ammo_restriction == rhs.ammo_restriction &&
           item_id_restriction == rhs.item_id_restriction &&
           material_restriction == rhs.material_restriction &&
           type == rhs.type &&
           volume_capacity() == rhs.volume_capacity() &&
           min_item_volume == rhs.min_item_volume &&
           max_contains_weight == rhs.max_contains_weight &&
           spoil_multiplier == rhs.spoil_multiplier &&
           weight_multiplier == rhs.weight_multiplier &&
           moves == rhs.moves &&
           // CDF: fields DDA does not compare
           max_item_volume == rhs.max_item_volume &&
           max_item_length == rhs.max_item_length &&
           min_item_length == rhs.min_item_length &&
           holster == rhs.holster &&
           ablative == rhs.ablative &&
           carry == rhs.carry &&
           sector == rhs.sector &&
           open_container == rhs.open_container &&
           transparent == rhs.transparent &&
           volume_multiplier == rhs.volume_multiplier &&
           volume_encumber_modifier == rhs.volume_encumber_modifier &&
           magazine_well == rhs.magazine_well &&
           sealed_by_type == rhs.sealed_by_type &&
           skill_restriction == rhs.skill_restriction &&
           no_active_items == rhs.no_active_items;
}
