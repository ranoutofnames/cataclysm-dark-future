#include "item_contents.h"

#include "avatar.h"
#include "character.h"
#include "debug.h"
#include "enums.h"
#include "flag.h"
#include "handle_liquid.h"
#include "item.h"
#include "itype.h"
#include "locations.h"
#include "map/map.h"
#include "messages.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>

struct tripoint;

item_contents::item_contents( item *container ) : owner( container ) {}

item_contents::~item_contents() = default;

void item_contents::refresh_top_cache() const
{
    top_cache.clear();
    for( const item_pocket &pocket : pockets ) {
        const std::vector<item *> &v = pocket.items().as_vector();
        top_cache.insert( top_cache.end(), v.begin(), v.end() );
    }
}

void item_contents::copy_from( const item_contents &source )
{
    ensure_pockets();
    source.ensure_pockets();
    // a magazine_adaptor changes the layout depending on contents: let insert_item rebuild it
    bool same_layout = source.adapted_pockets.empty() && adapted_pockets.empty();
    std::vector<const item_pocket *> src_standard;
    std::vector<item_pocket *> dest_standard;
    for( const item_pocket &pocket : source.pockets ) {
        if( !pocket.is_type( pocket_type::MIGRATION ) ) {
            src_standard.push_back( &pocket );
        }
    }
    for( item_pocket &pocket : pockets ) {
        if( !pocket.is_type( pocket_type::MIGRATION ) ) {
            dest_standard.push_back( &pocket );
        }
    }
    if( same_layout && src_standard.size() == dest_standard.size() ) {
        for( size_t i = 0; i < src_standard.size() && same_layout; ++i ) {
            same_layout = src_standard[i]->get_type() == dest_standard[i]->get_type();
        }
    } else {
        same_layout = false;
    }
    if( !same_layout ) {
        for( item * const &it : source.all_items_top() ) {
            insert_item( item::spawn( *it ) );
        }
        return;
    }
    // same layout: every item stays in the pocket (and sealed state) it was in
    for( size_t i = 0; i < src_standard.size(); ++i ) {
        for( item * const &it : src_standard[i]->items().as_vector() ) {
            dest_standard[i]->items().push_back( item::spawn( *it ) );
        }
        if( src_standard[i]->saved_sealed() ) {
            dest_standard[i]->seal();
        } else {
            dest_standard[i]->unseal();
        }
    }
    if( const item_pocket *src_mig = source.migration_pocket() ) {
        if( !src_mig->empty() ) {
            item_pocket *mig = migration_pocket( true );
            for( item * const &it : src_mig->items().as_vector() ) {
                mig->items().push_back( item::spawn( *it ) );
            }
        }
    }
    contents_changed();
}

void item_contents::invalidate_pocket_caches() const
{
    for( const item_pocket &pocket : pockets ) {
        pocket.invalidate_cache();
    }
}

// Invariant: every mutation of pocket contents, or of anything a pocket size depends on,
// must call contents_changed() (or item::invalidate_pocket_caches_upwards() directly).
// item_pocket::refresh_cache only detects changes to the item list itself; in-place
// changes (weight/volume vars, convert, size flags, charges) rely on this call.
void item_contents::contents_changed()
{
    refresh_top_cache();
    // the sizes of this item and of everything it is inside may have changed
    if( owner != nullptr ) {
        owner->invalidate_pocket_caches_upwards();
    } else {
        invalidate_pocket_caches();
    }
    if( owner != nullptr ) {
        owner->invalidate_processing_cache_upwards();
    } else {
        invalidate_processing_cache();
    }
}

bool item_contents::empty() const
{
    return std::ranges::all_of( pockets, []( const item_pocket & p ) {
        return p.empty();
    } );
}

auto item_contents::has_processing_items() const -> bool
{
    update_processing_cache();
    return !cached_processing_items.empty();
}

auto item_contents::processing_items() const -> const std::vector<item *> & // *NOPAD*
{
    update_processing_cache();
    return cached_processing_items;
}

auto item_contents::invalidate_processing_cache() const -> void
{
    processing_cache_dirty = true;
}

auto item_contents::update_processing_cache() const -> void
{
    if( !processing_cache_dirty ) {
        return;
    }

    cached_processing_items.clear();
    for( item *contained_item : all_items_top() ) {
        if( contained_item->needs_processing() ) {
            cached_processing_items.push_back( contained_item );
        }
    }
    processing_cache_dirty = false;
}

std::list<item_pocket> &item_contents::get_pockets()
{
    ensure_pockets();
    return pockets;
}

const std::list<item_pocket> &item_contents::get_pockets() const
{
    ensure_pockets();
    return pockets;
}

std::vector<item_pocket *> item_contents::get_pockets( pocket_type type )
{
    std::vector<item_pocket *> ret;
    for( item_pocket &pocket : get_pockets() ) {
        if( pocket.is_type( type ) ) {
            ret.push_back( &pocket );
        }
    }
    return ret;
}

std::vector<const item_pocket *> item_contents::get_pockets( pocket_type type ) const
{
    std::vector<const item_pocket *> ret;
    for( const item_pocket &pocket : get_pockets() ) {
        if( pocket.is_type( type ) ) {
            ret.push_back( &pocket );
        }
    }
    return ret;
}

item_pocket *item_contents::pocket_of( const item *it )
{
    for( item_pocket &pocket : pockets ) {
        const std::vector<item *> &v = pocket.items().as_vector();
        if( std::ranges::find( v, it ) != v.end() ) {
            return &pocket;
        }
    }
    return nullptr;
}

const item_pocket *item_contents::pocket_of( const item *it ) const
{
    return const_cast<item_contents *>( this )->pocket_of( it );
}

item_pocket *item_contents::migration_pocket( bool create )
{
    for( item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::MIGRATION ) ) {
            return &pocket;
        }
    }
    if( !create ) {
        return nullptr;
    }
    // the MIGRATION pocket always goes last
    pockets.emplace_back( owner, &migration_pocket_data() );
    return &pockets.back();
}

const item_pocket *item_contents::migration_pocket() const
{
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::MIGRATION ) ) {
            return &pocket;
        }
    }
    return nullptr;
}

void item_contents::ensure_pockets() const
{
    if( owner == nullptr || owner->type == pockets_type ) {
        return;
    }
    // items are never const objects, only accessed through const references
    const_cast<item_contents *>( this )->rebuild_pockets();
}

std::vector<pocket_data> item_contents::adapted_layout() const
{
    if( owner == nullptr || owner->type == nullptr ) {
        return {};
    }
    // The first mod with a magazine_adaptor replaces the magazines the item takes
    // (as item::magazine_compatible does). Look at our own pockets: this runs mid-rebuild.
    const itype *adaptor = nullptr;
    for( const item_pocket &pocket : pockets ) {
        if( !pocket.is_type( pocket_type::MOD ) && !pocket.is_type( pocket_type::MIGRATION ) ) {
            continue;
        }
        for( const item *it : pocket.items() ) {
            if( it->type->mod && !it->type->mod->magazine_adaptor.empty() ) {
                adaptor = it->type;
                break;
            }
        }
        if( adaptor != nullptr ) {
            break;
        }
    }
    if( adaptor == nullptr ) {
        return {};
    }

    // Only the magazines for the ammo the item can fire count, as in item::magazine_compatible.
    // Our own pockets are read directly: item::ammo_types would re-enter the rebuild.
    const std::set<ammotype> *ammo = nullptr;
    for( const item_pocket &pocket : pockets ) {
        if( !pocket.is_type( pocket_type::MOD ) && !pocket.is_type( pocket_type::MIGRATION ) ) {
            continue;
        }
        for( const item *it : pocket.items() ) {
            if( it->type->mod && !it->type->mod->ammo_modifier.empty() ) {
                ammo = &it->type->mod->ammo_modifier;
                break;
            }
        }
        if( ammo != nullptr ) {
            break;
        }
    }
    if( ammo == nullptr ) {
        if( owner->type->gun ) {
            ammo = &owner->type->gun->ammo;
        } else if( owner->type->tool ) {
            ammo = &owner->type->tool->ammo_id;
        }
    }
    if( ammo == nullptr ) {
        return {};
    }

    std::set<itype_id> mags;
    units::volume biggest = 0_ml;
    for( const auto &entry : adaptor->mod->magazine_adaptor ) {
        if( !ammo->contains( entry.first ) ) {
            continue;
        }
        for( const itype_id &mag : entry.second ) {
            mags.insert( mag );
            if( mag.is_valid() ) {
                biggest = std::max( biggest, mag->volume );
            }
        }
    }
    if( mags.empty() ) {
        return {};
    }

    std::vector<pocket_data> layout = owner->type->pockets;
    pocket_data *well = nullptr;
    for( pocket_data &data : layout ) {
        if( data.type == pocket_type::MAGAZINE_WELL ) {
            well = &data;
            break;
        }
    }
    if( well == nullptr ) {
        // a gun without detachable magazines gains a well; keep it before the MOD pocket
        auto at = std::ranges::find_if( layout, []( const pocket_data & d ) {
            return d.type == pocket_type::MOD;
        } );
        well = &*layout.insert( at, pocket_data( pocket_type::MAGAZINE_WELL ) );
        well->rigid = true;
    }
    well->item_restriction = mags;
    well->volume_capacity = std::max( { well->volume_capacity, biggest, units::volume( 1_ml ) } );
    return layout;
}

bool item_contents::has_adaptor_mod() const
{
    for( const item_pocket &pocket : pockets ) {
        if( !pocket.is_type( pocket_type::MOD ) && !pocket.is_type( pocket_type::MIGRATION ) ) {
            continue;
        }
        for( const item *it : pocket.items() ) {
            if( it->type->mod && !it->type->mod->magazine_adaptor.empty() ) {
                return true;
            }
        }
    }
    return false;
}

void item_contents::rebuild_pockets()
{
    if( owner == nullptr ) {
        return;
    }
    // The layout is the type's, with the MAGAZINE_WELL replaced by an installed
    // magazine_adaptor mod (DDA #41917).
    std::vector<pocket_data> fresh = adapted_layout();
    const bool adapted = !fresh.empty();
    const std::vector<pocket_data> no_pockets;
    const std::vector<pocket_data> &type_data = owner->type != nullptr ? owner->type->pockets :
            no_pockets;
    const std::vector<pocket_data> &new_data = adapted ? fresh : type_data;

    // Same layout (e.g. a sealed can becoming an open can, a tool turning on):
    // keep the pockets and their items, only swap the data. This never moves an item.
    std::vector<item_pocket *> standard;
    for( item_pocket &pocket : pockets ) {
        if( !pocket.is_type( pocket_type::MIGRATION ) ) {
            standard.push_back( &pocket );
        }
    }
    if( standard.size() == new_data.size() && std::equal( standard.begin(), standard.end(),
    new_data.begin(), []( const item_pocket * p, const pocket_data & d ) {
    return p->get_type() == d.type;
    } ) ) {
        // moving a vector keeps its elements where they are
        adapted_pockets = adapted ? std::move( fresh ) : std::vector<pocket_data>();
        const std::vector<pocket_data> &layout = adapted ? adapted_pockets : type_data;
        for( size_t i = 0; i < standard.size(); ++i ) {
            standard[i]->set_data( &layout[i] );
        }
        pockets_type = owner->type;
        contents_changed();
        check_overflow();
        return;
    }

    std::vector<detached_ptr<item>> held;
    for( item_pocket &pocket : pockets ) {
        for( detached_ptr<item> &it : pocket.items().clear() ) {
            held.push_back( std::move( it ) );
        }
    }
    pockets.clear();
    adapted_pockets = adapted ? std::move( fresh ) : std::vector<pocket_data>();
    const std::vector<pocket_data> &layout = adapted ? adapted_pockets : type_data;
    pockets_type = owner->type;
    for( const pocket_data &data : layout ) {
        pockets.emplace_back( owner, &data );
    }
    for( detached_ptr<item> &it : held ) {
        item_pocket *dest = route( *it );
        dest->insert( std::move( it ) );
    }
    contents_changed();
    // a type change is an overflow event (DDA #65518)
    check_overflow();
}

item_pocket *item_contents::route( const item &it )
{
    item_pocket *dest = fitting_pocket( it );
    return dest != nullptr ? dest : migration_pocket( true );
}

item_pocket *item_contents::fitting_pocket( const item &it )
{
    ensure_pockets();

    // Force-insert routing, after DDA migrate_content_item: mods, magazines and ammo,
    // software, corpses, then the best container that has room. Sealed pockets are
    // allowed: spawns fill factory-sealed cans. Anything else goes to MIGRATION.
    const auto best_fitting = [&]( pocket_type type ) -> item_pocket * {
        item_pocket *best = nullptr;
        for( item_pocket &pocket : pockets )
        {
            if( !pocket.is_type( type ) ||
                !pocket.can_contain( it, false, true ).success() ) {
                continue;
            }
            if( best == nullptr || best->better_pocket( pocket, it ) ) {
                best = &pocket;
            }
        }
        return best;
    };

    item_pocket *dest = nullptr;
    if( it.has_flag( flag_CASING ) ) {
        // spent casings stay with the gun until unloaded (BN brass catcher / RELOAD_EJECT)
        dest = best_fitting( pocket_type::MAGAZINE );
    } else if( it.is_gunmod() || it.is_toolmod() ) {
        dest = best_fitting( pocket_type::MOD );
    } else if( it.is_magazine() ) {
        dest = best_fitting( pocket_type::MAGAZINE_WELL );
    } else if( it.is_ammo() ) {
        dest = best_fitting( pocket_type::MAGAZINE );
    }
    if( dest == nullptr ) {
        dest = best_fitting( pocket_type::SOFTWARE );
    }
    if( dest == nullptr ) {
        dest = best_fitting( pocket_type::CORPSE );
    }
    if( dest == nullptr ) {
        dest = best_fitting( pocket_type::CONTAINER );
    }
    return dest;
}

namespace
{

// A MIGRATION item that one of the owner's real pockets would take (if it had room).
// Items no pocket would ever take (spent casings, an RC car's charge) live in MIGRATION.
bool belongs_in_a_pocket( const std::list<item_pocket> &pockets, const item &it )
{
    if( it.has_flag( flag_CASING ) ) {
        return false;
    }
    return std::ranges::any_of( pockets, [&it]( const item_pocket & p ) {
        return !p.is_type( pocket_type::MIGRATION ) && p.is_compatible( it ).success();
    } );
}

} // namespace

bool item_contents::needs_overflow() const
{
    for( const item_pocket &pocket : get_pockets() ) {
        if( pocket.is_type( pocket_type::MIGRATION ) ) {
            for( const item *it : pocket.items() ) {
                if( belongs_in_a_pocket( pockets, *it ) ) {
                    return true;
                }
            }
            continue;
        }
        if( !pocket.is_standard_type() ) {
            continue;
        }
        if( pocket.is_overfull() ) {
            return true;
        }
        for( const item *it : pocket.items() ) {
            if( !it->has_flag( flag_CASING ) && !pocket.is_compatible( *it ).success() ) {
                return true;
            }
        }
    }
    return false;
}

void item_contents::check_overflow()
{
    const bool was_pending = pending_overflow;
    pending_overflow = needs_overflow();
    if( pending_overflow != was_pending && owner != nullptr ) {
        // pending overflow makes the owner need processing
        owner->invalidate_processing_cache_upwards();
    }
}

int item_contents::overflow( Character *carrier, const tripoint_bub_ms &pos, item *parent )
{
    ensure_pockets();
    pending_overflow = false;

    std::vector<detached_ptr<item>> out;
    for( item_pocket &pocket : pockets ) {
        location_vector<item> &items = pocket.items();
        if( pocket.is_type( pocket_type::MIGRATION ) ) {
            for( auto iter = items.begin(); iter != items.end(); ) {
                if( belongs_in_a_pocket( pockets, **iter ) ) {
                    detached_ptr<item> det;
                    iter = items.erase( iter, &det );
                    out.push_back( std::move( det ) );
                } else {
                    ++iter;
                }
            }
            continue;
        }
        if( !pocket.is_standard_type() ) {
            continue;
        }
        for( auto iter = items.begin(); iter != items.end(); ) {
            if( !( *iter )->has_flag( flag_CASING ) && !pocket.is_compatible( **iter ).success() ) {
                detached_ptr<item> det;
                iter = items.erase( iter, &det );
                out.push_back( std::move( det ) );
            } else {
                ++iter;
            }
        }
        // newest first
        while( pocket.is_overfull() ) {
            detached_ptr<item> det;
            items.erase( items.end() - 1, &det );
            out.push_back( std::move( det ) );
        }
    }

    int moved = 0;
    map &here = get_map();
    for( detached_ptr<item> &it : out ) {
        // another pocket of this item
        if( item_pocket *home = fitting_pocket( *it ) ) {
            home->insert( std::move( it ) );
            continue;
        }
        // the parent container
        if( parent != nullptr ) {
            std::pair<item *, item_pocket *> found = parent->contents.best_pocket( *it, owner );
            if( found.second != nullptr ) {
                found.first->contents.insert_into( *found.second, std::move( it ) );
                moved++;
                continue;
            }
        }
        const std::string name = it->tname();
        const std::string owner_name = owner != nullptr ? owner->tname() : std::string();
        // the carrying character
        if( carrier != nullptr && !it->made_of( LIQUID ) && carrier->can_pick_volume( *it ) &&
            carrier->can_pick_weight( *it, true ) ) {
            carrier->i_add( std::move( it ) );
            carrier->add_msg_if_player( m_info, _( "You take the %1$s out of your %2$s." ), name,
                                        owner_name );
            moved++;
            continue;
        }
        // the ground
        if( here.inbounds( pos ) ) {
            detached_ptr<item> left = here.add_item_or_charges( pos, std::move( it ) );
            if( !left ) {
                if( carrier != nullptr ) {
                    carrier->add_msg_if_player( m_warning, _( "Your %1$s falls out of your %2$s." ), name,
                                                owner_name );
                } else if( get_player_character().sees( pos ) ) {
                    add_msg( m_warning, _( "The %1$s falls out of the %2$s." ), name, owner_name );
                }
                moved++;
                continue;
            }
            it = std::move( left );
        }
        // nowhere to put it: keep it, never lose it
        migration_pocket( true )->insert( std::move( it ) );
    }
    contents_changed();
    return moved;
}

ret_val<item_pocket::contain_code> item_contents::can_contain( const item &it,
        pocket_type type ) const
{
    using code = item_pocket::contain_code;
    std::optional<ret_val<code>> best_failure;
    bool any = false;
    for( const item_pocket &pocket : get_pockets() ) {
        if( !pocket.is_type( type ) ) {
            continue;
        }
        any = true;
        ret_val<code> res = pocket.can_contain( it );
        if( res.success() ) {
            return res;
        }
        // prefer reporting a capacity problem of a pocket that takes this kind of item
        if( !best_failure || ( !best_failure->success() && pocket.is_compatible( it ).success() ) ) {
            best_failure = res;
        }
    }
    if( !any || !best_failure ) {
        return ret_val<code>::make_failure( code::ERR_FLAG, _( "it can't hold anything" ) );
    }
    return *best_failure;
}

std::pair<item *, item_pocket *> item_contents::best_pocket( const item &it, const item *avoid )
{
    return best_pocket_impl( it, avoid, false, units::volume_max );
}

std::pair<item *, item_pocket *> item_contents::best_pocket_impl( const item &it,
        const item *avoid, bool nested, units::volume parent_room )
{
    ensure_pockets();
    item_pocket *best = nullptr;
    if( owner != avoid && owner != &it ) {
        for( item_pocket &pocket : pockets ) {
            if( !pocket.is_type( pocket_type::CONTAINER ) || pocket.sealed() ) {
                continue;
            }
            // a soft pocket inside another container only fits what the parent has room for
            if( nested && !pocket.rigid() && parent_room < it.volume() ) {
                continue;
            }
            if( !pocket.can_contain( it ).success() ) {
                continue;
            }
            if( best == nullptr || best->better_pocket( pocket, it, nested ) ) {
                best = &pocket;
            }
        }
    }
    if( best != nullptr ) {
        return { owner, best };
    }
    // only descend into contained items if no own pocket fits (DDA #79913)
    for( item_pocket &pocket : pockets ) {
        if( !pocket.is_type( pocket_type::CONTAINER ) || pocket.sealed() ) {
            continue;
        }
        for( item *contained : pocket.items() ) {
            if( contained == &it || contained == avoid ) {
                continue;
            }
            std::pair<item *, item_pocket *> found = contained->contents.best_pocket_impl( it, avoid, true,
                    pocket.remaining_volume() );
            if( found.second != nullptr ) {
                return found;
            }
        }
    }
    return { owner, nullptr };
}

void item_contents::insert_into( item_pocket &pocket, detached_ptr<item> &&it )
{
    if( !it ) {
        return;
    }
    const bool adaptor = it->type->mod && !it->type->mod->magazine_adaptor.empty();
    pocket.insert( std::move( it ) );
    contents_changed();
    if( adaptor ) {
        rebuild_pockets();
    }
}

ret_val<bool> item_contents::insert_item( detached_ptr<item> &&it )
{
    if( !it ) {
        return ret_val<bool>::make_failure();
    }
    const bool adaptor = it->type->mod && !it->type->mod->magazine_adaptor.empty();
    item_pocket *dest = route( *it );
    dest->insert( std::move( it ) );
    contents_changed();
    if( adaptor ) {
        // the mod changes which magazines fit (DDA #41917)
        rebuild_pockets();
    }
    return ret_val<bool>::make_success();
}

void item_contents::insert_copies( const item &proto, int n )
{
    if( n <= 0 ) {
        return;
    }
    const bool adaptor = proto.type->mod && !proto.type->mod->magazine_adaptor.empty();
    if( adaptor ) {
        // changes the layout with every copy: nothing to share
        for( int i = 0; i < n; i++ ) {
            insert_item( item::spawn( proto ) );
        }
        return;
    }
    // The pocket insert_item would route to: while it is the only pocket of its type, can
    // still hold a copy and no pocket of an earlier type took the copy, every further copy
    // goes there too, so route() is not repeated.
    ensure_pockets();
    int left = n;
    while( left > 0 ) {
        detached_ptr<item> copy = item::spawn( proto );
        item_pocket *dest = route( *copy );
        const bool sole = dest->get_type() != pocket_type::MIGRATION &&
                          get_pockets( dest->get_type() ).size() == 1;
        dest->insert( std::move( copy ) );
        left--;
        while( sole && left > 0 && dest->can_contain( proto, false, true ).success() ) {
            dest->insert( item::spawn( proto ) );
            left--;
        }
    }
    contents_changed();
}

size_t item_contents::num_item_stacks() const
{
    return all_items_top().size();
}

bool item_contents::spill_contents( const tripoint_bub_ms &pos )
{
    for( detached_ptr<item> &it : clear_items() ) {
        get_map().add_item_or_charges( pos, std::move( it ) );
    }
    return true;
}

void item_contents::handle_liquid_or_spill( Character &guy )
{
    const bool had_items = !empty();
    for( item_pocket &pocket : pockets ) {
        location_vector<item> &items = pocket.items();
        for( auto iter = items.begin(); iter != items.end(); ) {
            detached_ptr<item> det;
            const bool liquid = ( *iter )->made_of( LIQUID );
            iter = items.erase( iter, &det );
            if( liquid ) {
                liquid_handler::handle_all_liquid( std::move( det ), 1 );
            } else {
                guy.i_add_or_drop( std::move( det ) );
            }
        }
    }
    if( had_items ) {
        contents_changed();
    }
}

void item_contents::casings_handle( const std::function < detached_ptr<item>
                                    ( detached_ptr<item> && ) > &func )
{
    auto changed = false;
    for( item_pocket &pocket : pockets ) {
        pocket.items().remove_with( [&func, &changed]( detached_ptr<item> &&it ) {
            if( it->has_flag( flag_CASING ) ) {
                changed = true;
                it->unset_flag( flag_CASING );
                it = func( std::move( it ) );
                if( it ) {
                    it->set_flag( flag_CASING );
                }
            }
            return std::move( it );
        } );
    }
    if( changed ) {
        contents_changed();
    }
}

std::vector<detached_ptr<item>> item_contents::clear_items()
{
    std::vector<detached_ptr<item>> ret;
    for( item_pocket &pocket : pockets ) {
        for( detached_ptr<item> &it : pocket.items().clear() ) {
            ret.push_back( std::move( it ) );
        }
    }
    contents_changed();
    return ret;
}

void item_contents::on_destroy()
{
    for( item_pocket &pocket : pockets ) {
        pocket.items().on_destroy();
    }
}

void item_contents::set_item_defaults()
{
    /* For Items with a magazine or battery in its contents */
    for( item *contained_item : all_items_top() ) {
        /* for guns and other items defined to have a magazine but don't use "ammo" */
        if( contained_item->is_magazine() ) {
            contained_item->ammo_set(
                contained_item->ammo_default(), contained_item->ammo_capacity() / 2
            );
        } else { //Contents are batteries or food
            contained_item->charges = contained_item->typeId()->charges_default();
        }
    }
}

void item_contents::migrate_item( item &obj, const std::set<itype_id> &migrations )
{
    for( const itype_id &c : migrations ) {
        if( std::ranges::none_of( all_items_top(), [&]( const item * e ) {
        return e->typeId() == c;
        } ) ) {
            obj.put_in( item::spawn( c, obj.birthday() ) );
        }
    }
}

bool item_contents::has_any_with( const std::function<bool( const item &it )> &filter ) const
{
    return std::ranges::any_of( all_items_top(), [&filter]( const item * it ) -> bool{ return filter( *it );} );
}

bool item_contents::stacks_with( const item_contents &rhs, int depth ) const
{
    const std::vector<item *> &lhs_items = all_items_top();
    const std::vector<item *> &rhs_items = rhs.all_items_top();
    if( lhs_items.size() != rhs_items.size() ) {
        return false;
    }
    return std::equal( lhs_items.begin(), lhs_items.end(), rhs_items.begin(), [depth]( const item * a,
    const item * b ) {
        return a->charges == b->charges && a->stacks_with( *b, false, false, depth );
    } );
}

item *item_contents::get_item_with( const std::function<bool( const item &it )> &filter )
{
    const std::vector<item *> &items = all_items_top();
    auto found = std::ranges::find_if( items, [&filter]( const item * it ) -> bool{ return filter( *it );} );
    if( found == items.end() ) {
        return nullptr;
    }
    return *found;
}

const std::vector<item *> &item_contents::all_items_top() const
{
    // Items can be replaced or removed from inside a pocket's location_vector (remove_with,
    // visitable), so the cache is validated element by element, not only by its size.
    bool valid = true;
    size_t index = 0;
    for( const item_pocket &pocket : pockets ) {
        for( item * const &it : pocket.items().as_vector() ) {
            if( index >= top_cache.size() || top_cache[index] != it ) {
                valid = false;
                break;
            }
            index++;
        }
        if( !valid ) {
            break;
        }
    }
    if( !valid || index != top_cache.size() ) {
        refresh_top_cache();
    }
    return top_cache;
}

detached_ptr<item> item_contents::remove_top( item *it )
{
    detached_ptr<item> ret;
    for( item_pocket &pocket : pockets ) {
        location_vector<item> &items = pocket.items();
        auto iter = std::ranges::find_if( items, [&it]( item *&against ) {
            return against == it;
        } );
        if( iter != items.end() ) {
            items.erase( iter, &ret );
            break;
        }
    }
    contents_changed();
    if( ret && ret->type->mod && !ret->type->mod->magazine_adaptor.empty() ) {
        rebuild_pockets();
    }
    return ret;
}

std::vector<item *> item_contents::all_items_ptr()
{
    std::vector<item *> ret;
    for( item *it : all_items_top() ) {
        ret.push_back( it );
        std::vector<item *> inside = it->contents.all_items_ptr();
        ret.insert( ret.end(), inside.begin(), inside.end() );
    }
    return ret;
}

std::vector<const item *> item_contents::all_items_ptr() const
{
    std::vector<const item *> ret;
    for( const item *it : all_items_top() ) {
        ret.push_back( it );
        std::vector<const item *> inside = it->contents.all_items_ptr();
        ret.insert( ret.end(), inside.begin(), inside.end() );
    }
    return ret;
}

std::vector<item *> item_contents::gunmods()
{
    // the MOD pocket; a mod parked in MIGRATION still counts, as it did before pockets
    std::vector<item *> res;
    for( item_pocket &pocket : get_pockets() ) {
        if( !pocket.is_type( pocket_type::MOD ) && !pocket.is_type( pocket_type::MIGRATION ) ) {
            continue;
        }
        for( item *e : pocket.items() ) {
            if( e->is_gunmod() ) {
                res.push_back( e );
            }
        }
    }
    return res;
}

std::vector<const item *> item_contents::gunmods() const
{
    std::vector<const item *> res;
    for( const item *e : const_cast<item_contents *>( this )->gunmods() ) {
        res.push_back( e );
    }
    return res;
}

item &item_contents::front()
{
    const std::vector<item *> &items = all_items_top();
    if( items.empty() ) {
        debugmsg( "Attempted to call front on empty item contents" );
        return null_item_reference();
    }
    return *items.front();
}

const item &item_contents::front() const
{
    return const_cast<item_contents *>( this )->front();
}

item &item_contents::back()
{
    const std::vector<item *> &items = all_items_top();
    if( items.empty() ) {
        debugmsg( "Attempted to call back on empty item contents" );
        return null_item_reference();
    }
    return *items.back();
}

const item &item_contents::back() const
{
    return const_cast<item_contents *>( this )->back();
}

units::volume item_contents::item_size_modifier() const
{
    units::volume ret = 0_ml;
    for( const item *it : all_items_top() ) {
        ret += it->volume();
    }
    return ret;
}

units::mass item_contents::item_weight_modifier() const
{
    units::mass ret = 0_gram;
    for( const item *it : all_items_top() ) {
        ret += it->weight();
    }
    return ret;
}

int item_contents::best_quality( const quality_id &id ) const
{
    int ret = INT_MIN;
    for( const item *it : all_items_top() ) {
        ret = std::max( ret, it->get_quality( id ) );
    }
    return ret;
}

void item_contents::remove_top_items_with( const std::function < detached_ptr<item>
        ( detached_ptr<item> && ) >
        &filter )
{
    remove_items_with( [&filter]( detached_ptr<item> &&e ) {
        e = filter( std::move( e ) );
        return VisitResponse::SKIP;
    } );
}
