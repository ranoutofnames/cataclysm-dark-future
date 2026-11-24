#include "visitable.h"

#include "active_item_cache.h"
#include "bionics.h"
#include "character.h"
#include "debug.h"
#include "inventory.h"
#include "item.h"
#include "item_contents.h"
#include "itype.h"
#include "make_static.h"
#include "map/map.h"
#include "map/map_selector.h"
#include "map/submap.h"
#include "monster.h"
#include "mtype.h"
#include "mutation.h"
#include "pimpl.h"
#include "temp_crafting_inventory.h"
#include "player.h"
#include "point.h"
#include "type_id.h"
#include "units.h"
#include "value_ptr.h"
#include "vehicle/veh_type.h"
#include "vehicle/vehicle.h"
#include "vehicle/vehicle_part.h"
#include "vehicle/vehicle_selector.h"
#include "itype_slots/islot_tool.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <unordered_map>
#include <utility>

static const itype_id itype_apparatus( "apparatus" );
static const itype_id itype_toolset( "toolset" );
static const itype_id itype_voltmeter_bionic( "voltmeter_bionic" );
static const itype_id itype_UPS( "UPS" );
static const itype_id itype_bio_armor( "bio_armor" );

static const quality_id qual_BUTCHER( "BUTCHER" );

static const bionic_id bio_tools( "bio_tools" );
static const bionic_id bio_electrosense_voltmeter( "bio_electrosense_voltmeter" );
static const bionic_id bio_ups( "bio_ups" );

static const flag_id flag_BIONIC_ARMOR_INTERFACE( "BIONIC_ARMOR_INTERFACE" );
static const flag_id flag_IS_UPS( "IS_UPS" );
static const flag_id flag_BIONIC_TOOLS( "BIONIC_TOOLS" );
static const flag_id flag_ENCHANTMENT_TOOLS( "ENCHANTMENT_TOOLS" );
static const flag_id flag_USES_BIONIC_POWER( "USES_BIONIC_POWER" );
static const flag_id flag_USE_UPS( "USE_UPS" );

/** @relates visitable */
template <typename T>
item *visitable<T>::find_parent( const item &it )
{
    item *res = nullptr;
    if( visit_items( [&]( item * node, item * parent ) {
    if( node == &it ) {
            res = parent;
            return VisitResponse::ABORT;
        }
        return VisitResponse::NEXT;
    } ) != VisitResponse::ABORT ) {
        debugmsg( "Tried to find item parent using an object that doesn't contain it" );
    }
    return res;
}

/** @relates visitable */
template <typename T>
const item *visitable<T>::find_parent( const item &it ) const
{
    return const_cast<visitable<T> *>( this )->find_parent( it );
}

/** @relates visitable */
template <typename T>
std::vector<item *> visitable<T>::parents( const item &it )
{
    std::vector<item *> res;
    for( item *obj = find_parent( it ); obj; obj = find_parent( *obj ) ) {
        res.push_back( obj );
    }
    return res;
}

/** @relates visitable */
template <typename T>
std::vector<const item *> visitable<T>::parents( const item &it ) const
{
    std::vector<const item *> res;
    for( const item *obj = find_parent( it ); obj; obj = find_parent( *obj ) ) {
        res.push_back( obj );
    }
    return res;
}

/** @relates visitable */
template <typename T>
bool visitable<T>::has_item( const item &it ) const
{
    return visit_items( [&it]( const item * node ) {
        return node == &it ? VisitResponse::ABORT : VisitResponse::NEXT;
    } ) == VisitResponse::ABORT;
}

/** @relates visitable */
template <typename T>
bool visitable<T>::has_item_with( const std::function<bool( const item & )> &filter ) const
{
    return visit_items( [&filter]( const item * node ) {
        return filter( *node ) ? VisitResponse::ABORT : VisitResponse::NEXT;
    } ) == VisitResponse::ABORT;
}

/** @relates visitable */
template <typename T>
bool visitable<T>::has_item_directly( const item &it ) const
{
    return visit_items( [&it]( const item * node ) {
        return node == &it ? VisitResponse::ABORT : VisitResponse::SKIP;
    } ) == VisitResponse::ABORT;
}

/** @relates visitable */
template <typename T>
bool visitable<T>::has_item_with_directly( const std::function<bool( const item & )> &filter ) const
{
    return visit_items( [&filter]( const item * node ) {
        return filter( *node ) ? VisitResponse::ABORT : VisitResponse::SKIP;
    } ) == VisitResponse::ABORT;
}

/** Sums the two terms, being careful to not trigger overflow.
 * Doesn't handle underflow.
 *
 * @param a The first addend.
 * @param b The second addend.
 * @return the sum of the addends, but truncated to std::numeric_limits<int>::max().
 */
template <typename T>
static T sum_no_wrap( T a, T b )
{
    if( a > std::numeric_limits<T>::max() - b ||
        b > std::numeric_limits<T>::max() - a ) {
        return std::numeric_limits<T>::max();
    }
    return a + b;
}

template <typename T>
static int has_quality_internal( const T &self, const quality_id &qual, int level, int limit )
{
    int qty = 0;

    self.visit_items( [&qual, level, &limit, &qty]( const item * e ) {
        if( e->get_quality( qual ) >= level ) {
            qty = sum_no_wrap( qty, e->count() );
            if( qty >= limit ) {
                // found sufficient items
                return VisitResponse::ABORT;
            }
        }
        return VisitResponse::NEXT;
    } );
    return std::min( qty, limit );
}

static int has_quality_from_vpart( const vehicle &veh, int part, const quality_id &qual, int level,
                                   int limit )
{
    int qty = 0;

    auto pos = veh.cpart( part ).mount;
    for( const auto &n : veh.parts_at_relative( pos, true ) ) {

        // only unbroken parts can provide tool qualities
        if( !veh.cpart( n ).is_broken() ) {
            auto tq = veh.part_info( n ).qualities;
            auto iter = tq.find( qual );

            // does the part provide this quality?
            if( iter != tq.end() && iter->second >= level ) {
                qty = sum_no_wrap( qty, 1 );
            }
        }
    }
    return std::min( qty, limit );
}

template <typename T>
bool visitable<T>::has_quality( const quality_id &qual, int level, int qty ) const
{
    return has_quality_internal( *this, qual, level, qty ) == qty;
}

/** @relates visitable */
template <>
bool visitable<inventory>::has_quality( const quality_id &qual, int level, int qty ) const
{
    const inventory *inv = static_cast<const inventory *>( this );
    const std::map<quality_id, std::map<int, int>> &inv_qual_cache = inv->get_quality_cache();
    int res = 0;
    if( !inv_qual_cache.empty() ) {
        auto iter = inv_qual_cache.find( qual );
        if( iter != inv_qual_cache.end() ) {
            for( const auto &q : iter->second ) {
                if( q.first >= level ) {
                    res = sum_no_wrap( res, q.second );
                }
            }
        }
        return res >= qty;
    }
    for( const auto &stack : inv->items ) {
        res += stack.size() * has_quality_internal( *stack.front(), qual, level, qty );
        if( res >= qty ) {
            return true;
        }
    }
    return false;
}

// CDF: same counting as the inventory quality cache (own qualities of each visited item,
// by charges), so requirement checks answer as they did on the flat inventory
template <>
bool visitable<temp_crafting_inventory>::has_quality( const quality_id &qual, int level,
        int qty ) const
{
    int res = 0;
    visit_items( [&]( const item * e ) {
        const std::map<quality_id, int> item_qualities = e->get_qualities();
        const auto iter = item_qualities.find( qual );
        if( iter != item_qualities.end() && iter->second >= level ) {
            res = sum_no_wrap( res, e->count_by_charges() ? e->charges : 1 );
        }
        return res >= qty ? VisitResponse::ABORT : VisitResponse::NEXT;
    } );
    return res >= qty;
}

template <>
bool visitable<location_inventory>::has_quality( const quality_id &qual, int level, int qty ) const
{

    auto inv = static_cast<const location_inventory *>( this );
    return inv->inv.has_quality( qual, level, qty );
}

/** @relates visitable */
template <>
bool visitable<vehicle_selector>::has_quality( const quality_id &qual, int level, int qty ) const
{
    for( const auto &cursor : static_cast<const vehicle_selector &>( *this ) ) {
        if( cursor.ignore_vpart ) {
            continue;
        }

        qty -= has_quality_from_vpart( cursor.veh, cursor.part, qual, level, qty );
        if( qty <= 0 ) {
            return true;
        }
    }
    return has_quality_internal( *this, qual, level, qty ) == qty;
}

/** @relates visitable */
template <>
bool visitable<vehicle_cursor>::has_quality( const quality_id &qual, int level, int qty ) const
{
    auto self = static_cast<const vehicle_cursor *>( this );

    if( !self->ignore_vpart ) {
        qty -= has_quality_from_vpart( self->veh, self->part, qual, level, qty );
    }
    return qty <= 0 ? true : has_quality_internal( *this, qual, level, qty ) == qty;
}

/** @relates visitable */
template <>
bool visitable<Character>::has_quality( const quality_id &qual, int level, int qty ) const
{
    auto self = static_cast<const Character *>( this );

    for( const auto &bio : *self->my_bionics ) {
        if( bio.get_quality( qual ) >= level ) {
            if( qty <= 1 ) {
                return true;
            }

            qty--;
        }
    }
    for( const auto it : self->get_enchantment_fake_items() ) {
        for( const auto &[itqual, lev] : it->qualities ) {
            if( qual != itqual || lev < level ) { continue; }
            if( qty <= 1 ) {
                return true;
            }
            qty--;
        }
    }
    if( qual == qual_BUTCHER ) {
        for( const trait_id &mut : self->get_mutations() ) {
            if( mut->butchering_quality > level ) {
                if( qty <= 1 ) { return true; }
                qty--;
            }
        }
    }

    return qty <= 0 ? true : has_quality_internal( *this, qual, level, qty ) == qty;
}

template <typename T>
static int max_quality_internal( const T &self, const quality_id &qual )
{
    int res = INT_MIN;
    self.visit_items( [&res, &qual]( const item * e ) {
        res = std::max( res, e->get_quality( qual ) );
        return VisitResponse::NEXT;
    } );
    return res;
}

static int max_quality_from_vpart( const vehicle &veh, int part, const quality_id &qual )
{
    int res = INT_MIN;

    auto pos = veh.cpart( part ).mount;
    for( const auto &n : veh.parts_at_relative( pos, true ) ) {

        // only unbroken parts can provide tool qualities
        if( !veh.cpart( n ).is_broken() ) {
            auto tq = veh.part_info( n ).qualities;
            auto iter = tq.find( qual );

            // does the part provide this quality?
            if( iter != tq.end() ) {
                res = std::max( res, iter->second );
            }
        }
    }
    return res;
}

template <typename T>
int visitable<T>::max_quality( const quality_id &qual ) const
{
    return max_quality_internal( *this, qual );
}

/** @relates visitable */
template<>
int visitable<Character>::max_quality( const quality_id &qual ) const
{
    int res = INT_MIN;

    auto self = static_cast<const Character *>( this );

    for( const auto &bio : *self->my_bionics ) {
        res = std::max( res, bio.get_quality( qual ) );
    }
    for( const auto it : self->get_enchantment_fake_items() ) {
        if( it->qualities.contains( qual ) ) {
            res = std::max( res, it->qualities.at( qual ) );
        }
    }

    if( qual == qual_BUTCHER ) {
        for( const trait_id &mut : self->get_mutations() ) {
            if( mut->butchering_quality > 0 ) {
                res = std::max( res, mut->butchering_quality );
            }
        }
    }

    return std::max( res, max_quality_internal( *this, qual ) );
}

/** @relates visitable */
template <>
int visitable<vehicle_cursor>::max_quality( const quality_id &qual ) const
{
    auto self = static_cast<const vehicle_cursor *>( this );
    int vpart = self->ignore_vpart ? 0 : max_quality_from_vpart( self->veh, self->part, qual );
    return std::max( vpart, max_quality_internal( *this, qual ) );
}

/** @relates visitable */
template <>
int visitable<vehicle_selector>::max_quality( const quality_id &qual ) const
{
    int res = INT_MIN;
    for( const auto &e : static_cast<const vehicle_selector &>( *this ) ) {
        res = std::max( res, e.max_quality( qual ) );
    }
    return res;
}

/** @relates visitable */
template <typename T>
std::vector<item *> visitable<T>::items_with( const std::function<bool( const item & )> &filter )
const
{
    std::vector<item *> res;
    visit_items( [&res, &filter]( const item * node, const item * ) {
        if( filter( *node ) ) {
            res.push_back( const_cast<item *>( node ) );
        }
        return VisitResponse::NEXT;
    } );
    return res;
}

/** @relates visitable */
template <typename T>
VisitResponse
visitable<T>::visit_items( const std::function<VisitResponse( const item *,
                           const item * )> &func ) const
{
    return const_cast<visitable<T> *>( this )->visit_items(
               static_cast<const std::function<VisitResponse( item *, item * )>&>( func ) );
}

/** @relates visitable */
template <typename T>
VisitResponse visitable<T>::visit_items( const std::function<VisitResponse( const item * )> &func )
const
{
    return visit_items( [&func]( const item * it, const item * ) {
        return func( it );
    } );
}

/** @relates visitable */
template <typename T>
VisitResponse visitable<T>::visit_items( const std::function<VisitResponse( item * )> &func )
{
    return visit_items( [&func]( item * it, item * ) {
        return func( it );
    } );
}



namespace
{

auto visit_internal( std::function < VisitResponse( detached_ptr<item> &&e ) > filter,
                     location_vector<item> &items, VisitResponse &last ) -> bool
{
    auto processing_changed = false;
    items.remove_with( [&last, &processing_changed, &filter]( detached_ptr<item> &&e ) {
        if( last == VisitResponse::ABORT ) {
            return std::move( e );
        }
        const auto needs_processing_before = e != nullptr && e->needs_processing();
        last = filter( std::move( e ) );
        // NOLINTNEXTLINE(bugprone-use-after-move)
        if( e != nullptr && e->needs_processing() != needs_processing_before ) {
            processing_changed = true;
        }
        // NOLINTNEXTLINE(bugprone-use-after-move)
        if( last == VisitResponse::NEXT && e ) {
            e->contents.remove_items_with( [&last, &filter]( detached_ptr<item> &&e ) {
                if( last == VisitResponse::ABORT ) {
                    return last;
                }
                last = filter( std::move( e ) );
                return last;
            } );
        }
        return std::move( e );
    } );
    return processing_changed;
}

auto visit_internal( std::function < VisitResponse( detached_ptr<item> &&e ) > filter,
                     location_vector<item> &items ) -> bool
{
    auto last = VisitResponse::NEXT;
    return visit_internal( std::move( filter ), items, last );
}

} // namespace

// Specialize visitable<T>::visit_items() for each class that will implement the visitable interface

static VisitResponse visit_internal( const std::function<VisitResponse( item *, item * )> &func,
                                     item *node, item *parent = nullptr )
{
    switch( func( node, parent ) ) {
        case VisitResponse::ABORT:
            return VisitResponse::ABORT;

        case VisitResponse::NEXT:
            // CDF: DDA visitable.cpp has no skip; a tool with ammo is is_magazine() (D13), lookup R2/R3
            if( node->is_gun() || ( node->is_magazine() && !node->is_tool() ) ) {
                // Content of guns and magazines are accessible only via their specific accessors
                return VisitResponse::NEXT;
            }

            if( node->contents.visit_contents( func, node ) == VisitResponse::ABORT ) {
                return VisitResponse::ABORT;
            }
        /* intentional fallthrough */

        case VisitResponse::SKIP:
            return VisitResponse::NEXT;
    }

    /* never reached but suppresses GCC warning */
    return VisitResponse::ABORT;
}

// CDF (CAP1d): the pockets whose items are listed as carried: the carried storage and the holster
// pockets, container pockets only. A tool's own ammo or magazine pocket is never one of them, so a
// loaded tool's ammo stays visible only to the tool-aware lookups (in_tools).
static auto is_listed_pocket( const item_pocket &pocket ) -> bool
{
    return ( pocket.is_carry() || pocket.is_holster() ) && pocket.is_type( pocket_type::CONTAINER );
}

VisitResponse item_contents::visit_contents( const std::function<VisitResponse( item *, item * )>
        &func, item *parent, bool skip_carry )
{
    for( item_pocket &pocket : pockets ) {
        // CDF: E1-2: installed files are not carried items: no walk (usable, countable, craftable) reaches them
        if( pocket.is_type( pocket_type::E_FILE_STORAGE ) ) {
            continue;
        }
        // CDF: holster pockets are listed with the carried items (see is_listed_pocket)
        if( skip_carry && is_listed_pocket( pocket ) ) {
            continue;
        }
        for( item *&e : pocket.items() ) {
            switch( visit_internal( func, e, parent ) ) {
                case VisitResponse::ABORT:
                    return VisitResponse::ABORT;
                default:
                    break;
            }
        }
    }
    return VisitResponse::NEXT;
}

/** @relates visitable */
template <>
VisitResponse visitable<item>::visit_items(
    const std::function<VisitResponse( item *, item * )> &func )
{
    auto it = static_cast<item *>( this );
    return visit_internal( func, it );
}

/** @relates visitable */
// CDF: temp_crafting_inventory (J01) walks its stored top-level references
template <>
VisitResponse visitable<temp_crafting_inventory>::visit_items(
    const std::function<VisitResponse( item *, item * )> &func )
{
    auto inv = static_cast<const temp_crafting_inventory *>( this );
    for( item *it : inv->top_items() ) {
        if( visit_internal( func, it ) == VisitResponse::ABORT ) {
            return VisitResponse::ABORT;
        }
    }
    return VisitResponse::NEXT;
}

/** @relates visitable */
template <>
VisitResponse visitable<inventory>::visit_items(
    const std::function<VisitResponse( item *, item * )> &func )
{
    auto inv = static_cast<const inventory *>( this );
    for( auto &stack : inv->items ) {
        for( auto &it : stack ) {
            if( visit_internal( func, it ) == VisitResponse::ABORT ) {
                return VisitResponse::ABORT;
            }
        }
    }
    return VisitResponse::NEXT;
}

template <>
VisitResponse visitable<location_inventory>::visit_items(
    const std::function<VisitResponse( item *, item * )> &func )
{
    auto inv = static_cast<location_inventory *>( this );
    return inv->inv.visit_items( func );
}

/** @relates visitable */
template <>
VisitResponse visitable<Character>::visit_items(
    const std::function<VisitResponse( item *, item * )> &func )
{
    auto ch = static_cast<Character *>( this );

    if( !ch->primary_weapon().is_null() &&
        visit_internal( func, &ch->primary_weapon() ) == VisitResponse::ABORT ) {
        return VisitResponse::ABORT;
    }

    // Worn items first. What is carried in their `carry` pockets is visited afterwards as the
    // top-level items it used to be, so callers that only look at the top level (returning SKIP)
    // still see the carried items.
    for( auto &e : ch->worn ) {
        switch( func( e, nullptr ) ) {
            case VisitResponse::ABORT:
                return VisitResponse::ABORT;
            case VisitResponse::NEXT:
                // CDF: DDA visitable.cpp has no skip; a tool with ammo is is_magazine() (D13), lookup R2/R3
                if( !e->is_gun() && !( e->is_magazine() && !e->is_tool() ) &&
                    e->contents.visit_contents( func, e, true ) == VisitResponse::ABORT ) {
                    return VisitResponse::ABORT;
                }
                break;
            case VisitResponse::SKIP:
                break;
        }
    }

    const auto visit_carried = [&func]( item & holder ) {
        for( item_pocket &pocket : holder.contents.get_pockets() ) {
            if( !is_listed_pocket( pocket ) ) {
                continue;
            }
            for( item *&carried_it : pocket.items() ) {
                if( visit_internal( func, carried_it ) == VisitResponse::ABORT ) {
                    return VisitResponse::ABORT;
                }
            }
        }
        return VisitResponse::NEXT;
    };
    for( auto &e : ch->worn ) {
        if( visit_carried( *e ) == VisitResponse::ABORT ) {
            return VisitResponse::ABORT;
        }
    }
    if( item *storage = ch->get_body_storage() ) {
        if( visit_carried( *storage ) == VisitResponse::ABORT ) {
            return VisitResponse::ABORT;
        }
    }

    return VisitResponse::NEXT;
}

/** @relates visitable */
template <>
VisitResponse visitable<map_cursor>::visit_items(
    const std::function<VisitResponse( item *, item * )> &func )
{
    auto cur = static_cast<map_cursor *>( this );
    map &here = get_map();
    // skip inaccessible items
    if( here.has_flag( "SEALED", *cur ) && !here.has_flag( "LIQUIDCONT", *cur ) ) {
        return VisitResponse::NEXT;
    }

    for( item *&e : here.i_at( *cur ) ) {
        if( visit_internal( func, e ) == VisitResponse::ABORT ) {
            return VisitResponse::ABORT;
        }
    }
    return VisitResponse::NEXT;
}

/** @relates visitable */
template <>
VisitResponse visitable<map_selector>::visit_items(
    const std::function<VisitResponse( item *, item * )> &func )
{
    for( auto &cursor : static_cast<map_selector &>( *this ) ) {
        if( cursor.visit_items( func ) == VisitResponse::ABORT ) {
            return VisitResponse::ABORT;
        }
    }
    return VisitResponse::NEXT;
}

/** @relates visitable */
template <>
VisitResponse visitable<vehicle_cursor>::visit_items(
    const std::function<VisitResponse( item *, item * )> &func )
{
    auto self = static_cast<vehicle_cursor *>( this );

    int idx = self->veh.part_with_feature( self->part, "CARGO", true );
    if( idx >= 0 ) {
        for( auto *&e : self->veh.get_items( idx ) ) {
            if( visit_internal( func, e ) == VisitResponse::ABORT ) {
                return VisitResponse::ABORT;
            }
        }
    }
    return VisitResponse::NEXT;
}

/** @relates visitable */
template <>
VisitResponse visitable<vehicle_selector>::visit_items(
    const std::function<VisitResponse( item *, item * )> &func )
{
    for( auto &cursor : static_cast<vehicle_selector &>( *this ) ) {
        if( cursor.visit_items( func ) == VisitResponse::ABORT ) {
            return VisitResponse::ABORT;
        }
    }
    return VisitResponse::NEXT;
}

/** @relates visitable */
template <>
VisitResponse visitable<monster>::visit_items(
    const std::function<VisitResponse( item *, item * )> &func )
{
    monster *mon = static_cast<monster *>( this );

    for( item * const &it : mon->get_items() ) {
        if( visit_internal( func, it ) == VisitResponse::ABORT ) {
            return VisitResponse::ABORT;
        }
    }

    if( mon->get_storage_item() &&
        visit_internal( func, mon->get_storage_item() ) == VisitResponse::ABORT ) {
        return VisitResponse::ABORT;
    }
    if( mon->get_armor_item() &&
        visit_internal( func, mon->get_armor_item() ) == VisitResponse::ABORT ) {
        return VisitResponse::ABORT;
    }
    if( mon->get_tack_item() &&
        visit_internal( func, mon->get_tack_item() ) == VisitResponse::ABORT ) {
        return VisitResponse::ABORT;
    }
    if( mon->get_tied_item() &&
        visit_internal( func, mon->get_tied_item() ) == VisitResponse::ABORT ) {
        return VisitResponse::ABORT;
    }
    if( mon->get_battery_item() &&
        visit_internal( func, mon->get_battery_item() ) == VisitResponse::ABORT ) {
        return VisitResponse::ABORT;
    }

    return VisitResponse::NEXT;
}

// Specialize visitable<T>::remove_items_with() for each class that will implement the visitable interface

/** @relates visitable */
template <typename T>
void temp_visitable<T>::remove_item( item &it )
{
    auto obj = remove_items_with( [&it]( const item & e ) {
        return &e == &it;
    } );
    if( obj.empty() ) {
        debugmsg( "Tried removing item from object which did not contain it" );
    }
}

template <typename T>
detached_ptr<item> location_visitable<T>::remove_item( item &it )
{
    detached_ptr<item> ret;
    remove_items_with( [&it, &ret]( detached_ptr<item> &&e ) {
        if( &*e == &it ) {
            ret = std::move( e );
            return VisitResponse::ABORT;
        }
        return VisitResponse::NEXT;
    } );
    if( !ret ) {
        debugmsg( "Tried removing item from object which did not contain it" );
    }
    return ret;
}

void item_contents::remove_items_with( const std::function < VisitResponse(
        detached_ptr<item> && ) > &filter )
{
    const auto old_size = num_item_stacks();
    // CDF: removing a mod that changes the layout rebuilds it, as remove_top does
    const bool had_layout_mod = has_adaptor_mod();
    auto last = VisitResponse::NEXT;
    auto processing_changed = false;
    for( item_pocket &pocket : pockets ) {
        if( last == VisitResponse::ABORT ) {
            break;
        }
        processing_changed |= visit_internal( filter, pocket.items(), last );
    }
    if( had_layout_mod ) {
        rebuild_pockets();
    }
    if( num_item_stacks() == old_size && !processing_changed ) {
        return;
    }
    contents_changed();
}

/** @relates visitable */
template <>
std::vector<item *> temp_visitable<inventory>::remove_items_with( const
        std::function<bool( const item &e )> &filter, int count )
{
    auto inv = static_cast<inventory *>( this );
    std::vector<item *> res;
    if( inv->locked ) {
        debugmsg( "Attempted recursive item removal" );
        return res;
    }

    if( count <= 0 ) {
        // nothing to do
        return res;
    }
    inv->locked = true;
    for( auto stack = inv->items.begin(); stack != inv->items.end() && count > 0; ) {
        std::vector<item *> &istack = *stack;
        for( auto istack_iter = istack.begin(); istack_iter != istack.end() && count > 0; ) {
            if( filter( **istack_iter ) ) {
                count--;
                res.push_back( *istack_iter );
                istack_iter = istack.erase( istack_iter );
            } else {
                ++istack_iter;
            }
        }

        if( istack.empty() ) {
            stack = inv->items.erase( stack );
        } else {
            ++stack;
        }
    }

    // Invalidate binning cache
    inv->binned = false;
    inv->items_type_cached = false;
    inv->locked = false;
    return res;
}

// CDF: forwards to temp_crafting_inventory::remove_items_with (reference removal only)
template <>
std::vector<item *> temp_visitable<temp_crafting_inventory>::remove_items_with( const
        std::function<bool( const item &e )> &filter, int count )
{
    return static_cast<temp_crafting_inventory *>( this )->remove_items_with( filter, count );
}

template <>
void location_visitable<item>::remove_items_with( const
        std::function < VisitResponse( detached_ptr<item> &&e ) > &filter )
{
    auto it = static_cast<item *>( this );
    it->contents.remove_items_with( filter );
}

template <>
void location_visitable<location_inventory>::remove_items_with( const
        std::function < VisitResponse( detached_ptr<item> &&e ) > &filter )
{
    auto inv = static_cast<location_inventory *>( this );
    if( inv->inv.locked ) {
        debugmsg( "Attempted recursive item removal" );
        return;
    }
    inv->inv.locked = true;


    VisitResponse last = VisitResponse::NEXT;
    for( auto stack = inv->inv.items.begin(); stack != inv->inv.items.end() &&
         last != VisitResponse::ABORT; ) {
        std::vector<item *> &istack = *stack;
        const auto original_invlet = istack.front()->invlet;

        for( auto istack_iter = istack.begin(); istack_iter != istack.end() &&
             last != VisitResponse::ABORT; ) {
            location<item> *saved_loc = ( *istack_iter )->loc;
            ( *istack_iter )->remove_location();
            detached_ptr<item> t( *istack_iter );

            last = filter( std::move( t ) );

            // NOLINTNEXTLINE(bugprone-use-after-move)
            if( !t ) {
                istack_iter = istack.erase( istack_iter );
                if( istack_iter == istack.begin() && istack_iter != istack.end() ) {
                    ( *istack_iter )->invlet = original_invlet;
                }
            } else if( last == VisitResponse::NEXT ) {
                t->contents.remove_items_with( [&filter, &last]( detached_ptr<item> &&e ) {
                    last = filter( std::move( e ) );
                    return last;
                } );
            }

            if( t ) {
                t.release();
                ( *istack_iter )->set_location( saved_loc );
                istack_iter++;
            }
        }

        if( istack.empty() ) {
            stack = inv->inv.items.erase( stack );
        } else {
            ++stack;
        }
    }

    // Invalidate binning cache
    inv->inv.binned = false;
    inv->inv.items_type_cached = false;
    inv->inv.locked = false;
}

/** @relates visitable */
template <>
void location_visitable<Character>::remove_items_with( const
        std::function < VisitResponse( detached_ptr<item> &&e ) > &filter )
{
    auto ch = static_cast<Character *>( this );

    VisitResponse last = VisitResponse::NEXT;

    auto filter_and_collect = [&last, &filter]( detached_ptr<item> &&e ) {
        if( last == VisitResponse::ABORT ) {
            return last;
        }
        last = filter( std::move( e ) );
        return last;
    };

    // the carried items are inside the worn ones and the body storage
    if( item *storage = ch->get_body_storage() ) {
        storage->contents.remove_items_with( filter_and_collect );
    }

    if( last == VisitResponse::ABORT ) {
        return;
    }

    // J3: the carried items sit in the carry pockets of the worn items (the best pocket is the DDA
    // one, so it can be a worn item's), and they are the top items of the character: when the
    // filter doesn't descend into a worn item, its carry pockets are still visited
    visit_internal( [&last, &filter_and_collect]( detached_ptr<item> &&worn_it ) {
        const VisitResponse response = filter_and_collect( std::move( worn_it ) );
        // NOLINTNEXTLINE(bugprone-use-after-move)
        if( response == VisitResponse::SKIP && worn_it ) {
            // NOLINTNEXTLINE(bugprone-use-after-move)
            for( item_pocket &pocket : worn_it->contents.get_pockets() ) {
                if( is_listed_pocket( pocket ) ) {
                    visit_internal( filter_and_collect, pocket.items(), last );
                }
            }
            worn_it->invalidate_processing_cache_upwards();
            if( last == VisitResponse::ABORT ) {
                return last;
            }
        }
        return response;
    }, ch->worn );

    if( last == VisitResponse::ABORT ) {
        return;
    }

    if( !ch->primary_weapon().attempt_detach( [&last, &filter]( detached_ptr<item> &&e ) {
    last = filter( std::move( e ) );
        // NOLINTNEXTLINE(bugprone-use-after-move)
        return std::move( e );
    } ) ) {
        if( last == VisitResponse::NEXT ) {
            ch->primary_weapon().contents.remove_items_with( filter );
        }
    }
}

/** @relates visitable */
template <>
void location_visitable<map_cursor>::remove_items_with( const
        std::function < VisitResponse( detached_ptr<item> &&e ) > &filter )
{
    auto cur = static_cast<map_cursor *>( this );
    const auto cur_pos = tripoint_bub_ms( *cur );

    map &here = get_map();
    if( !here.inbounds( cur_pos ) ) {
        debugmsg( "cannot remove items from map: cursor out-of-bounds" );
        return;
    }

    // fetch the appropriate item stack
    point_sm_ms offset;
    submap *sub = here.get_submap_at( cur_pos, offset );

    visit_internal( [&filter, sub, offset]( detached_ptr<item> &&e ) {
        item &obj = *e;
        VisitResponse res = filter( std::move( e ) );
        // NOLINTNEXTLINE(bugprone-use-after-move)
        if( !e ) {
            // remove from the active items cache (if it isn't there does nothing)
            sub->active_items.remove( &obj );

            // if necessary remove item from the luminosity map
            sub->update_lum_rem( offset, obj );
        }
        return res;
    }, sub->get_items( offset ) );

    here.update_submap_active_item_status( *cur );
}

/** @relates visitable */
template <>
void location_visitable<map_selector>::remove_items_with( const
        std::function < VisitResponse( detached_ptr<item> &&e ) > &filter )
{
    VisitResponse last = VisitResponse::NEXT;
    for( auto &cursor : static_cast<map_selector &>( *this ) ) {
        cursor.remove_items_with( [&last, &filter]( detached_ptr<item> &&e ) {
            if( last == VisitResponse::ABORT ) {
                return last;
            }
            last = filter( std::move( e ) );
            return last;
        } );
        if( last == VisitResponse::ABORT ) {
            return;
        }
    }
}

/** @relates visitable */
template <>
void location_visitable<vehicle_cursor>::remove_items_with( const
        std::function < VisitResponse( detached_ptr<item> &&e ) > &filter )
{
    auto cur = static_cast<vehicle_cursor *>( this );

    int idx = cur->veh.part_with_feature( cur->part, "CARGO", false );
    if( idx < 0 ) {
        return;
    }

    vehicle_part &part = cur->veh.part( idx );
    bool removed = false;

    visit_internal( [&filter, &removed]( detached_ptr<item> &&e ) {
        VisitResponse ret = filter( std::move( e ) );
        // NOLINTNEXTLINE(bugprone-use-after-move)
        if( !e ) {
            removed = true;
        }
        return ret;
    }, part.items );

    if( removed ) {
        // if we removed any items then invalidate the cached mass
        cur->veh.invalidate_mass();
    }

}

/** @relates visitable */
template <>
void location_visitable<vehicle_selector>::remove_items_with( const
        std::function < VisitResponse( detached_ptr<item> &&e ) > &filter )
{

    VisitResponse last = VisitResponse::NEXT;
    for( auto &cursor : static_cast<vehicle_selector &>( *this ) ) {
        cursor.remove_items_with( [&last, &filter]( detached_ptr<item> &&e ) {
            if( last == VisitResponse::ABORT ) {
                return last;
            }
            last = filter( std::move( e ) );
            return last;
        } );
        if( last == VisitResponse::ABORT ) {
            return;
        }
    }
}

/** @relates visitable */
template <>
void location_visitable<monster>::remove_items_with( const
        std::function < VisitResponse( detached_ptr<item> &&e ) > &filter )
{
    monster *mon = static_cast<monster *>( this );

    VisitResponse last = VisitResponse::NEXT;
    auto filter_and_collect = [&last, &filter]( detached_ptr<item> &&e ) {
        if( last == VisitResponse::ABORT ) {
            return last;
        }
        last = filter( std::move( e ) );
        return last;
    };

    visit_internal( filter_and_collect, mon->inv );

    auto check_item = [&filter_and_collect]( detached_ptr<item> &&it ) {
        filter_and_collect( std::move( it ) );
        // NOLINTNEXTLINE(bugprone-use-after-move)
        return std::move( it );
    };

    if( mon->get_storage_item() ) {
        mon->get_storage_item()->attempt_detach( check_item );
    }
    if( mon->get_armor_item() ) {
        mon->get_armor_item()->attempt_detach( check_item );
    }
    if( mon->get_tack_item() ) {
        mon->get_tack_item()->attempt_detach( check_item );
    }
    if( mon->get_tied_item() ) {
        mon->get_tied_item()->attempt_detach( check_item );
    }
    if( mon->get_battery_item() ) {
        mon->get_battery_item()->attempt_detach( check_item );
    }
}
/** @relates visitable */

template <typename T>
static int charges_of_ups( const T &self, int limit,
                           const std::function<bool( const item & )> &,
                           std::function<void( int )> )
{
    int qty = 0;
    self->visit_items( [&]( const item * e ) {
        if( e->has_flag( flag_IS_UPS ) ) {
            qty = sum_no_wrap( qty, e->ammo_remaining() * e->type->tool->ups_eff_mult );
        }
        return qty < limit ? VisitResponse::NEXT : VisitResponse::ABORT;
    } );
    return std::min( qty, limit );
}

namespace
{
// One battery pocket of a MULTIMAG tool: local stock and per-use cost.
// CDF: named struct instead of DDA's std::pair<int, int> (AGENT_RULES: no std::pair)
struct battery_stock {
    int local;
    int per_use;
};

// Per-tool stock for charges_of dedup. Legacy contributes local charges;
// multimag records per-battery-pocket so shared-pool greedy can bill K uses
// without re-solving firing_requirements.
struct tool_stock_entry {
    enum class kind_t { LEGACY, MULTIMAG };
    kind_t kind = kind_t::LEGACY;
    bool uses_ups = false;
    bool uses_bionic = false;
    int local_charges = 0;
    const item *src = nullptr;
    std::vector<battery_stock> battery;
};
} // namespace

// CDF: flag_USE_UPS / flag_USES_BIONIC_POWER statics (json_flag_* absent); gun_mode_DEFAULT absent, so gun_mode_id( "DEFAULT" )
static tool_stock_entry build_multimag_entry( const item &e )
{
    tool_stock_entry out;
    out.kind = tool_stock_entry::kind_t::MULTIMAG;
    out.uses_ups = e.has_flag( flag_USE_UPS );
    out.uses_bionic = e.has_flag( flag_USES_BIONIC_POWER );
    out.src = &e;

    // Tools lower consumption_per_use into firing_requirements per_mode[DEFAULT].
    const std::vector<pocket_consumption_entry> *entries =
        e.type->firing_requirements.for_mode( gun_mode_id( "DEFAULT" ) );
    if( entries == nullptr ) {
        return out;
    }
    for( const pocket_consumption_entry &pce : *entries ) {
        const item_pocket *pkt = e.pocket_by_id( pce.pocket );
        if( pkt == nullptr || pce.qty < 1 ) {
            continue;
        }
        if( e.pocket_accepts_battery( pkt ) ) {
            out.battery.push_back( { e.ammo_remaining_in_pocket( pce.pocket ), pce.qty } );
        }
    }
    return out;
}

// CDF: scan takes a const item * visitor, not item_location (REPLAN D1); the filter is std::function<bool( const item & )>, not item_filter
template <typename T>
static void scan_tool_charges( const T &self, const itype_id &id,
                               const std::function<bool( const item & )> &filter, int limit,
                               std::vector<tool_stock_entry> &entries, bool in_tools,
                               int &raw_ups_charges )
{
    map &here = get_map();
    int qty = 0;
    self.visit_items( [&]( const item *e ) {
        // Raw IS_UPS sum, no ups_eff_mult (DDA 1111-1118); charges_of_ups keeps its multiplier
        if( id == itype_UPS && e->has_flag( flag_IS_UPS ) && filter( *e ) ) {
            raw_ups_charges = sum_no_wrap( raw_ups_charges, e->ammo_remaining_linked( here, nullptr ) );
            return raw_ups_charges < limit ? VisitResponse::SKIP : VisitResponse::ABORT;
        }
        // CDF: !is_broken() filter dropped, BN has no broken state (8-3 OD-6)
        if( filter( *e ) ) {
            if( e->is_tool() ) {
                if( e->typeId() == id || ( in_tools && e->ammo_current() == id ) ) {
                    if( e->uses_firing_requirements() ) {
                        entries.push_back( build_multimag_entry( *e ) );
                    } else {
                        tool_stock_entry entry;
                        entry.kind = tool_stock_entry::kind_t::LEGACY;
                        entry.local_charges = e->ammo_remaining_linked( here, nullptr );
                        entry.uses_ups = e->has_flag( flag_USE_UPS );
                        entry.uses_bionic = e->has_flag( flag_USES_BIONIC_POWER );
                        entry.src = e;
                        qty = sum_no_wrap( qty, entry.local_charges );
                        entries.push_back( entry );
                    }
                }
                return qty < limit ? VisitResponse::SKIP : VisitResponse::ABORT;

            } else if( e->count_by_charges() ) {
                // CDF: count_by_charges stays typeId()==id; in_tools applies to tools only (DDA item.cpp:4013 gate is tool/gun)
                if( e->typeId() == id ) {
                    tool_stock_entry entry;
                    entry.kind = tool_stock_entry::kind_t::LEGACY;
                    entry.local_charges = e->charges;
                    qty = sum_no_wrap( qty, entry.local_charges );
                    entries.push_back( entry );
                }
                // items counted by charges are not themselves expected to be containers
                return qty < limit ? VisitResponse::SKIP : VisitResponse::ABORT;
            }
        }
        // recurse through any nested containers
        return qty < limit ? VisitResponse::NEXT : VisitResponse::ABORT;
    } );
}

template <typename M>
static int apply_external_pools_legacy( const M &main,
                                        const std::vector<tool_stock_entry> &entries, int limit,
                                        const std::function<void( int )> &visitor )
{
    int qty = 0;
    bool any_ups = false;
    bool any_bionic = false;
    for( const tool_stock_entry &e : entries ) {
        qty = sum_no_wrap( qty, e.local_charges );
        any_ups = any_ups || e.uses_ups;
        any_bionic = any_bionic || e.uses_bionic;
    }
    // CDF: bio_ups power is already counted by main.charges_of( itype_UPS ) for a Character (visitable<Character>::charges_of, bio_ups branch); DDA 1185-1189 would count it twice
    if( any_bionic ) {
        qty = sum_no_wrap( qty, static_cast<int>( units::to_kilojoule(
                               get_player_character().get_power_level() ) ) );
    }
    if( qty < limit && any_ups ) {
        qty += main.charges_of( itype_UPS, limit - qty );
        if( visitor ) {
            // CDF: visitor receives the running total, BN; DDA 1197 passes used_ups
            visitor( qty );
        }
    }
    return std::min( qty, limit );
}

template <typename M>
static int apply_external_pools_multimag( const M &main,
        const std::vector<tool_stock_entry> &entries, int limit )
{
    bool any_ups = false;
    bool any_bionic = false;
    for( const tool_stock_entry &e : entries ) {
        any_ups = any_ups || e.uses_ups;
        any_bionic = any_bionic || e.uses_bionic;
    }
    // UPS goes through main so ground UPS in crafting_inventory is seen.
    // CDF: DDA 1215 and 1222-1225 add active bio_ups power back because DDA's Character::charges_of( itype_UPS ) strips it; CDF's visitable<Character>::charges_of( itype_UPS ) already counts it (bio_ups branch), so the add-back would count it twice (AUTO-43)
    int external = 0;
    if( any_bionic ) {
        external = sum_no_wrap( external, static_cast<int>( units::to_kilojoule(
                                    get_player_character().get_power_level() ) ) );
    }
    if( any_ups ) {
        external = sum_no_wrap( external, main.charges_of( itype_UPS, INT_MAX ) );
    }

    int total = 0;
    int external_remaining = external;
    map &here = get_map();
    for( const tool_stock_entry &e : entries ) {
        if( e.src == nullptr ) {
            continue;
        }
        // Cable to a linked vehicle battery is per-tool, not shared, so it
        // adds to this tool's budget without affecting the shared external.
        // CDF: available_cable_charges is a stub that returns 0 (AUTO-26 O2); the call stays so the cable pool arrives with its owner
        const int cable_pool = e.src->available_cable_charges( here );
        const int budget = sum_no_wrap( external_remaining, cable_pool );
        const int uses = e.src->feasible_tool_uses( budget );
        // Battery consumed for K uses is sum max(0, K * per_use - local) over
        // battery pockets. Drain cable first (matches firing-loop order), then
        // bill the remainder to the shared external pool.
        int64_t consumed = 0;
        for( const battery_stock &b : e.battery ) {
            const int64_t per = b.per_use;
            const int64_t local = b.local;
            consumed += std::max<int64_t>( 0, static_cast<int64_t>( uses ) * per - local );
        }
        const int64_t cable_used = std::min<int64_t>( consumed, cable_pool );
        const int64_t shared_used = consumed - cable_used;
        external_remaining -= static_cast<int>(
                                  std::min<int64_t>( shared_used, external_remaining ) );
        total = sum_no_wrap( total, uses );
        if( total >= limit ) {
            break;
        }
    }
    return std::min( total, limit );
}

template <typename M>
static int apply_external_pools( const M &main,
                                 const std::vector<tool_stock_entry> &entries, int limit,
                                 const std::function<void( int )> &visitor, int raw_ups_charges )
{
    if( raw_ups_charges > 0 ) {
        return std::min( raw_ups_charges, limit );
    }
    std::vector<tool_stock_entry> legacy;
    std::vector<tool_stock_entry> multi;
    legacy.reserve( entries.size() );
    multi.reserve( entries.size() );
    for( const tool_stock_entry &e : entries ) {
        if( e.kind == tool_stock_entry::kind_t::MULTIMAG ) {
            multi.push_back( e );
        } else {
            legacy.push_back( e );
        }
    }
    int total = 0;
    if( !legacy.empty() ) {
        total = sum_no_wrap( total, apply_external_pools_legacy( main, legacy, limit, visitor ) );
    }
    if( !multi.empty() && total < limit ) {
        total = sum_no_wrap( total, apply_external_pools_multimag( main, multi, limit - total ) );
    }
    return std::min( total, limit );
}

template <typename T, typename M>
static int charges_of_internal( const T &self, const M &main, const itype_id &id, int limit,
                                const std::function<bool( const item & )> &filter,
                                std::function<void( int )> visitor, bool in_tools )
{
    std::vector<tool_stock_entry> entries;
    int raw_ups_charges = 0;
    scan_tool_charges( self, id, filter, limit, entries, in_tools, raw_ups_charges );
    return apply_external_pools( main, entries, limit, visitor, raw_ups_charges );
}

/** @relates visitable */
template <typename T>
int visitable<T>::charges_of( const itype_id &what, int limit,
                              const std::function<bool( const item & )> &filter,
                              std::function<void( int )> visitor, bool in_tools ) const
{
    return charges_of_internal( *this, *this, what, limit, filter, visitor, in_tools );
}

/** @relates visitable */
template <>
int visitable<inventory>::charges_of( const itype_id &what, int limit,
                                      const std::function<bool( const item & )> &filter,
                                      std::function<void( int )> visitor, bool in_tools ) const
{
    if( what == itype_UPS ) {
        int qty = 0;
        qty = charges_of_ups( this, limit, filter, visitor );
        return std::min( qty, limit );
    }
    // CDF: a tool holding ammo `what` is not binned under `what`, so in_tools walks the live items (DDA temp_crafting_inventory::charges_of, visitable.cpp:1412-1414; CDF bin index differs)
    if( in_tools ) {
        return charges_of_internal( *this, *this, what, limit, filter, std::move( visitor ), in_tools );
    }
    const auto &binned = static_cast<const inventory *>( this )->get_binned_items();
    const auto iter = binned.find( what );
    if( iter == binned.end() ) {
        return 0;
    }

    int res = 0;
    for( const item *it : iter->second ) {
        res = sum_no_wrap( res, charges_of_internal( *it, *this, what, limit, filter, visitor, in_tools ) );
        if( res >= limit ) {
            break;
        }
    }
    return std::min( limit, res );
}

/** @relates visitable */
template <>
int visitable<temp_crafting_inventory>::charges_of( const itype_id &what, int limit,
        const std::function<bool( const item & )> &filter,
        std::function<void( int )> visitor, bool in_tools ) const
{
    if( what == itype_UPS ) {
        return std::min( charges_of_ups( this, limit, filter, visitor ), limit );
    }
    return charges_of_internal( *this, *this, what, limit, filter, std::move( visitor ), in_tools );
}

template <>
int visitable<location_inventory>::charges_of( const itype_id &what, int limit,
        const std::function<bool( const item & )> &filter,
        std::function<void( int )> visitor, bool in_tools ) const
{

    auto inv = static_cast<const location_inventory *>( this );
    return inv->inv.charges_of( what, limit, filter, std::move( visitor ), in_tools );
}

/** @relates visitable */
template <>
int visitable<Character>::charges_of( const itype_id &what, int limit,
                                      const std::function<bool( const item & )> &filter,
                                      std::function<void( int )> visitor, bool in_tools ) const
{
    auto self = static_cast<const Character *>( this );
    auto p = dynamic_cast<const player *>( self );

    if( what == itype_UPS ) {
        int qty = 0;
        qty = charges_of_ups( this, limit, filter, visitor );
        if( p && p->has_active_bionic( bio_ups ) ) {
            qty = sum_no_wrap( qty, units::to_kilojoule( p->get_power_level() ) );
        }
        if( p && p->is_mounted() ) {
            auto mons = p->mounted_creature.get();
            if( mons->has_flag( MF_RIDEABLE_MECH ) && mons->get_battery_item() ) {
                qty = sum_no_wrap( qty, mons->get_battery_item()->ammo_remaining() );
            }
        }
        return std::min( qty, limit );
    }


    if( what->has_flag( flag_BIONIC_TOOLS ) ) {
        if( p && p->has_active_bionic_with_fake( what ) ) {
            return std::min( units::to_kilojoule( p->get_power_level() ), limit );
        } else {
            return 0;
        }
    }

    if( what->has_flag( flag_ENCHANTMENT_TOOLS ) ) {
        if( p && p->has_enchantment_with_fake( what ) && what->has_flag( flag_USES_BIONIC_POWER ) ) {
            return std::min( units::to_kilojoule( p->get_power_level() ), limit );
        } else {
            return 0;
        }
    }

    if( what == itype_bio_armor ) {
        float efficiency = 1;
        int power_charges = 0;

        for( const bionic &bio : *self->my_bionics ) {
            if( bio.powered && bio.info().has_flag( flag_BIONIC_ARMOR_INTERFACE ) ) {
                efficiency = std::max( efficiency, bio.info().fuel_efficiency );
            }
        }
        if( efficiency == 1 ) {
            debugmsg( "Character lacks a bionic armor interface with fuel efficiency field." );
        }
        power_charges = units::to_kilojoule( self->as_player()->get_power_level() ) * efficiency;

        return std::min( power_charges, limit );
    }

    return charges_of_internal( *this, *this, what, limit, filter, std::move( visitor ), in_tools );
}

template <typename T>
static int amount_of_internal( const T &self, const itype_id &id, bool pseudo, int limit,
                               const std::function<bool( const item & )> &filter )
{
    int qty = 0;
    self.visit_items( [&qty, &id, &pseudo, &limit, &filter]( const item * e ) {
        if( ( id.str() == "any" || e->typeId() == id ) && filter( *e ) && ( pseudo ||
                !e->has_flag( STATIC( flag_id( "PSEUDO" ) ) ) ) ) {
            qty = sum_no_wrap( qty, 1 );
        }
        return qty != limit ? VisitResponse::NEXT : VisitResponse::ABORT;
    } );
    return qty;
}

/** @relates visitable */
template <typename T>
int visitable<T>::amount_of( const itype_id &what, bool pseudo, int limit,
                             const std::function<bool( const item & )> &filter ) const
{
    return amount_of_internal( *this, what, pseudo, limit, filter );
}

/** @relates visitable */
template <>
int visitable<inventory>::amount_of( const itype_id &what, bool pseudo, int limit,
                                     const std::function<bool( const item & )> &filter ) const
{
    const auto &binned = static_cast<const inventory *>( this )->get_binned_items();
    const auto iter = binned.find( what );
    if( iter == binned.end() && what != itype_id( "any" ) ) {
        return 0;
    }

    int res = 0;
    if( what.str() == "any" ) {
        for( const auto &kv : binned ) {
            for( const item *it : kv.second ) {
                res = sum_no_wrap( res, it->amount_of( what, pseudo, limit, filter ) );
                if( res >= limit ) {
                    break;
                }
            }
        }
    } else {
        for( const item *it : iter->second ) {
            res = sum_no_wrap( res, it->amount_of( what, pseudo, limit, filter ) );
            if( res >= limit ) {
                break;
            }
        }
    }

    return std::min( limit, res );
}

template <>
int visitable<location_inventory>::amount_of( const itype_id &what, bool pseudo, int limit,
        const std::function<bool( const item & )> &filter ) const
{

    auto inv = static_cast<const location_inventory *>( this );
    return inv->inv.amount_of( what, pseudo, limit, filter );
}


/** @relates visitable */
template <>
int visitable<Character>::amount_of( const itype_id &what, bool pseudo, int limit,
                                     const std::function<bool( const item & )> &filter ) const
{
    auto self = static_cast<const Character *>( this );

    if( what->has_flag( flag_BIONIC_TOOLS ) && pseudo && self->has_active_bionic_with_fake( what ) ) {
        return 1;
    }

    if( what->has_flag( flag_ENCHANTMENT_TOOLS ) && pseudo &&
        self->has_enchantment_with_fake( what ) ) {
        return 1;
    }

    if( what == itype_apparatus && pseudo ) {
        int qty = 0;
        visit_items( [&qty, &limit, &filter]( const item * e ) {
            if( e->get_quality( quality_id( "SMOKE_PIPE" ) ) >= 1 && filter( *e ) ) {
                qty = sum_no_wrap( qty, 1 );
            }
            return qty < limit ? VisitResponse::SKIP : VisitResponse::ABORT;
        } );
        return std::min( qty, limit );
    }

    return amount_of_internal( *this, what, pseudo, limit, filter );
}

/** @relates visitable */
template <typename T>
bool visitable<T>::has_amount( const itype_id &what, int qty, bool pseudo,
                               const std::function<bool( const item & )> &filter ) const
{
    return amount_of( what, pseudo, qty, filter ) == qty;
}

template <typename T>
void location_visitable<T>::remove_top_items_with( const std::function < detached_ptr<item>
        ( detached_ptr<item> && ) > &filter )
{
    remove_items_with( [&filter]( detached_ptr<item> &&e ) {
        e = filter( std::move( e ) );
        return VisitResponse::SKIP;
    } );
}

template <typename T>
void location_visitable<T>::remove_all_items_with( const std::function < detached_ptr<item>
        ( detached_ptr<item> && ) > &filter )
{
    remove_items_with( [&filter]( detached_ptr<item> &&e ) {
        e = filter( std::move( e ) );
        return VisitResponse::NEXT;
    } );
}

// explicit template initialization for all classes implementing the visitable interface
template class visitable<item>;
template class visitable<inventory>;
template class visitable<temp_crafting_inventory>;
template class visitable<location_inventory>;
template class visitable<Character>;
template class visitable<map_selector>;
template class visitable<map_cursor>;
template class visitable<vehicle_selector>;
template class visitable<vehicle_cursor>;
template class visitable<monster>;

template class temp_visitable<inventory>;
template class temp_visitable<temp_crafting_inventory>;
template class location_visitable<item>;
template class location_visitable<location_inventory>;
template class location_visitable<Character>;
template class location_visitable<map_selector>;
template class location_visitable<map_cursor>;
template class location_visitable<vehicle_selector>;
template class location_visitable<vehicle_cursor>;
template class location_visitable<monster>;
