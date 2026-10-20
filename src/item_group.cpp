#include "item_group.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <optional>
#include <set>
#include <vector>

#include "calendar.h"
#include "debug.h"
#include "detached_ptr.h"
#include "enums.h"
#include "flag.h"
#include "flat_set.h"
#include "game_constants.h"
#include "item.h"
#include "item_contents.h"
#include "item_factory.h"
#include "item_pocket.h"
#include "itype.h"
#include "iuse_actor.h"
#include "json.h"
#include "rng.h"
#include "type_id.h"
#include "options.h"
#include "pocket_type.h"
#include "ret_val.h"

namespace
{

/// Ammo spawn entries can be wrapped in their default container.
auto spawned_ammo_id( const item &spawned ) -> itype_id
{
    const auto *ammo = &spawned;
    while( ammo->is_container() && ammo->contents.num_item_stacks() == 1 ) {
        ammo = &ammo->get_contained();
    }
    return ammo->typeId();
}

} // namespace

// FIXME: Somehow we cant get item_groups from their ids
// Only can get Item_spawn_data
// Either that or it is not clear how to access it from item_factory
/** @relates string_id */
template<>
bool string_id<Item_group>::is_valid() const
{
    return item_group::group_is_defined( *this );
}

namespace
{
// Per DDA item_group.cpp:101 (guess_pocket_for); the E_FILE_STORAGE case is cut (E1).
auto guess_pocket_for( const item &container, const item &payload ) -> pocket_type
{
    if( ( container.is_gun() && payload.is_gunmod() ) || ( container.is_tool() &&
            payload.is_toolmod() ) ) {
        return pocket_type::MOD;
    }
    if( ( container.is_gun() || container.is_tool() ) && payload.is_magazine() ) {
        return pocket_type::MAGAZINE_WELL;
    } else if( container.is_magazine() && payload.is_ammo() ) {
        return pocket_type::MAGAZINE;
    }
    if( !container.has_pocket_type( pocket_type::CONTAINER ) &&
        container.has_pocket_type( pocket_type::SOFTWARE ) ) {
        // CDF: the software pocket of a usb drive stands in for DDA's e-file storage (E1)
        return pocket_type::SOFTWARE;
    }
    return pocket_type::CONTAINER;
}

// Per DDA item_group.cpp:118 (put_into_container), over detached items (T2/T4): an item is
// only consumed by put_in when it fits, so the excess is still in `items` afterwards.
void put_into_container( std::vector<detached_ptr<item>> &items, std::size_t num_items,
                         const std::optional<itype_id> &container_type,
                         const std::optional<std::string> &/*container_variant*/, const bool sealed,
                         const time_point &birthday, Item_spawn_data::overflow_behaviour on_overflow,
                         const std::string &context )
{
    if( !container_type ) {
        return;
    }

    // Randomly permute the list of items so that when some don't fit it's
    // not always the ones at the end which are rejected.
    assert( items.size() >= num_items );
    std::shuffle( items.end() - static_cast<std::ptrdiff_t>( num_items ), items.end(),
                  rng_get_engine() );

    // CDF: BN items have no variants, so the container variant is not applied
    detached_ptr<item> ctr_det = item::spawn( *container_type, birthday );
    item &ctr = *ctr_det;
    std::vector<detached_ptr<item>> excess;
    ctr.begin_bulk_fill();
    for( auto it = items.end() - static_cast<std::ptrdiff_t>( num_items ); it != items.end(); ++it ) {
        const pocket_type pk_type = guess_pocket_for( ctr, **it );
        const std::string payload_type = ( *it )->typeId().str();
        // quiet=true: the overflow path below handles failure.
        const ret_val<void> ret = ctr.put_in( std::move( *it ), pk_type, false, nullptr, true );
        if( ret.success() ) {
            continue;
        }
        if( ctr.is_corpse() ) {
            ctr.force_insert_item( std::move( *it ), pk_type );
            continue;
        }
        switch( on_overflow ) {
            case Item_spawn_data::overflow_behaviour::none:
                debugmsg( "item %s could not be put in container %s when spawning item group %s: %s.  "
                          "This can be resolved either by changing the container or contents "
                          "to ensure that they fit, or by specifying an overflow behaviour via "
                          "\"on_overflow\" on the item group.",
                          payload_type, container_type->str(), context, ret.str() );
                break;
            case Item_spawn_data::overflow_behaviour::spill:
                excess.push_back( std::move( *it ) );
                break;
            case Item_spawn_data::overflow_behaviour::discard:
                break;
            case Item_spawn_data::overflow_behaviour::last:
                debugmsg( "Invalid overflow_behaviour" );
                break;
        }
    }
    ctr.end_bulk_fill();
    ctr.add_automatic_whitelist();
    if( sealed ) {
        ctr.seal();
    }

    items.erase( items.end() - static_cast<std::ptrdiff_t>( num_items ), items.end() );
    items.reserve( items.size() + excess.size() + 1 );
    items.insert( items.end(), std::make_move_iterator( excess.begin() ),
                  std::make_move_iterator( excess.end() ) );
    items.push_back( std::move( ctr_det ) );
}
} // namespace

std::vector<detached_ptr<item>> Item_spawn_data::create( const time_point &birthday ) const
{
    RecursionList rec;
    return create( birthday, rec );
}

detached_ptr<item> Item_spawn_data::create_single( const time_point &birthday ) const
{
    RecursionList rec;
    return create_single( birthday, rec );
}

Single_item_creator::Single_item_creator( const std::string &_id, Type _type, int _probability )
    : Item_spawn_data( _probability )
    , id( _id )
    , type( _type )
{
}

detached_ptr<item> Single_item_creator::create_single( const time_point &birthday,
        RecursionList &rec ) const
{
    detached_ptr<item> tmp;
    if( type == S_ITEM ) {
        if( id == "corpse" ) {
            tmp = item::make_corpse( mtype_id::NULL_ID(), birthday );
        } else {
            tmp = item::spawn( id, birthday );
            if( tmp && !migrated_from.is_null() ) {
                // what the old id would have come out as, e.g. a plate carrier with its
                // plates; before the modifier below adds its own contents
                item_controller->migrate_item( migrated_from, *tmp );
            }
        }
    } else if( type == S_ITEM_GROUP ) {
        if( std::ranges::contains( rec, id ) ) {
            debugmsg( "recursion in item spawn list %s", id.c_str() );
            return detached_ptr<item>();
        }
        rec.push_back( id );
        Item_spawn_data *isd = item_controller->get_group( item_group_id( id ) );
        if( isd == nullptr ) {
            debugmsg( "unknown item spawn list %s", id.c_str() );
            return detached_ptr<item>();
        }
        tmp = isd->create_single( birthday, rec );
        rec.erase( rec.end() - 1 );
    } else if( type == S_NONE ) {
        return detached_ptr<item>();
    }
    if( !tmp ) {
        return detached_ptr<item>();
    }
    if( one_in( 3 ) && tmp->has_flag( flag_VARSIZE ) ) {
        tmp->set_flag( flag_FIT );
    }
    if( modifier ) {
        tmp = modifier->modify( std::move( tmp ) );
    } else {
        // TODO: change the spawn lists to contain proper references to containers
        tmp = item::in_its_container( std::move( tmp ) );
    }
    if( container_item ) {
        // per DDA item_group.cpp:196: the entry-wrapper / object form of container-item
        const int qty = tmp->count();
        tmp = item::in_container( *container_item, std::move( tmp ), qty, sealed,
                                  container_item_variant.value_or( "" ) );
    }
    return tmp;
}

std::vector<detached_ptr<item>> Single_item_creator::create( const time_point &birthday,
                             RecursionList &rec ) const
{
    std::vector<detached_ptr<item>> result;
    int cnt = 1;
    if( modifier ) {
        auto modifier_count = modifier->count;
        cnt = ( modifier_count.first == modifier_count.second ) ? modifier_count.first : rng(
                  modifier_count.first, modifier_count.second );
    }
    for( ; cnt > 0; cnt-- ) {
        if( type == S_ITEM ) {
            detached_ptr<item> itm = create_single( birthday, rec );
            if( itm && !itm->is_null() ) {
                result.push_back( std::move( itm ) );
            }
        } else {
            if( std::ranges::contains( rec, id ) ) {
                debugmsg( "recursion in item spawn list %s", id.c_str() );
                return result;
            }
            rec.push_back( id );
            Item_spawn_data *isd = item_controller->get_group( item_group_id( id ) );
            if( isd == nullptr ) {
                debugmsg( "unknown item spawn list %s", id.c_str() );
                return result;
            }
            std::vector<detached_ptr<item>> tmplist = isd->create( birthday, rec );
            rec.erase( rec.end() - 1 );
            if( modifier ) {
                for( auto &elem : tmplist ) {
                    elem = modifier->modify( std::move( elem ) );
                }
            } else {
                for( auto &itm : tmplist ) {
                    itm = item::in_its_container( std::move( itm ) );
                }
            }
            result.insert( result.end(), std::make_move_iterator( tmplist.begin() ),
                           std::make_move_iterator( tmplist.end() ) );
        }
    }
    put_into_container( result, result.size(), container_item, container_item_variant, sealed,
                        birthday, on_overflow, id );
    return result;
}

void Single_item_creator::check_consistency( const std::string &context ) const
{
    if( on_overflow != overflow_behaviour::none && !container_item ) {
        debugmsg( "item group %s specifies overflow behaviour but not container", context );
    }
    if( type == S_ITEM ) {
        if( !itype_id( id ).is_valid() ) {
            debugmsg( "item id %s is unknown (in %s)", id, context );
        }
    } else if( type == S_ITEM_GROUP ) {
        // TODO: figure out a way to check for itemgroup recursion here
        // Beyond the fact that your game wil ljust stall...
        if( !item_group::group_is_defined( item_group_id( id ) ) ) {
            debugmsg( "item group id %s is unknown (in %s)", id, context );
        }
    } else if( type == S_NONE ) {
        // this is okay, it will be ignored
    } else {
        debugmsg( "Unknown type of Single_item_creator: %d", static_cast<int>( type ) );
    }
    if( modifier ) {
        modifier->check_consistency( context );
        for( auto &item : every_item_modified( false ) ) {
            if( modifier && modifier->ammo != nullptr ) {
                for( auto &ammo : modifier->ammo->every_item_modified() ) {
                    if( item->is_tool() ) {
                        if( item->magazine_integral() ) {
                            if( !item->ammo_types().contains( ammo->ammo_type() ) &&
                                !item->ammo_types().contains( ammo->ammo_default()->ammo->type ) ) {
                                debugmsg( "Incompatible ammo: %s in ( %s %s )", ammo->type->get_id().str(),
                                          item->type->get_id().str(), context );
                            }
                        } else if( !item->ammo_types().empty() ) {
                            if( !item->ammo_types().contains( ammo->ammo_type() ) &&
                                !item->ammo_types().contains( ammo->ammo_default()->ammo->type ) ) {
                                debugmsg( "Incompatible ammo: %s in ( %s %s )", ammo->type->get_id().str(),
                                          item->type->get_id().str(), context );
                            }
                        } else if( item->ammo_default() != ammo->type->get_id() ) {
                            debugmsg( "Incompatible ammo: %s in ( %s %s )", ammo->type->get_id().str(),
                                      item->type->get_id().str(), context );
                        }
                    }
                    if( item->is_magazine() || item->is_gun() ) {
                        if( item->magazine_integral() ) {
                            if( !item->ammo_types().contains( ammo->ammo_type() ) ) {
                                debugmsg( "Incompatible ammo: %s in ( %s %s )", ammo->type->get_id().str(),
                                          item->type->get_id().str(), context );
                            }
                        } else if( !item->ammo_types().contains( ammo->ammo_type() ) ) {
                            debugmsg( "Incompatible ammo: %s in ( %s %s )", ammo->type->get_id().str(),
                                      item->type->get_id().str(), context );
                        }
                    }
                    // Exception as can_reload_with does not work for containers
                    // As it always returns true regardless of volume
                    else if( item->is_container() ) {
                        if( !item->is_reloadable_with( ammo->type->get_id() ) ) {
                            debugmsg( "Incompatible contained item: %s in ( %s %s )", ammo->type->get_id().str(),
                                      item->type->get_id().str(), context );
                        }
                        auto volume = ammo->volume();
                        if( ammo->count_by_charges() ) {
                            volume /= ammo->charges;
                        }
                        auto maxmult = std::max( modifier->charges.second, modifier->count.second );
                        // If autofill put to 1
                        if( maxmult == -1 ) {
                            maxmult = 1;
                        }
                        if( item->get_container_capacity() < volume * maxmult ) {
                            debugmsg( "Incompatible contained size: %s in ( %s %s )", ammo->type->get_id().str(),
                                      item->type->get_id().str(), context );
                        }
                    }
                }
            }
            if( modifier && modifier->container != nullptr ) {
                for( auto &container : modifier->container->every_item_modified() ) {
                    if( container->is_container() ) {
                        if( container->is_container() && container->type->container &&
                            !container->type->container->watertight &&
                            item->type->phase == LIQUID ) {
                            debugmsg( "Liquid %s in solid container ( %s %s )", item->type->get_id().str(),
                                      container->type->get_id().str(), context );
                        }
                        auto volume = item->volume();
                        if( item->count_by_charges() ) {
                            volume /= item->charges;
                        }
                        auto maxmult = std::max( modifier->charges.second, modifier->count.second );
                        // If autofill put to 1
                        if( maxmult == -1 ) {
                            maxmult = 1;
                        }
                        if( container->get_container_capacity() < volume * maxmult ) {
                            debugmsg( "Incompatible individual contents size: %s in ( %s %s )", item->type->get_id().str(),
                                      container->type->get_id().str(), context );
                        }
                    } else if( container->is_holster() && !container->can_holster( *item ) ) {
                        debugmsg( "Incorrect holstered object object: ( %s %s ) in %s", item->type->get_id().str(),
                                  context, container->type->get_id().str() );
                    } else if( !container->get_container_pockets().empty() &&
                               !container->can_contain( *item ).success() ) {
                        // CDF: ammo pouches and quivers are plain pocket containers (P1f)
                        debugmsg( "Incorrect bandoliered object object: ( %s %s ) in %s", item->type->get_id().str(),
                                  context, container->type->get_id().str() );
                    } else if( container->is_tool() ) {
                        if( container->magazine_integral() ) {
                            if( !container->ammo_types().contains( item->ammo_type() ) &&
                                !container->ammo_types().contains( item->ammo_default()->ammo->type ) ) {
                                debugmsg( "Incompatible ammo: %s in ( %s %s )", item->type->get_id().str(),
                                          container->type->get_id().str(), context );
                            }
                        } else if( !container->ammo_types().empty() ) {
                            if( !container->ammo_types().contains( item->ammo_type() ) &&
                                !container->ammo_types().contains( item->ammo_default()->ammo->type ) ) {
                                debugmsg( "Incompatible ammo: %s in ( %s %s )", item->type->get_id().str(),
                                          container->type->get_id().str(), context );
                            }
                        } else if( container->ammo_default() != item->type->get_id() ) {
                            debugmsg( "Incompatible ammo: %s in ( %s %s )", item->type->get_id().str(),
                                      container->type->get_id().str(), context );
                        }
                    }
                }
            }
            if( modifier && modifier->contents != nullptr ) {
                for( auto &contents : modifier->contents->every_item_modified() ) {
                    // Exception as can_reload_with does not work for containers
                    // As it always returns true regardless of volume
                    if( item->is_container() ) {
                        if( !item->is_reloadable_with( contents->type->get_id() ) ) {
                            // Okay if we cant contain it first try nesting it
                            // Then if still failing enter debugmsg
                            contents = item::in_its_container( std::move( contents ) );
                            if( !item->is_reloadable_with( contents->type->get_id() ) ) {
                                debugmsg( "Incompatible contained item: %s in ( %s %s )", contents->type->get_id().str(),
                                          item->type->get_id().str(), context );
                            }
                        }
                        auto volume = contents->volume();
                        if( contents->count_by_charges() ) {
                            volume /= contents->charges;
                        }
                        auto maxmult = std::max( modifier->charges.second, modifier->count.second );
                        // If autofill put to 1
                        if( maxmult == -1 ) {
                            maxmult = 1;
                        }
                        if( item->get_container_capacity() < volume * maxmult ) {
                            debugmsg( "Incompatible contained size: %s in ( %s %s )", contents->type->get_id().str(),
                                      item->type->get_id().str(), context );
                        }
                    } else if( item->is_holster() ) {
                        // Need to keep these if statements seperate for ordering purposes
                        if( !item->can_holster( *contents ) ) {
                            debugmsg( "Incorrect holstered object object: ( %s %s ) in %s", contents->type->get_id().str(),
                                      context, item->type->get_id().str() );
                        }
                    } else if( !item->get_container_pockets().empty() ) {
                        if( !item->can_contain( *contents ).success() ) {
                            debugmsg( "Incorrect bandoliered object object: ( %s %s ) in %s", contents->type->get_id().str(),
                                      context, item->type->get_id().str() );
                        }
                    } else if( item->is_gun() ) {
                        if( !item->is_gunmod_compatible( *contents ).success() ) {
                            debugmsg( "Incompatible gunmod: %s in ( %s %s )", contents->type->get_id().str(),
                                      item->type->get_id().str(), context );
                        }
                    } else if( item->is_tool() ) {
                        // TODO: Make a unified check in item
                        // This is taken from iuse.cpp iuse::toolmod_attach
                        if( contents->is_toolmod() ) {
                            bool is_acceptable_ammo_mod = std::any_of( contents->type->mod->acceptable_ammo.begin(),
                            contents->type->mod->acceptable_ammo.end(), [&]( const ammotype & at ) {
                                return item->ammo_types( false ).count( at );
                            } );
                            if( ( contents->has_flag( flag_USE_UPS ) &&
                                  ( item->has_flag( flag_IS_UPS ) || item->has_flag( flag_USE_UPS ) ) ) ||
                                !item->toolmods().empty()  || !is_acceptable_ammo_mod ) {
                                debugmsg( "Incompatible toolmod: %s in ( %s %s )", contents->type->get_id().str(),
                                          item->type->get_id().str(), context );
                            }
                            if( item->magazine_current() ) {
                                debugmsg( "Tried to put a toolmod in a magazine containing item %s in ( %s %s )",
                                          contents->type->get_id().str(),
                                          item->type->get_id().str(), context );
                            }
                        } else {
                            debugmsg( "Tried to put %s in ( %s %s ) as a toolmod while it wasn't",
                                      contents->type->get_id().str(),
                                      item->type->get_id().str(), context );
                        }
                    }
                }
            }
        }
    }
}

bool Single_item_creator::remove_item( const itype_id &itemid )
{
    if( modifier ) {
        if( modifier->remove_item( itemid ) ) {
            type = S_NONE;
            return true;
        }
    }
    if( type == S_ITEM ) {
        if( itemid.str() == id ) {
            type = S_NONE;
            return true;
        }
    } else if( type == S_ITEM_GROUP ) {
        Item_spawn_data *isd = item_controller->get_group( item_group_id( id ) );
        if( isd != nullptr ) {
            isd->remove_item( itemid );
        }
    }
    return type == S_NONE;
}

bool Single_item_creator::replace_item( const itype_id &itemid, const itype_id &replacementid,
                                        const std::string &context )
{
    if( modifier ) {
        if( modifier->replace_item( itemid, replacementid, context ) ) {
            return true;
        }
    }
    if( type == S_ITEM ) {
        if( itemid.str() == id ) {
            if( get_option<bool>( "MIGRATION_CHECKS" ) ) {
                debugmsg( "Migrated item: %s in ( %s ), should be migrated to %s", itemid,
                          context, replacementid );
            }
            if( migrated_from.is_null() ) {
                migrated_from = itemid;
            }
            id = replacementid.str();
            return true;
        }
    } else if( type == S_ITEM_GROUP ) {
        Item_spawn_data *isd = item_controller->get_group( item_group_id( id ) );
        if( isd != nullptr ) {
            isd->replace_item( itemid, replacementid, "in itemgroup " + id );
        }
    }
    return type == S_NONE;
}

bool Single_item_creator::has_item( const itype_id &itemid ) const
{
    return type == S_ITEM && itemid.str() == id;
}

std::set<const itype *> Single_item_creator::every_item() const
{
    switch( type ) {
        case S_ITEM: {
            const itype *ptr = &*itype_id( id );
            return { ptr };
        }
        case S_ITEM_GROUP: {
            Item_spawn_data *isd = item_controller->get_group( item_group_id( id ) );
            if( isd != nullptr ) {
                return isd->every_item();
            }
            return {};
        }
        case S_NONE:
            return {};
    }
    assert( !"Unexpected type" );
    return {};
}

std::vector<detached_ptr<item>> Single_item_creator::every_item_modified( bool modify ) const
{
    std::vector<detached_ptr<item>> items;
    switch( type ) {
        case S_ITEM: {
            detached_ptr<item> itm = item::spawn( itype_id( id ) );
            if( modifier && modify && itm ) {
                items.push_back( modifier->modify( std::move( itm ) ) );
            } else if( modify && itm ) {
                items.push_back( item::in_its_container( std::move( itm ) ) );
            }
            return items;
        }
        case S_ITEM_GROUP: {
            auto group_id = item_group_id( id );
            // Check consistency so we get the consistency errors not the
            // I failed to place it right errors
            Item_spawn_data *isd = item_controller->get_group( group_id );
            if( !isd ) {
                debugmsg( "Invalid item group %s", id );
                return {};
            }
            if( isd && !isd->checked ) {
                isd->check_consistency( "" );
                isd->checked = true;
            }
            std::vector<detached_ptr<item>> item_group_items = isd->every_item_modified( true );
            items.reserve( items.size() + item_group_items.size() );
            for( auto &itm : item_group_items ) {
                if( modifier && modify ) {
                    items.push_back( modifier->modify( std::move( itm ) ) );
                } else if( modify ) {
                    items.push_back( item::in_its_container( std::move( itm ) ) );
                }
            }
            return items;
        }
        case S_NONE:
            return {};
    }
    assert( !"Unexpected type" );
    return {};
}

void Single_item_creator::inherit_ammo_mag_chances( const int ammo, const int mag )
{
    if( ammo != 0 || mag != 0 ) {
        if( !modifier ) {
            modifier.emplace();
        }
        modifier->with_ammo = ammo;
        modifier->with_magazine = mag;
    }
}

Item_modifier::Item_modifier()
    : damage( 0, 0 )
    , count( 1, 1 )
      // Dirt in guns is capped unless overwritten in the itemgroup
      // most guns should not be very dirty or dirty at all
    , dirt( 0, 500 )
    , charges( -1, -1 )
    , with_ammo( 0 )
    , with_magazine( 0 )
{
}

detached_ptr<item> Item_modifier::modify( detached_ptr<item> &&new_item ) const
{
    if( new_item->is_null() ) {
        return std::move( new_item );
    }

    new_item->set_damage( rng( damage.first, damage.second ) * itype::damage_scale );
    // no need for dirt if it's a bow
    if( new_item->is_gun() && !new_item->has_flag( flag_PRIMITIVE_RANGED_WEAPON ) &&
        !new_item->has_flag( flag_NON_FOULING ) ) {
        int random_dirt = rng( dirt.first, dirt.second );
        // if gun RNG is dirty, must add dirt fault to allow cleaning
        if( random_dirt > 0 ) {
            new_item->set_var( "dirt", random_dirt );
            new_item->faults.emplace( "fault_gun_dirt" );
            // chance to be unlubed, but only if it's not a laser or something
        } else if( one_in( 10 ) && !new_item->has_flag( flag_NEEDS_NO_LUBE ) ) {
            new_item->faults.emplace( "fault_gun_unlubricated" );
        }
    }

    // create container here from modifier or from default to get max charges later
    detached_ptr<item> cont;
    if( container != nullptr ) {
        cont = container->create_single( new_item->birthday() );
    }
    if( ( !cont || cont->is_null() ) && new_item->type->default_container.has_value() ) {
        const itype_id &cont_value = new_item->type->default_container.value_or( itype_id::NULL_ID() );
        if( !cont_value.is_null() ) {
            cont = item::spawn( cont_value, new_item->birthday() );
        }
    }

    int max_capacity = -1;
    if( charges.first != -1 && charges.second == -1 ) {
        const int max_ammo = new_item->ammo_capacity();
        if( max_ammo > 0 ) {
            max_capacity = max_ammo;
        }
    }

    if( max_capacity == -1 && cont != nullptr && !cont->is_null() && ( new_item->made_of( LIQUID ) ||
            ( !new_item->is_tool() && !new_item->is_gun() && !new_item->is_magazine() ) ) ) {
        max_capacity = new_item->charges_per_volume( cont->get_container_capacity() );
    }

    const bool charges_not_set = charges.first == -1 && charges.second == -1;
    int ch = -1;
    if( !charges_not_set ) {
        int charges_min = charges.first == -1 ? 0 : charges.first;
        int charges_max = charges.second == -1 ? max_capacity : charges.second;

        if( charges_min == -1 && charges_max != -1 ) {
            charges_min = 0;
        }

        if( max_capacity != -1 && ( charges_max > max_capacity || ( charges_min != 1 &&
                                    charges_max == -1 ) ) ) {
            charges_max = max_capacity;
        }

        if( charges_min > charges_max ) {
            charges_min = charges_max;
        }

        ch = charges_min == charges_max ? charges_min : rng( charges_min,
                charges_max );
    } else if( cont != nullptr && !cont->is_null() && new_item->made_of( LIQUID ) ) {
        new_item->charges = std::max( 1, max_capacity );
    }

    // CDF: hoisted from the DDA spawn block (item_group.cpp:625-626) so both blocks share it
    const std::vector<itype_id> well_defaults = new_item->magazines_default();
    const bool is_multi_well = well_defaults.size() > 1;

    if( ch != -1 && is_multi_well ) {
        // TODO(multimag): per-well charges JSON keys (e.g. well_charges: [N, M]).
        // CDF: no `context` in scope here, so the message has no "in %s" prefix
        debugmsg( "'charges' is ambiguous on multi-well item '%s'; use per-well JSON when available",
                  new_item->typeId().str() );
        ch = -1;
    }

    if( ch != -1 ) {
        if( new_item->count_by_charges() || new_item->made_of( LIQUID ) ) {
            // food, ammo
            // count_by_charges requires that charges is at least 1. It makes no sense to
            // spawn a "water (0)" item.
            new_item->charges = std::max( 1, ch );
        } else if( new_item->is_tool() ) {
            const int qty = std::min( ch, new_item->ammo_capacity() );
            new_item->charges = qty;
            if( !new_item->ammo_types().empty() && qty > 0 ) {
                new_item->ammo_set( new_item->ammo_default(), qty );
            }
        } else if( new_item->type->can_have_charges() ) {
            new_item->charges = ch;
        }
    }

    if( ch > 0 && ( new_item->is_gun() || new_item->is_magazine() ) ) {
        if( ammo == nullptr ) {
            // In case there is no explicit ammo item defined, use the default ammo
            if( !new_item->ammo_types().empty() ) {
                new_item->ammo_set( new_item->ammo_default(), ch );
            }
        } else {
            detached_ptr<item> am = ammo->create_single( new_item->birthday() );
            if( am ) {
                new_item->ammo_set( spawned_ammo_id( *am ), ch );
            }
        }
        // Make sure the item is in valid state
        if( new_item->ammo_data() && new_item->magazine_integral() ) {
            new_item->charges = std::min( new_item->charges, new_item->ammo_capacity() );
        } else {
            new_item->charges = 0;
        }
    }

    // CDF: DDA outer condition (item_group.cpp:687-688) is added to the old one, not substituted:
    // integral-ammo guns and charge tools have no well and must keep getting their ammo
    // (tests/item_group_test.cpp "item group ammo loading unwraps packaged ammunition")
    if( new_item->is_tool() || new_item->is_gun() || new_item->is_magazine() ||
        new_item->has_pocket_type( pocket_type::MAGAZINE_WELL ) ) {
        bool spawn_ammo = rng( 0, 99 ) < with_ammo && ch == -1 &&
                          ( !new_item->is_tool() || new_item->type->tool->rand_charges.empty() );
        // CDF: !magazine_integral() added as in DDA item_group.cpp:690
        bool spawn_mag  = rng( 0, 99 ) < with_magazine && !new_item->magazine_integral();

        if( is_multi_well ) {
            // CDF: DDA multi-well dispatch (item_group.cpp:689-698)
            // TODO(multimag): per-well ammo-chance / magazine-chance JSON keys.
            new_item->dress_magazine_wells( spawn_mag, spawn_ammo );
        } else {
            // CDF: the single-well guards stay as before, moved after the dispatch
            spawn_ammo = spawn_ammo && new_item->ammo_remaining() == 0;
            spawn_mag = spawn_mag && !new_item->magazine_current()
                        && new_item->magazine_default() != itype_id::NULL_ID();

            if( spawn_mag ) {
                new_item->put_in( item::spawn( new_item->magazine_default(), new_item->birthday() ) );
            }

            if( spawn_ammo ) {
                if( ammo ) {
                    detached_ptr<item> am = ammo->create_single( new_item->birthday() );
                    if( am ) {
                        new_item->ammo_set( spawned_ammo_id( *am ) );
                    }
                } else {
                    new_item->ammo_set( new_item->ammo_default() );
                }
            }
        }
    }

    if( cont != nullptr && !cont->is_null() ) {
        // per DDA item_group.cpp:716: an item that does not fit is lost (put_in reports it with
        // debugmsg); only group-level on_overflow can spill (D13)
        const pocket_type pk_type = guess_pocket_for( *cont, *new_item );
        cont->put_in( std::move( new_item ), pk_type );
        cont->add_automatic_whitelist();
        new_item = std::move( cont );
        if( sealed ) {
            new_item->seal();
        }
    }

    if( contents != nullptr ) {
        std::vector<detached_ptr<item>> contentitems = contents->create( new_item->birthday() );
        // Runs of identical items are put in together (DDA #87184). Only items that are the
        // same in every respect that matters qualify: non-liquid, not counted by charges,
        // stackable with the run's first item, equally old and equally rotten.
        for( size_t i = 0; i < contentitems.size(); ) {
            // custom code for directly attaching pockets to MOLLE vests (DDA item_group.cpp:730-748)
            if( const use_function *action = new_item->get_use( "attach_molle" );
                action != nullptr && contentitems[i]->can_attach_as_pocket() ) {
                const molle_attach_actor *actor = dynamic_cast<const molle_attach_actor *>
                                                  ( action->get_actor_ptr() );
                const int vacancies = actor != nullptr ?
                                      actor->size - new_item->contents.get_additional_space_used() : 0;
                // intentionally might lose items here if they don't fit this is because it's
                // impossible to know in a spawn group how much stuff could end up in it
                // if you roll 3, 3 size items in a 3 slot vest you shouldn't get an error
                // but you should get a vest with at least the first one
                if( contentitems[i]->get_pocket_size() <= vacancies ) {
                    new_item->contents.add_pocket( std::move( contentitems[i] ) );
                }
                i++;
                continue;
            }
            const item &first = *contentitems[i];
            size_t run = 1;
            if( !first.count_by_charges() && !first.made_of( LIQUID ) ) {
                while( i + run < contentitems.size() ) {
                    const item &next = *contentitems[i + run];
                    if( !first.stacks_with( next ) || first.age() != next.age() ||
                        first.get_rot() != next.get_rot() ) {
                        break;
                    }
                    run++;
                }
            }
            if( run > 1 ) {
                new_item->put_in_copies( first, static_cast<int>( run ) );
                for( size_t j = 0; j < run; j++ ) {
                    contentitems[i + j] = detached_ptr<item>();
                }
            } else {
                // per DDA item_group.cpp:746: checked insert, an item that does not fit is lost
                const pocket_type pk_type = guess_pocket_for( *new_item, first );
                new_item->put_in( std::move( contentitems[i] ), pk_type );
            }
            i += run;
        }
    }

    for( const flag_id &flag : custom_flags ) {
        new_item->set_flag( flag );
    }

    for( const auto &fn : postprocess_fns ) {
        new_item = fn( std::move( new_item ) );
    }
    return std::move( new_item );
}

void Item_modifier::check_consistency( const std::string &context ) const
{
    if( ammo != nullptr ) {
        ammo->check_consistency( "ammo of " + context );
    }
    if( container != nullptr ) {
        container->check_consistency( "container of " + context );
    }
    if( contents != nullptr ) {
        contents->check_consistency( "contents of " + context );
    }
    if( with_ammo < 0 || with_ammo > 100 ) {
        debugmsg( "Item modifier's ammo chance %d is out of range", with_ammo );
    }
    if( with_magazine < 0 || with_magazine > 100 ) {
        debugmsg( "Item modifier's magazine chance %d is out of range", with_magazine );
    }
}

bool Item_modifier::remove_item( const itype_id &itemid )
{
    if( ammo != nullptr ) {
        if( ammo->remove_item( itemid ) ) {
            ammo.reset();
        }
    }
    if( container != nullptr ) {
        if( container->remove_item( itemid ) ) {
            container.reset();
            return true;
        }
    }
    return false;
}

bool Item_modifier::replace_item( const itype_id &itemid, const itype_id &replacementid,
                                  const std::string &context )
{
    if( ammo != nullptr ) {
        ammo->replace_item( itemid, replacementid, "ammo of " + context );
    }
    if( container != nullptr ) {
        if( container->replace_item( itemid, replacementid, "container of " + context ) ) {
            return true;
        }
    }
    return false;
}

Item_group::Item_group( Type t, int probability, int ammo_chance, int magazine_chance )
    : Item_spawn_data( probability )
    , type( t )
    , with_ammo( ammo_chance )
    , with_magazine( magazine_chance )
    , sum_prob( 0 )
{
    if( probability <= 0 || ( t != Type::G_DISTRIBUTION && probability > 100 ) ) {
        debugmsg( "Probability %d out of range", probability );
    }
    if( ammo_chance < 0 || ammo_chance > 100 ) {
        debugmsg( "Ammo chance %d is out of range.", ammo_chance );
    }
    if( magazine_chance < 0 || magazine_chance > 100 ) {
        debugmsg( "Magazine chance %d is out of range.", magazine_chance );
    }
}

void Item_group::add_item_entry( const itype_id &itemid, int probability,
                                 const itype_id &migrated_from )
{
    std::unique_ptr<Single_item_creator> entry = std::make_unique<Single_item_creator>(
                itemid.str(), Single_item_creator::S_ITEM, probability );
    entry->migrated_from = migrated_from;
    add_entry( std::move( entry ) );
}

void Item_group::add_group_entry( const item_group_id &groupid, int probability )
{
    add_entry( std::make_unique<Single_item_creator>(
                   groupid.str(), Single_item_creator::S_ITEM_GROUP, probability ) );
}

void Item_group::add_entry( std::unique_ptr<Item_spawn_data> ptr )
{
    assert( ptr.get() != nullptr );
    if( ptr->probability <= 0 ) {
        return;
    }
    if( type == G_COLLECTION ) {
        ptr->probability = std::min( 100, ptr->probability );
    }
    sum_prob += ptr->probability;

    // Make the ammo and magazine probabilities from the outer entity apply to the nested entity:
    // If ptr is an Item_group, it already inherited its parent's ammo/magazine chances in its constructor.
    Single_item_creator *sic = dynamic_cast<Single_item_creator *>( ptr.get() );
    if( sic ) {
        sic->inherit_ammo_mag_chances( with_ammo, with_magazine );
    }
    items.push_back( std::move( ptr ) );
}

std::vector<detached_ptr<item>> Item_group::create( const time_point &birthday,
                             RecursionList &rec ) const
{
    std::vector<detached_ptr<item>> result;
    if( type == G_COLLECTION ) {
        for( const auto &elem : items ) {
            if( rng( 0, 99 ) >= ( elem )->probability ) {
                continue;
            }
            std::vector<detached_ptr<item>> tmp = ( elem )->create( birthday, rec );
            result.insert( result.end(), std::make_move_iterator( tmp.begin() ),
                           std::make_move_iterator( tmp.end() ) );
        }
    } else if( type == G_DISTRIBUTION ) {
        int p = rng( 0, sum_prob - 1 );
        for( const auto &elem : items ) {
            p -= ( elem )->probability;
            if( p >= 0 ) {
                continue;
            }
            std::vector<detached_ptr<item>> tmp = ( elem )->create( birthday, rec );
            result.insert( result.end(), std::make_move_iterator( tmp.begin() ),
                           std::make_move_iterator( tmp.end() ) );
            break;
        }
    }
    put_into_container( result, result.size(), container_item, container_item_variant, sealed,
                        birthday, on_overflow, "item group" );

    return result;
}

detached_ptr<item> Item_group::create_single( const time_point &birthday, RecursionList &rec ) const
{
    if( type == G_COLLECTION ) {
        for( const auto &elem : items ) {
            if( rng( 0, 99 ) >= ( elem )->probability ) {
                continue;
            }
            return ( elem )->create_single( birthday, rec );
        }
    } else if( type == G_DISTRIBUTION ) {
        int p = rng( 0, sum_prob - 1 );
        for( const auto &elem : items ) {
            p -= ( elem )->probability;
            if( p >= 0 ) {
                continue;
            }
            return ( elem )->create_single( birthday, rec );
        }
    }
    return detached_ptr<item>();
}

void Item_group::check_consistency( const std::string &context ) const
{
    if( on_overflow != overflow_behaviour::none && !container_item ) {
        debugmsg( "item group %s specifies overflow behaviour but not container", context );
    }
    for( const auto &elem : items ) {
        ( elem )->check_consistency( context );
    }
}

bool Item_group::remove_item( const itype_id &itemid )
{
    for( prop_list::iterator a = items.begin(); a != items.end(); ) {
        if( ( *a )->remove_item( itemid ) ) {
            sum_prob -= ( *a )->probability;
            a = items.erase( a );
        } else {
            ++a;
        }
    }
    if( container_item && *container_item == itemid ) {
        container_item = std::nullopt;
        on_overflow = overflow_behaviour::none;
    }
    return items.empty();
}

bool Item_group::remove_specific_item( const std::string &itemid )
{
    for( prop_list::iterator a = items.begin(); a != items.end(); ) {
        auto b = dynamic_cast<Single_item_creator *>( ( &*a )->get() );
        if( b == nullptr ) {
            ++a;
        } else if( b->type == Single_item_creator::Type::S_ITEM ) {
            if( itemid == b->id ) {
                sum_prob -= ( *a )->probability;
                a = items.erase( a );
                return true;
            }
            ++a;
        } else {
            ++a;
        }
    }
    return items.empty();
}

bool Item_group::remove_specific_group( const std::string &itemid )
{
    for( prop_list::iterator a = items.begin(); a != items.end(); ) {
        auto b = dynamic_cast<Single_item_creator *>( ( &*a )->get() );
        if( b == nullptr ) {
            ++a;
        } else if( b->type == Single_item_creator::Type::S_ITEM_GROUP ) {
            if( itemid == b->id ) {
                sum_prob -= ( *a )->probability;
                a = items.erase( a );
                return true;
            }
            ++a;
        } else {
            ++a;
        }
    }
    return items.empty();
}

bool Item_group::replace_item( const itype_id &itemid, const itype_id &replacementid,
                               const std::string &context )
{
    for( const std::unique_ptr<Item_spawn_data> &elem : items ) {
        ( elem )->replace_item( itemid, replacementid, "item in " + context );
    }
    return items.empty();
}

bool Item_group::has_item( const itype_id &itemid ) const
{
    for( const std::unique_ptr<Item_spawn_data> &elem : items ) {
        if( ( elem )->has_item( itemid ) ) {
            return true;
        }
    }
    return false;
}

std::set<const itype *> Item_group::every_item() const
{
    std::set<const itype *> result;
    for( const auto &spawn_data : items ) {
        std::set<const itype *> these_items = spawn_data->every_item();
        result.insert( these_items.begin(), these_items.end() );
    }
    return result;
}

std::vector<detached_ptr<item>> Item_group::every_item_modified( bool /*modify*/ ) const
{
    std::vector<detached_ptr<item>> result;
    for( const auto &spawn_data : items ) {
        auto these_items = spawn_data->every_item_modified();
        result.reserve( result.size() + these_items.size() );
        for( auto &itm : these_items ) {
            result.push_back( std::move( itm ) );
        }
    }
    return result;
}

std::vector<detached_ptr<item>> item_group::items_from( const item_group_id &group_id,
                             const time_point &birthday )
{
    const auto group = item_controller->get_group( group_id );
    if( group == nullptr ) {
        return {};
    }
    return group->create( birthday );
}

std::vector<detached_ptr<item>> item_group::items_from( const item_group_id &group_id )
{
    return items_from( group_id, calendar::start_of_cataclysm );
}

detached_ptr<item> item_group::item_from( const item_group_id &group_id,
        const time_point &birthday )
{
    const auto group = item_controller->get_group( group_id );
    if( group == nullptr ) {
        return detached_ptr<item>();
    }
    return group->create_single( birthday );
}

detached_ptr<item> item_group::item_from( const item_group_id &group_id )
{
    return item_from( group_id, calendar::start_of_cataclysm );
}

bool item_group::group_is_defined( const item_group_id &group_id )
{
    return item_controller->get_group( group_id ) != nullptr;
}

bool item_group::group_contains_item( const item_group_id &group_id, const itype_id &type_id )
{
    const auto group = item_controller->get_group( group_id );
    if( group == nullptr ) {
        return false;
    }
    return group->has_item( type_id );
}

std::set<const itype *> item_group::every_possible_item_from( const item_group_id &group_id )
{
    Item_spawn_data *group = item_controller->get_group( group_id );
    if( group == nullptr ) {
        return {};
    }
    return group->every_item();
}

void item_group::load_item_group( const JsonObject &jsobj, const item_group_id &group_id,
                                  const std::string &subtype )
{
    item_controller->load_item_group( jsobj, group_id, subtype );
}

static item_group_id get_unique_group_id()
{
    // This is just a hint what id to use next. Overflow of it is defined and if the group
    // name is already used, we simply go the next id.
    static unsigned int next_id = 0;
    // Prefixing with a character outside the ASCII range, so it is hopefully unique and
    // (if actually printed somewhere) stands out. Theoretically those auto-generated group
    // names should not be seen anywhere.
    static const std::string unique_prefix = "\u01F7 ";
    while( true ) {
        const item_group_id new_group = item_group_id( unique_prefix + std::to_string( next_id++ ) );
        if( !item_group::group_is_defined( new_group ) ) {
            return new_group;
        }
    }
}

item_group_id item_group::load_item_group( const JsonValue &value,
        const std::string &default_subtype )
{
    if( value.test_string() ) {
        return item_group_id( value.get_string() );
    } else if( value.test_object() ) {
        const item_group_id group = get_unique_group_id();

        JsonObject jo = value.get_object();
        const std::string subtype = jo.get_string( "subtype", default_subtype );
        item_controller->load_item_group( jo, group, subtype );

        return group;
    } else if( value.test_array() ) {
        const item_group_id group = get_unique_group_id();

        JsonArray jarr = value.get_array();
        // load_item_group needs a bool, invalid subtypes are unexpected and most likely errors
        // from the caller of this function.
        if( default_subtype != "collection" && default_subtype != "distribution" ) {
            debugmsg( "invalid subtype for item group: %s", default_subtype.c_str() );
        }
        item_controller->load_item_group( jarr, group, default_subtype == "collection", 0, 0 );

        return group;
    } else {
        value.throw_error( "invalid item group, must be string (group id) or object/array (the group data)" );
        // stream.error always throws, this is here to prevent a warning
        return item_group_id{};
    }
}
