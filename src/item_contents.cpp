#include "item_contents.h"

#include "avatar.h"
#include "character.h"
#include "clothing_mod.h"
#include "debug.h"
#include "enums.h"
#include "flag.h"
#include "game_constants.h"
#include "handle_liquid.h"
#include "item.h"
#include "item_factory.h"
#include "iteminfo_format_utils.h"
#include "iteminfo_query.h"
#include "itype.h"
#include "iuse_actor.h"
#include "locations.h"
#include "output.h"
#include "map/map.h"
#include "messages.h"
#include "string_formatter.h"
#include "string_utils.h"
#include "translations.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

struct tripoint;

namespace
{
/** The webbing size of a carrier's attach_molle action, 0 if it has none. */
auto molle_size_of( const item &carrier ) -> int
{
    const use_function *use = carrier.get_use( "attach_molle" );
    if( use == nullptr ) {
        return 0;
    }
    const molle_attach_actor *actor = dynamic_cast<const molle_attach_actor *>( use->get_actor_ptr() );
    return actor != nullptr ? actor->size : 0;
}
} // namespace

item_contents::item_contents( item *container ) : owner( container ),
    additional_pockets( new contents_item_location( container ) ) {}

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
    // attached pouches first (J5), so the pockets line up with the source's
    for( const item *shell : source.additional_pockets ) {
        add_pocket( item::spawn( *shell ) );
    }
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
    ensure_pockets();
    if( pockets.empty() ) {
        return true;
    }
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::MOD ) ) {
            // item mods aren't really contents per se
            continue;
        }
        if( !pocket.empty() ) {
            return false;
        }
    }
    return true;
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
    well->item_id_restriction = cata::flat_set<itype_id>( mags.begin(), mags.end() );
    well->raw_volume_capacity = std::max( { well->raw_volume_capacity, biggest, units::volume( 1_ml ) } );
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
    // the pocketed clothing mod adds a worn storage pocket (CDF P1c step 16)
    const auto storage_mod = static_cast<int>( owner->type != nullptr && owner->type->armor ?
                             std::lround( owner->get_clothing_mod_val( clothing_mod_type_storage ) ) : 0L );
    if( storage_mod > 0 ) {
        if( fresh.empty() ) {
            fresh = owner->type->pockets;
        }
        pocket_data mod_pocket( pocket_type::CONTAINER );
        mod_pocket.carry = true;
        mod_pocket.raw_volume_capacity = storage_mod * units::legacy_volume_factor;
        mod_pocket.rigid = owner->type->rigid;
        mod_pocket.moves = INVENTORY_HANDLING_PENALTY;
        fresh.push_back( mod_pocket );
    }
    const bool adapted = !fresh.empty();
    const std::vector<pocket_data> no_pockets;
    const std::vector<pocket_data> &type_data = owner->type != nullptr ? owner->type->pockets :
            no_pockets;
    const std::vector<pocket_data> &new_data = adapted ? fresh : type_data;

    // Same layout (e.g. a sealed can becoming an open can, a tool turning on):
    // keep the pockets and their items, only swap the data. This never moves an item.
    // The pockets of attached pouches (J5) are not part of the layout: they are kept as they are.
    const size_t n_added = added_pocket_count();
    std::vector<item_pocket *> standard;
    for( item_pocket &pocket : pockets ) {
        if( !pocket.is_type( pocket_type::MIGRATION ) ) {
            standard.push_back( &pocket );
        }
    }
    standard.resize( standard.size() >= n_added ? standard.size() - n_added : 0 );
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

    // pull the attached pouches' pockets out, so the rebuild neither empties nor drops them
    std::list<item_pocket> kept;
    {
        auto added_begin = end_of_real_pockets();
        for( size_t i = 0; i < n_added && added_begin != pockets.begin(); ++i ) {
            --added_begin;
        }
        kept.splice( kept.end(), pockets, added_begin, end_of_real_pockets() );
    }
    std::vector<detached_ptr<item>> held;
    // CDF: the player's pocket settings follow the layout change, matched by index and type
    // (DDA keeps them in update_modified_pockets, item_contents.cpp:2183)
    struct kept_settings {
        pocket_type type;
        item_pocket::favorite_settings settings;
    };
    std::vector<kept_settings> old_settings;
    for( item_pocket &pocket : pockets ) {
        if( !pocket.is_type( pocket_type::MIGRATION ) ) {
            old_settings.push_back( { pocket.get_type(), pocket.settings } );
        }
        for( detached_ptr<item> &it : pocket.items().clear() ) {
            held.push_back( std::move( it ) );
        }
    }
    pockets.clear();
    adapted_pockets = adapted ? std::move( fresh ) : std::vector<pocket_data>();
    const std::vector<pocket_data> &layout = adapted ? adapted_pockets : type_data;
    pockets_type = owner->type;
    size_t settings_idx = 0;
    for( const pocket_data &data : layout ) {
        pockets.emplace_back( owner, &data );
        if( settings_idx < old_settings.size() && old_settings[settings_idx].type == data.type ) {
            pockets.back().settings = old_settings[settings_idx].settings;
        }
        settings_idx++;
    }
    pockets.splice( pockets.end(), kept );
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

item_pocket *item_contents::fitting_pocket( const item &it, bool allow_ablative )
{
    ensure_pockets();

    // Force-insert routing, after DDA migrate_content_item: mods, magazines and ammo,
    // software, corpses, then the best container that has room. Sealed pockets are
    // allowed: spawns fill factory-sealed cans. Anything else goes to MIGRATION.
    const auto best_fitting = [&]( pocket_type type ) -> item_pocket * {
        item_pocket *best = nullptr;
        for( item_pocket &pocket : pockets )
        {
            if( !pocket.is_type( type ) || ( !allow_ablative && pocket.get_data().ablative ) ||
                !pocket.can_contain( it ).success() ) { // sealed pockets are allowed here: spawns fill factory-sealed cans (D23)
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

// Note: length is part of is_compatible(), so an over-length item spills on migration by design
// (owner decision 2026-10-13); newly spawned items never reach this state.
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
    // parallel to out: true for items re-homed from the MIGRATION pocket, which may go into
    // plate slots (spawned and migrated carriers get their plates this way)
    std::vector<bool> from_migration;
    for( item_pocket &pocket : pockets ) {
        location_vector<item> &items = pocket.items();
        if( pocket.is_type( pocket_type::MIGRATION ) ) {
            for( auto iter = items.begin(); iter != items.end(); ) {
                if( belongs_in_a_pocket( pockets, **iter ) ) {
                    detached_ptr<item> det;
                    iter = items.erase( iter, &det );
                    out.push_back( std::move( det ) );
                    from_migration.push_back( true );
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
                from_migration.push_back( false );
            } else {
                ++iter;
            }
        }
        // newest first
        while( pocket.is_overfull() ) {
            detached_ptr<item> det;
            items.erase( items.end() - 1, &det );
            out.push_back( std::move( det ) );
            from_migration.push_back( false );
        }
    }

    int moved = 0;
    map &here = get_map();
    for( size_t out_index = 0; out_index < out.size(); ++out_index ) {
        detached_ptr<item> &it = out[out_index];
        // another pocket of this item
        if( item_pocket *home = fitting_pocket( *it, from_migration[out_index] ) ) {
            home->insert( std::move( it ) );
            continue;
        }
        // the parent container
        if( parent != nullptr ) {
            const pocket_choice found = parent->contents.best_pocket( *it, owner );
            if( found.pocket != nullptr ) {
                found.container->contents.insert_into( *found.pocket, std::move( it ) );
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
    bool any_sealed = false;
    for( const item_pocket &pocket : get_pockets() ) {
        if( !pocket.is_type( type ) ) {
            continue;
        }
        any = true;
        // CDF/Lua: DDA's pocket can_contain has no sealed test, so this wrapper refuses (D23)
        if( pocket.sealed() ) {
            any_sealed = true;
            continue;
        }
        ret_val<code> res = pocket.can_contain( it );
        if( res.success() ) {
            return res;
        }
        // prefer reporting a capacity problem of a pocket that takes this kind of item
        if( !best_failure || ( !best_failure->success() && pocket.is_compatible( it ).success() ) ) {
            best_failure = res;
        }
    }
    if( any_sealed && !best_failure ) {
        return ret_val<code>::make_failure( code::ERR_NO_SPACE, _( "it is sealed" ) );
    }
    if( !any || !best_failure ) {
        return ret_val<code>::make_failure( code::ERR_FLAG, _( "it can't hold anything" ) );
    }
    return *best_failure;
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
        while( sole && left > 0 && dest->can_contain( proto ).success() ) {
            dest->insert( item::spawn( proto ) );
            left--;
        }
    }
    contents_changed();
}

size_t item_contents::num_item_stacks() const
{
    ensure_pockets();
    size_t num = 0;
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::MOD ) ||
            pocket.is_type( pocket_type::MAGAZINE_WELL ) ||
            pocket.is_type( pocket_type::CORPSE ) ) {
            // mods and magazine wells aren't really a contained item, which this function gets
            continue;
        }
        num += pocket.size();
    }
    return num;
}

bool item_contents::spill_contents( const tripoint_bub_ms &pos )
{
    ensure_pockets();
    bool spilled = false;
    for( item_pocket &pocket : pockets ) {
        // per DDA item_contents.cpp:1280: pockets of NO_UNLOAD items are not spilled
        if( !pocket.get_pocket_data()->_no_unload ) {
            spilled = pocket.spill_contents( pos ) || spilled;
        }
    }
    contents_changed();
    return spilled;
}

void item_contents::handle_liquid_or_spill( Character &guy, const item *const avoid )
{
    ensure_pockets();
    for( item_pocket &pocket : pockets ) {
        if( pocket.is_standard_type() ) {
            pocket.handle_liquid_or_spill( guy, avoid );
        }
    }
    contents_changed();
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
    additional_pockets.on_destroy();
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

void item_contents::migrate_item( item &obj, const std::vector<migration_content> &migrations )
{
    bool added = false;
    for( const migration_content &c : migrations ) {
        // BN's "only if missing" rule, made count-aware: add what is missing up to the count,
        // so a second load or a second spawn never doubles the contents
        const std::vector<item *> &top = all_items_top();
        // attached pouches are not top items: count them too, or applying twice would double them
        int present = static_cast<int>( std::ranges::count_if( top, [&c]( const item * e ) {
            return e->typeId() == c.id;
        } ) );
        present += static_cast<int>( std::ranges::count_if( additional_pockets,
        [&c]( const item * e ) {
            return e->typeId() == c.id;
        } ) );
        for( int i = present; i < c.count; ++i ) {
            detached_ptr<item> migrated = item::spawn( c.id, obj.birthday() );
            // PALS pouches are attached on purpose, as the attach action does (DDA item_group.cpp:730)
            if( migrated->can_attach_as_pocket() && molle_size_of( obj ) > 0 ) {
                const int vacancies = molle_size_of( obj ) - get_additional_space_used();
                if( migrated->get_pocket_size() <= vacancies ) {
                    add_pocket( std::move( migrated ) );
                    added = true;
                }
                continue;
            }
            // goes through the router, so a plate lands in a matching plate slot
            obj.put_in( std::move( migrated ) );
            added = true;
        }
    }
    // Items the old type left in the MIGRATION pocket may now have a pocket of their own:
    // rebuild_pockets() flags the overflow pass that re-homes them before anything is
    // moved to the wearer or the ground.
    if( added ) {
        const item_pocket *left_over = migration_pocket();
        if( left_over != nullptr && !left_over->empty() ) {
            rebuild_pockets();
        }
    }
}

bool item_contents::has_any_with( const std::function<bool( const item &it )> &filter ) const
{
    return std::ranges::any_of( all_items_top(), [&filter]( const item * it ) -> bool{ return filter( *it );} );
}

bool item_contents::stacks_with( const item_contents &rhs, int depth, int maxdepth ) const
{
    ensure_pockets();
    rhs.ensure_pockets();
    // CDF: an empty MIGRATION pocket exists on demand, so it doesn't make a difference
    const auto relevant = []( const std::list<item_pocket> &list ) {
        std::vector<const item_pocket *> res;
        for( const item_pocket &pocket : list ) {
            if( !pocket.is_type( pocket_type::MIGRATION ) || !pocket.empty() ) {
                res.push_back( &pocket );
            }
        }
        return res;
    };
    const std::vector<const item_pocket *> lhs_pockets = relevant( pockets );
    const std::vector<const item_pocket *> rhs_pockets = relevant( rhs.pockets );
    if( lhs_pockets.size() != rhs_pockets.size() ) {
        return false;
    }
    return ( empty() && rhs.empty() ) ||
           std::equal( lhs_pockets.begin(), lhs_pockets.end(), rhs_pockets.begin(),
    [depth, maxdepth]( const item_pocket * a, const item_pocket * b ) {
        return a->stacks_with( *b, depth, maxdepth );
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
    ensure_pockets();
    units::volume total_vol = 0_ml;
    for( const item_pocket &pocket : pockets ) {
        total_vol += pocket.item_size_modifier();
    }
    return total_vol;
}

units::mass item_contents::item_weight_modifier() const
{
    ensure_pockets();
    units::mass total_mass = 0_gram;
    for( const item_pocket &pocket : pockets ) {
        total_mass += pocket.item_weight_modifier();
    }
    return total_mass;
}

int item_contents::best_quality( const quality_id &id ) const
{
    ensure_pockets();
    int ret = 0;
    for( const item_pocket &pocket : pockets ) {
        ret = std::max( pocket.best_quality( id ), ret );
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

// ---------------------------------------------------------------------------------------------
// DDA item_contents.cpp (b4b62254ae2) transplant, J3. Bodies follow DDA; CDF differences are
// tagged `// CDF:`. `contents` in DDA is `pockets` here and every reader calls ensure_pockets().
// ---------------------------------------------------------------------------------------------

item_contents::item_contents( const std::vector<pocket_data> &pockets_data ) :
    additional_pockets( new contents_item_location( nullptr ) )
{
    // CDF: ownerless contents (DDA builds them for item types); the data must outlive us
    for( const pocket_data &data : pockets_data ) {
        pockets.emplace_back( nullptr, &data );
    }
}

bool item_contents::empty_with_no_mods() const
{
    ensure_pockets();
    return pockets.empty() ||
    std::ranges::all_of( pockets, []( const item_pocket & p ) {
        return p.is_default_state();
    } );
}

bool item_contents::empty_container() const
{
    ensure_pockets();
    for( const item_pocket &pocket : pockets ) {
        if( !pocket.is_type( pocket_type::CONTAINER ) ) {
            continue;
        }
        if( !pocket.empty() ) {
            return false;
        }
    }
    return true;
}

bool item_contents::full( bool allow_bucket ) const
{
    ensure_pockets();
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::CONTAINER ) && !pocket.full( allow_bucket ) ) {
            return false;
        }
    }
    return true;
}

bool item_contents::is_magazine_full() const
{
    ensure_pockets();
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::MAGAZINE ) && pocket.full( false ) ) {
            return true;
        }
    }
    return false;
}

bool item_contents::bigger_on_the_inside( const units::volume &container_volume ) const
{
    ensure_pockets();
    units::volume min_logical_volume = 0_ml;
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::CONTAINER ) ) {
            if( pocket.rigid() ) {
                min_logical_volume += pocket.volume_capacity();
            } else {
                min_logical_volume += pocket.magazine_well();
            }
        }
    }
    return container_volume <= min_logical_volume;
}

size_t item_contents::size() const
{
    ensure_pockets();
    return pockets.size();
}

void item_contents::read_mods( const item_contents &read_input )
{
    read_input.ensure_pockets();
    for( const item_pocket &pocket : read_input.pockets ) {
        if( pocket.saved_type() == pocket_type::MOD ) {
            for( const item *it : pocket.all_items_top() ) {
                if( it->is_gunmod() || it->is_toolmod() ) {
                    insert_item( *it, pocket_type::MOD );
                } else {
                    debugmsg( "Non-mod %s in MOD pocket!", it->tname() );
                    insert_item( *it, pocket_type::MIGRATION );
                }
            }
        }
    }
}

void item_contents::combine( const item_contents &read_input, const bool convert,
                             const bool into_bottom, bool restack_charges, bool ignore_contents )
{
    ensure_pockets();
    read_input.ensure_pockets();
    std::vector<const item_pocket *> mismatched_pockets;
    // CDF: T2, the DDA copies in `std::vector<item>` are detached copies
    std::vector<detached_ptr<item>> uninserted_items;
    size_t pocket_index = 0;

    // DDA: the pouches attached to read_input come first, so their pockets line up
    for( const item *shell : read_input.additional_pockets ) {
        add_pocket( *shell );
    }

    for( const item_pocket &pocket : read_input.pockets ) {
        if( pocket_index < pockets.size() ) {
            if( convert ) {
                if( pocket.is_type( pocket_type::MIGRATION ) ||
                    pocket.is_type( pocket_type::CORPSE ) ||
                    pocket.is_type( pocket_type::MAGAZINE ) ||
                    pocket.is_type( pocket_type::MAGAZINE_WELL ) ) {
                    ++pocket_index;
                    for( const item *it : pocket.all_items_top() ) {
                        insert_item( *it, pocket.get_pocket_data()->type, ignore_contents );
                    }
                    continue;
                } else if( pocket.is_type( pocket_type::MOD ) ) {
                    // skipping mod type pocket because using combine this way requires mods to come first
                    // and to call update_mod_pockets
                    ++pocket_index;
                    continue;
                }
            } else {
                if( pocket.saved_type() == pocket_type::MOD ) {
                    // this is already handled in item_contents::read_mods
                    ++pocket_index;
                    continue;
                } else if( pocket.saved_type() == pocket_type::MIGRATION ||
                           pocket.saved_type() == pocket_type::CORPSE ) {
                    for( const item *it : pocket.all_items_top() ) {
                        insert_item( *it, pocket.saved_type(), ignore_contents );
                    }
                    ++pocket_index;
                    continue;
                }
            }
            auto current_pocket_iter = pockets.begin();
            std::advance( current_pocket_iter, pocket_index );

            if( !current_pocket_iter->is_type( convert ? pocket.get_pocket_data()->type :
                                               pocket.saved_type() ) ) {
                mismatched_pockets.push_back( &pocket );
                continue;
            }

            for( const item *it : pocket.all_items_top() ) {
                detached_ptr<item> copy = item::spawn( *it );
                const ret_val<item *> inserted = current_pocket_iter->insert_item( std::move( copy ),
                                                 into_bottom, restack_charges, ignore_contents );
                if( !inserted.success() ) {
                    // consumed only on success, so the copy is still ours
                    uninserted_items.push_back( std::move( copy ) );
                    DebugLog( DL::Warn, DC::Game ) <<
                                                   "error: item " << it->typeId().str() << "cannot fit into pocket while loading: " << inserted.str();
                }
            }

            if( pocket.saved_sealed() ) {
                current_pocket_iter->seal();
            }
            current_pocket_iter->settings = pocket.settings;
        } else {
            for( const item *it : pocket.all_items_top() ) {
                uninserted_items.push_back( item::spawn( *it ) );
            }
        }
        ++pocket_index;
    }

    for( const item_pocket *pocket : mismatched_pockets ) {
        const pocket_type mismatched_type = convert ? pocket->get_pocket_data()->type :
                                            pocket->saved_type();
        for( const item *it : pocket->all_items_top() ) {
            detached_ptr<item> copy = item::spawn( *it );
            const ret_val<item *> inserted = insert_item( std::move( copy ), mismatched_type,
                                             ignore_contents );
            if( !inserted.success() ) {
                debugmsg( "error: item %s cannot fit into any pocket while loading: %s",
                          it->typeId().str(), inserted.str() );
                uninserted_items.push_back( std::move( copy ) );
            }
        }
    }

    for( detached_ptr<item> &uninserted_item : uninserted_items ) {
        if( uninserted_item ) {
            insert_item( std::move( uninserted_item ), pocket_type::MIGRATION, ignore_contents );
        }
    }
    contents_changed();
}

namespace
{

// Static helper function to implement the const and non-const versions of find_pocket_for with
// less code duplication (DDA item_contents_helper)
template<typename Pockets>
auto find_pocket_for_impl( Pockets &pockets, const item &it, pocket_type pk_type )
{
    using my_pocket_type = std::conditional_t <
                           std::is_const_v<Pockets>,
                           const item_pocket,
                           item_pocket
                           >;
    static constexpr my_pocket_type *null_pocket = nullptr;

    std::vector<std::string> failure_messages;
    int num_pockets_of_type = 0;

    for( my_pocket_type &pocket : pockets ) {
        if( !pocket.is_type( pk_type ) ) {
            continue;
        }
        if( pk_type != pocket_type::CONTAINER &&
            pk_type != pocket_type::MAGAZINE &&
            pk_type != pocket_type::MAGAZINE_WELL &&
            pocket.is_type( pk_type ) ) {
            return ret_val<my_pocket_type *>::make_success(
                       &pocket, "special pocket type override" );
        }
        ++num_pockets_of_type;
        ret_val<item_pocket::contain_code> ret_contain = pocket.can_contain( it );
        if( ret_contain.success() ) {
            return ret_val<my_pocket_type *>::make_success( &pocket, ret_contain.str() );
        } else {
            failure_messages.push_back( ret_contain.str() );
        }
    }

    if( failure_messages.empty() ) {
        return ret_val<my_pocket_type *>::make_failure( null_pocket,
                _( "pocket with type (%s) not found" ),
                io::enum_to_string( pk_type ) );
    }
    std::sort( failure_messages.begin(), failure_messages.end(), localized_compare );
    failure_messages.erase(
        std::unique( failure_messages.begin(), failure_messages.end() ),
        failure_messages.end() );
    return ret_val<my_pocket_type *>::make_failure(
               null_pocket,
               vgettext( "pocket unacceptable because %s", "pockets unacceptable because %s",
                         num_pockets_of_type ),
               enumerate_as_string( failure_messages, enumeration_conjunction::or_ ) );
}

} // namespace

ret_val<item_pocket *> item_contents::find_pocket_for( const item &it, pocket_type pk_type )
{
    ensure_pockets();
    if( pk_type == pocket_type::MIGRATION ) {
        // CDF: the MIGRATION pocket exists on demand
        migration_pocket( true );
    }
    return find_pocket_for_impl( pockets, it, pk_type );
}

ret_val<const item_pocket *> item_contents::find_pocket_for( const item &it,
        pocket_type pk_type ) const
{
    ensure_pockets();
    return find_pocket_for_impl( pockets, it, pk_type );
}

int item_contents::obtain_cost( const item &it ) const
{
    ensure_pockets();
    for( const item_pocket &pocket : pockets ) {
        const int mv = pocket.obtain_cost( it );
        if( mv != 0 ) {
            return mv;
        }
    }
    return 0;
}

int item_contents::insert_cost( const item &it ) const
{
    ret_val<const item_pocket *> pocket = find_pocket_for( it, pocket_type::CONTAINER );
    if( pocket.success() ) {
        return pocket.value()->moves();
    } else {
        return -1;
    }
}

ret_val<item *> item_contents::insert_item( detached_ptr<item> &&it, pocket_type pk_type,
        bool ignore_contents, const bool unseal_pockets )
{
    if( !it ) {
        return ret_val<item *>::make_failure( nullptr, _( "There is nothing to put in." ) );
    }
    if( pk_type == pocket_type::LAST ) {
        // LAST is invalid, so we assume it will be a regular container
        pk_type = pocket_type::CONTAINER;
    }

    ret_val<item_pocket *> pocket = find_pocket_for( *it, pk_type );
    if( !pocket.success() ) {
        return ret_val<item *>::make_failure( nullptr, pocket.str() );
    }

    const bool adaptor = it->type->mod && !it->type->mod->magazine_adaptor.empty();
    ret_val<item *> inserted = pocket.value()->insert_item( std::move( it ), false, true,
                               ignore_contents );
    if( inserted.success() ) {
        if( unseal_pockets ) {
            pocket.value()->unseal();
        }
        contents_changed();
        if( adaptor ) {
            // the mod changes which magazines fit (DDA #41917)
            rebuild_pockets();
        }
        return inserted;
    }
    return ret_val<item *>::make_failure( nullptr, inserted.str() );
}

ret_val<item *> item_contents::insert_item( const item &it, pocket_type pk_type,
        bool ignore_contents, bool unseal_pockets )
{
    // T2: the DDA spelling inserts a copy
    return insert_item( item::spawn( it ), pk_type, ignore_contents, unseal_pockets );
}

void item_contents::force_insert_item( detached_ptr<item> &&it, pocket_type pk_type )
{
    ensure_pockets();
    if( pk_type == pocket_type::MIGRATION ) {
        // CDF: the MIGRATION pocket exists on demand
        migration_pocket( true );
    }
    for( item_pocket &pocket : pockets ) {
        if( pocket.is_type( pk_type ) ) {
            pocket.add( std::move( it ) );
            contents_changed();
            return;
        }
    }
    debugmsg( "ERROR: Could not insert item %s as contents does not have pocket type",
              it ? it->tname() : std::string( "(null)" ) );
}

void item_contents::force_insert_item( const item &it, pocket_type pk_type )
{
    force_insert_item( item::spawn( it ), pk_type );
}

pocket_choice item_contents::best_pocket( const item &it, const item *avoid,
        const bool allow_sealed, const bool ignore_settings, const bool nested, bool ignore_rigidity,
        bool allow_nested )
{
    ensure_pockets();
    // @TODO: this could be made better by doing a plain preliminary volume check.
    // if the total volume of the parent is not sufficient, a child won't have enough either.
    pocket_choice ret{ owner, nullptr };
    if( owner == &it || ( avoid != nullptr && owner == avoid ) ) {
        // CDF: DDA reaches this through the avoid check of best_pocket_in_contents
        return ret;
    }
    std::vector<item_pocket *> valid_pockets;
    for( item_pocket &pocket : pockets ) {
        if( pocket.is_forbidden() ) {
            continue;
        }
        if( !pocket.is_type( pocket_type::CONTAINER ) ) {
            // best pocket is for picking stuff up.
            // containers are the only pockets that are available for such...
            continue;
        }
        if( !allow_sealed && pocket.sealed() ) {
            // we don't want to unseal a pocket to put something in it automatically
            // that needs to be something a player explicitly does
            continue;
        }
        if( allow_nested ) {
            //if not allow check nested pockets ,it is no need to save valid pockets
            valid_pockets.emplace_back( &pocket );
        }
        if( !ignore_settings && !pocket.settings.accepts_item( it ) ) {
            // Item forbidden by whitelist / blacklist
            continue;
        }
        if( !pocket.rigid() && (
                !pocket.settings.get_item_whitelist().empty() ||
                !pocket.settings.get_category_whitelist().empty() ||
                pocket.settings.priority() > 0 ) ) {
            ignore_rigidity = true;
        }
        if( ( !ignore_rigidity && nested && !pocket.rigid() ) || !pocket.can_contain( it ).success() ) {
            // non-rigid nested pocket makes no sense, item should also be able to fit in parent.
            continue;
        }
        if( ret.pocket == nullptr || ret.pocket->better_pocket( pocket, it, /*nested=*/nested ) ) {
            ret.pocket = &pocket;
        }
    }
    if( allow_nested && owner != nullptr ) {
        const bool parent_pkt_selected = ret.pocket != nullptr;
        for( item_pocket *pocket : valid_pockets ) {
            pocket_choice nested_content_pocket =
                pocket->best_pocket_in_contents( *owner, it, avoid, allow_sealed, ignore_settings );
            if( !nested_content_pocket.pocket ||
                ( !nested_content_pocket.pocket->rigid() && pocket->remaining_volume() < it.volume() ) ) {
                // no nested pocket found, or the nested pocket is soft and the parent is full
                continue;
            }
            if( parent_pkt_selected ) {
                if( ret.pocket->better_pocket( *nested_content_pocket.pocket, it, true ) ) {
                    // item is whitelisted in nested pocket, prefer that over parent pocket
                    ret = nested_content_pocket;
                }
                continue;
            }
            ret = nested_content_pocket;
        }
    }
    return ret;
}

units::length item_contents::max_containable_length( const bool unrestricted_pockets_only ) const
{
    ensure_pockets();
    units::length ret = 0_mm;
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_forbidden() ) {
            continue;
        }
        bool restriction_condition = !pocket.is_type( pocket_type::CONTAINER ) ||
                                     pocket.is_ablative() || pocket.holster_full();
        if( unrestricted_pockets_only ) {
            restriction_condition = restriction_condition && pocket.is_restricted();
        }
        if( restriction_condition ) {
            continue;
        }
        units::length candidate = pocket.max_containable_length();
        if( candidate > ret ) {
            ret = candidate;
        }
    }
    return ret;
}

units::length item_contents::min_containable_length() const
{
    ensure_pockets();
    units::length ret = 0_mm;
    for( const item_pocket &pocket : pockets ) {
        if( !pocket.is_type( pocket_type::CONTAINER ) || pocket.is_ablative() ||
            pocket.holster_full() ) {
            continue;
        }
        units::length candidate = pocket.min_containable_length();
        if( candidate > ret ) {
            ret = candidate;
        }
    }
    return ret;
}

std::set<flag_id> item_contents::magazine_flag_restrictions() const
{
    ensure_pockets();
    std::set<flag_id> ret;
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::MAGAZINE_WELL ) ) {
            const pocket_data::FlagsSetType &src = pocket.get_pocket_data()->get_flag_restrictions();
            ret.clear();
            ret.insert( src.begin(), src.end() );
        }
    }
    return ret;
}

units::volume item_contents::max_containable_volume( const bool unrestricted_pockets_only ) const
{
    ensure_pockets();
    units::volume ret = 0_ml;
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_forbidden() ) {
            continue;
        }
        bool restriction_condition = !pocket.is_type( pocket_type::CONTAINER ) ||
                                     pocket.is_ablative() || pocket.holster_full() ||
                                     pocket.volume_capacity() >= pocket_data::max_volume_for_container;
        if( unrestricted_pockets_only ) {
            restriction_condition = restriction_condition && pocket.is_restricted();
        }
        if( restriction_condition ) {
            continue;
        }
        units::volume candidate = pocket.remaining_volume();
        if( candidate > ret ) {
            ret = candidate;
        }

    }
    return ret;
}

ret_val<void> item_contents::is_compatible( const item &it ) const
{
    ensure_pockets();
    ret_val<void> ret = ret_val<void>::make_failure( _( "is not a container" ) );
    for( const item_pocket &pocket : pockets ) {
        // mod, migration, corpse, and software aren't regular pockets.
        if( !pocket.is_standard_type() ) {
            continue;
        }
        const ret_val<item_pocket::contain_code> pocket_contain_code = pocket.is_compatible( it );
        if( pocket_contain_code.success() ) {
            return ret_val<void>::make_success();
        }
        ret = ret_val<void>::make_failure( pocket_contain_code.str() );
    }
    return ret;
}

ret_val<void> item_contents::can_contain_rigid( const item &it,
        const bool ignore_pkt_settings, const bool ignore_non_container_pocket ) const
{
    int copies = 1;
    return can_contain_rigid( it, copies, ignore_pkt_settings, ignore_non_container_pocket );
}

ret_val<void> item_contents::can_contain_rigid( const item &it, int &copies_remaining,
        const bool ignore_pkt_settings, const bool ignore_non_container_pocket ) const
{
    ensure_pockets();
    ret_val<void> ret = ret_val<void>::make_failure( _( "is not a container" ) );
    for( const item_pocket &pocket : pockets ) {
        // Only count container in pickup_inventory_preset.
        if( ignore_non_container_pocket && !pocket.is_type( pocket_type::CONTAINER ) ) {
            continue;
        }
        if( pocket.is_type( pocket_type::MOD ) ||
            pocket.is_type( pocket_type::CORPSE ) ||
            pocket.is_type( pocket_type::MIGRATION ) ) {
            continue;
        }
        if( !pocket.rigid() ) {
            ret = ret_val<void>::make_failure( _( "is not rigid" ) );
            continue;
        }
        if( !ignore_pkt_settings && !pocket.settings.accepts_item( it ) ) {
            ret = ret_val<void>::make_failure( _( "denied by pocket auto insert settings" ) );
            continue;
        }
        const ret_val<item_pocket::contain_code> pocket_contain_code = pocket.can_contain( it,
                copies_remaining, false );
        if( copies_remaining <= 0 || pocket_contain_code.success() ) {
            return ret_val<void>::make_success();
        }
        ret = ret_val<void>::make_failure( pocket_contain_code.str() );
    }
    return ret;
}

ret_val<void> item_contents::can_contain( const item &it, const bool ignore_pkt_settings,
        const bool ignore_non_container_pocket, units::volume remaining_parent_volume ) const
{
    int copies = 1;
    return can_contain( it, copies, ignore_pkt_settings, ignore_non_container_pocket,
                        remaining_parent_volume );
}

ret_val<void> item_contents::can_contain( const item &it, int &copies_remaining,
        const bool ignore_pkt_settings, const bool ignore_non_container_pocket,
        units::volume remaining_parent_volume ) const
{
    ensure_pockets();
    ret_val<void> ret = ret_val<void>::make_failure( _( "is not a container" ) );
    bool has_ret = false;

    if( copies_remaining <= 0 ) {
        return ret_val<void>::make_success();
    }
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_forbidden() ) {
            continue;
        }
        // Only count container in pickup_inventory_preset.
        if( ignore_non_container_pocket && !pocket.is_type( pocket_type::CONTAINER ) ) {
            continue;
        }
        // mod, migration, corpse, and software aren't regular pockets.
        if( !pocket.is_standard_type() ) {
            continue;
        }
        if( !ignore_pkt_settings && !pocket.settings.accepts_item( it ) ) {
            ret = ret_val<void>::make_failure( _( "denied by pocket auto insert settings" ) );
            continue;
        }
        if( !pocket.rigid() && it.volume() > remaining_parent_volume ) {
            continue;
        }
        const ret_val<item_pocket::contain_code> pocket_contain_code = pocket.can_contain( it,
                copies_remaining );
        if( copies_remaining <= 0 ) {
            return ret_val<void>::make_success();
        }
        int n = copies_remaining;
        bool could_contain = pocket.can_contain( it, n, /*ignore_contents=*/true ).success();
        if( has_ret && !could_contain ) {
            // This pocket could never contain the item, even if the pocket was emptied.
            // So we would prefer to get the failure msg from another pocket which could potentially hold the item.
            continue;
        }
        ret = ret_val<void>::make_failure( pocket_contain_code.str() );
        has_ret = true;
    }
    return ret;
}

bool item_contents::can_contain_liquid( bool held_or_ground ) const
{
    ensure_pockets();
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::CONTAINER ) &&
            pocket.can_contain_liquid( held_or_ground ) ) {
            return true;
        }
    }
    return false;
}

bool item_contents::contains_no_solids() const
{
    ensure_pockets();
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::CONTAINER ) &&
            pocket.contains_phase( phase_id::SOLID ) ) {
            return false;
        }
    }

    return true;
}

bool item_contents::can_reload_with( const item &ammo, const bool now ) const
{
    // CDF: ensure_pockets() as in the other item_contents bodies
    ensure_pockets();
    for( const item_pocket *pocket : get_all_reloadable_pockets() ) {
        if( pocket->can_reload_with( ammo, now ) ) {
            return true;
        }
    }
    return false;

}

bool item_contents::can_unload_liquid() const
{
    ensure_pockets();
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::CONTAINER ) && pocket.can_unload_liquid() ) {
            return true;
        }
    }
    return false;
}

void item_contents::on_pickup( Character &guy, item *avoid )
{
    ensure_pockets();
    for( item_pocket &pocket : pockets ) {
        if( !pocket.is_type( pocket_type::MOD ) ) {
            pocket.on_pickup( guy, avoid );
        }
    }
    contents_changed();
}

void item_contents::overflow( const tripoint_bub_ms &pos, Character *carrier )
{
    ensure_pockets();
    pending_overflow = false;
    // CDF: T4/T5/T7, a pocket sheds into the pockets above its owner, then the carrier's
    // surroundings, then the map at pos
    for( item_pocket &pocket : pockets ) {
        pocket.overflow( pos, owner, carrier );
    }
    contents_changed();
}

void item_contents::clear_magazines()
{
    ensure_pockets();
    for( item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::MAGAZINE ) ) {
            pocket.clear_items();
        }
    }
    contents_changed();
}

void item_contents::clear_pockets_if( const std::function<bool( item_pocket const & )> &filter )
{
    ensure_pockets();
    for( item_pocket &pocket : pockets ) {
        if( filter( pocket ) ) {
            pocket.clear_items();
        }
    }
    contents_changed();
}

void item_contents::update_open_pockets()
{
    ensure_pockets();
    for( item_pocket &pocket : pockets ) {
        if( pocket.empty() ) {
            pocket.unseal();
        }
    }
}

bool item_contents::seal_all_pockets()
{
    ensure_pockets();
    bool any_sealed = false;
    for( item_pocket &pocket : pockets ) {
        any_sealed = pocket.seal() || any_sealed;
    }
    return any_sealed;
}

bool item_contents::all_pockets_sealed() const
{
    ensure_pockets();
    bool all_sealed = false;
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::CONTAINER ) ) {
            if( !pocket.sealed() ) {
                return false;
            } else {
                all_sealed = true;
            }
        }
    }

    return all_sealed;
}

bool item_contents::any_pockets_sealed() const
{
    ensure_pockets();
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::CONTAINER ) ) {
            if( pocket.sealed() ) {
                return true;
            }
        }
    }

    return false;
}

int item_contents::ammo_consume( int qty, const tripoint_bub_ms &pos, float fuel_efficiency )
{
    return item_contents::ammo_consume( qty, &get_map(), pos, fuel_efficiency );
}

int item_contents::ammo_consume( int qty, map *here, const tripoint_bub_ms &pos,
                                 float fuel_efficiency )
{
    ensure_pockets();
    int consumed = 0;
    for( item_pocket &pocket : pockets ) {
        if( qty <= 0 ) {
            break;
        }
        if( pocket.is_type( pocket_type::MAGAZINE_WELL ) ) {
            if( pocket.empty() ) {
                continue;
            }
            item *mag = pocket.magazine_current();
            if( mag == nullptr ) {
                continue;
            }
            // CDF: no 4-arg item::ammo_consume( int, map&, pos, carrier ); the BN 2-arg
            // one consumes straight from the magazine's own contents.
            const int res = mag->ammo_consume( qty, pos );
            if( res && mag->ammo_remaining() == 0 ) {
                if( mag->has_flag( flag_MAG_DESTROY ) ) {
                    pocket.remove_item( mag );
                } else if( mag->has_flag( flag_MAG_EJECT ) ) {
                    // CDF: pocket.remove_item returns the detached item to eject.
                    here->add_item( pos, pocket.remove_item( mag ) );
                }
            }
            qty -= res;
            consumed += res;
        } else if( pocket.is_type( pocket_type::MAGAZINE ) ) {
            if( !pocket.empty() && pocket.front().is_fuel() && fuel_efficiency >= 0 ) {
                // if using fuel instead of battery, everything is in kJ
                // charges is going to be the energy needed over the energy in 1 unit of fuel * the efficiency of the generator
                // CDF: item::fuel_energy() is kJ per charge (AUTO-23), DDA's is an energy per
                // volume; from_kilojoule() restores DDA's joule arithmetic.
                int charges_used = ceil( static_cast<float>( units::from_kilojoule( static_cast<std::int64_t>
                                         ( qty ) ).value() ) / (
                                             static_cast<float>( units::from_kilojoule( static_cast<std::int64_t>
                                                     ( pocket.front().fuel_energy() ) ).value() ) * fuel_efficiency ) );

                const int res = pocket.ammo_consume( charges_used );
                //calculate the amount of energy generated
                int energy_generated = res * units::to_kilojoule( units::from_kilojoule(
                                           static_cast<std::int64_t>( pocket.front().fuel_energy() ) ) );
                consumed += energy_generated;
                qty -= energy_generated;
                qty = std::max( 0, qty );
            } else {
                const int res = pocket.ammo_consume( qty );
                consumed += res;
                qty -= res;
            }
        }
    }
    contents_changed();
    return consumed;
}

int item_contents::ammo_consume_in_pocket( const std::string &id, int qty, map *here,
        const tripoint_bub_ms &pos )
{
    if( id.empty() || qty <= 0 ) {
        return 0;
    }
    ensure_pockets();
    for( item_pocket &pocket : pockets ) {
        const pocket_data *pd = pocket.get_pocket_data();
        if( pd == nullptr || pd->pocket_id != id ) {
            continue;
        }
        if( pocket.is_type( pocket_type::MAGAZINE_WELL ) ) {
            item *mag = pocket.magazine_current();
            if( mag == nullptr ) {
                return 0;
            }
            // CDF: no 4-arg item::ammo_consume( int, map&, pos, carrier ); use the BN 2-arg one.
            const int res = mag->ammo_consume( qty, pos );
            if( res && mag->ammo_remaining() == 0 ) {
                if( mag->has_flag( flag_MAG_DESTROY ) ) {
                    pocket.remove_item( mag );
                } else if( mag->has_flag( flag_MAG_EJECT ) ) {
                    // CDF: pocket.remove_item returns the detached item to eject.
                    here->add_item( pos, pocket.remove_item( mag ) );
                }
            }
            contents_changed();
            return res;
        }
        if( pocket.is_type( pocket_type::MAGAZINE ) ) {
            const int res = pocket.ammo_consume( qty );
            contents_changed();
            return res;
        }
        return 0;
    }
    return 0;
}

item *item_contents::magazine_current()
{
    ensure_pockets();
    for( item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::MAGAZINE_WELL ) ) {
            item *mag = pocket.magazine_current();
            if( mag != nullptr ) {
                return mag;
            }
        }
    }
    return nullptr;
}

const item *item_contents::magazine_current() const
{
    return const_cast<item_contents *>( this )->magazine_current();
}

std::vector<item *> item_contents::magazines_current()
{
    ensure_pockets();
    std::vector<item *> result;
    for( item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::MAGAZINE_WELL ) ) {
            item *mag = pocket.magazine_current();
            if( mag != nullptr ) {
                result.push_back( mag );
            }
        }
    }
    return result;
}

std::vector<const item *> item_contents::magazines_current() const
{
    std::vector<const item *> result;
    for( const item *mag : const_cast<item_contents *>( this )->magazines_current() ) {
        result.push_back( mag );
    }
    return result;
}

int item_contents::ammo_capacity( const ammotype &ammo ) const
{
    ensure_pockets();
    int ret = 0;
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::MAGAZINE ) ) {
            ret += pocket.ammo_capacity( ammo );
        }
    }
    return ret;
}

std::set<ammotype> item_contents::ammo_types() const
{
    ensure_pockets();
    std::set<ammotype> ret;
    for( const item_pocket &pocket : pockets ) {
        for( const ammotype &ammo : pocket.ammo_types() ) {
            ret.emplace( ammo );
        }
    }
    return ret;
}

item &item_contents::first_ammo()
{
    ensure_pockets();
    if( pockets.empty() ) {
        debugmsg( "Error: Contents has no pockets" );
        return null_item_reference();
    }
    for( item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::MAGAZINE_WELL ) ) {
            if( pocket.empty() ) {
                continue;
            }
            item *mag = pocket.magazine_current();
            if( mag != nullptr ) {
                item &ammo = mag->contents.first_ammo();
                if( !ammo.is_null() ) {
                    return ammo;
                }
            }
            continue;
        }
        if( !pocket.is_type( pocket_type::MAGAZINE ) || pocket.empty() ) {
            continue;
        }
        if( pocket.front().has_flag( flag_CASING ) ) {
            for( item *i : pocket.all_items_top() ) {
                if( !i->has_flag( flag_CASING ) ) {
                    return *i;
                }
            }
            continue;
        }
        return pocket.front();
    }
    return null_item_reference();
}

const item &item_contents::first_ammo() const
{
    return const_cast<item_contents *>( this )->first_ammo();
}

bool item_contents::will_spill() const
{
    ensure_pockets();
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_standard_type() && pocket.will_spill() ) {
            return true;
        }
    }
    return false;
}

bool item_contents::will_spill_if_unsealed() const
{
    ensure_pockets();
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_standard_type() && pocket.will_spill_if_unsealed() ) {
            return true;
        }
    }
    return false;
}

bool item_contents::spill_open_pockets( Character &guy, const item *avoid )
{
    ensure_pockets();
    for( item_pocket &pocket : pockets ) {
        if( pocket.is_standard_type() && pocket.will_spill() ) {
            pocket.handle_liquid_or_spill( guy, avoid );
            if( !pocket.empty() ) {
                contents_changed();
                return false;
            }
        }
    }
    contents_changed();
    return true;
}

bool item_contents::has_pocket_type( const pocket_type pk_type ) const
{
    ensure_pockets();
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pk_type ) ) {
            return true;
        }
    }
    return false;
}

bool item_contents::has_unrestricted_pockets() const
{
    ensure_pockets();
    int restricted_pockets_qty = 0;
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_restricted() ) {
            restricted_pockets_qty++;
        }
    }
    return restricted_pockets_qty < static_cast <int>( get_container_pockets().size() );
}

bool item_contents::has_any_with( const std::function<bool( const item &it )> &filter,
                                  pocket_type pk_type ) const
{
    ensure_pockets();
    for( const item_pocket &pocket : pockets ) {
        if( !pocket.is_type( pk_type ) ) {
            continue;
        }
        if( pocket.has_any_with( filter ) ) {
            return true;
        }
    }
    return false;
}

bool item_contents::same_contents( const item_contents &rhs ) const
{
    ensure_pockets();
    rhs.ensure_pockets();
    if( pockets.size() != rhs.pockets.size() ) {
        return false;
    }
    return std::equal( pockets.begin(), pockets.end(),
                       rhs.pockets.begin(), rhs.pockets.end(),
    []( const item_pocket & a, const item_pocket & b ) {
        return a.same_contents( b );
    } );
}

bool item_contents::is_funnel_container( units::volume &bigger_than ) const
{
    ensure_pockets();
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::CONTAINER ) ) {
            if( pocket.is_funnel_container( bigger_than ) ) {
                return true;
            }
        }
    }
    return false;
}

bool item_contents::is_restricted_container() const
{
    ensure_pockets();
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_restricted() ) {
            return true;
        }
    }
    return false;
}

bool item_contents::is_single_restricted_container() const
{
    std::vector<const item_pocket *> const contained_pockets = get_container_pockets();
    return contained_pockets.size() == 1 && contained_pockets[0]->is_restricted();
}

item &item_contents::first_item()
{
    ensure_pockets();
    for( item_pocket &pocket : pockets ) {
        if( pocket.empty() || !pocket.is_type( pocket_type::CONTAINER ) ) {
            continue;
        }
        // the first item we come to is the only one.
        return pocket.front();
    }
    return null_item_reference();
}

item &item_contents::only_item()
{
    if( num_item_stacks() != 1 ) {
        debugmsg( "ERROR: item_contents::only_item called with %d items contained", num_item_stacks() );
        return null_item_reference();
    }
    return first_item();
}

const item &item_contents::first_item() const
{
    return const_cast<item_contents *>( this )->first_item();
}

const item &item_contents::only_item() const
{
    return const_cast<item_contents *>( this )->only_item();
}

const item *item_contents::get_item_with( const std::function<bool( const item &it )> &filter )
const
{
    return const_cast<item_contents *>( this )->get_item_with( filter );
}

void item_contents::remove_items_if( const std::function<bool( item & )> &filter )
{
    ensure_pockets();
    for( item_pocket &pocket : pockets ) {
        pocket.remove_items_if( filter );
    }
    contents_changed();
}

std::vector<item *> item_contents::all_items_top( const std::function<bool( item_pocket & )>
        &filter )
{
    ensure_pockets();
    std::vector<item *> all_items_internal;
    for( item_pocket &pocket : pockets ) {
        if( filter( pocket ) ) {
            const std::vector<item *> contained_items = pocket.all_items_top();
            all_items_internal.insert( all_items_internal.end(), contained_items.begin(),
                                       contained_items.end() );
        }
    }
    return all_items_internal;
}

std::vector<item *> item_contents::all_items_top( pocket_type pk_type, bool unloading )
{
    if( unloading ) {
        return all_items_top( [pk_type]( item_pocket & pocket ) {
            return pocket.is_type( pk_type ) && pocket.settings.is_unloadable();
        } );
    }
    return all_items_top( [pk_type]( item_pocket & pocket ) {
        return pocket.is_type( pk_type );
    } );
}

std::vector<const item *> item_contents::all_items_top( const
        std::function<bool( const item_pocket & )> &filter ) const
{
    ensure_pockets();
    std::vector<const item *> all_items_internal;
    for( const item_pocket &pocket : pockets ) {
        if( filter( pocket ) ) {
            const std::vector<const item *> contained_items = pocket.all_items_top();
            all_items_internal.insert( all_items_internal.end(), contained_items.begin(),
                                       contained_items.end() );
        }
    }
    return all_items_internal;
}

std::vector<const item *> item_contents::all_items_top( pocket_type pk_type ) const
{
    return all_items_top( [pk_type]( const item_pocket & pocket ) {
        return pocket.is_type( pk_type );
    } );
}

content_newness item_contents::get_content_newness( const std::set<itype_id> &read_items ) const
{
    content_newness ret = content_newness::SEEN;
    for( const item_pocket *pocket : get_standard_pockets() ) {
        // taking transparent from item_contents::all_known_contents()
        if( !pocket->transparent() ) {
            ret = content_newness::MIGHT_BE_HIDDEN;
            continue;
        }
        for( const item *itm : pocket->all_items_top() ) {
            if( !read_items.count( itm->typeId() ) ) {
                return content_newness::NEW;
            }
            switch( itm->contents.get_content_newness( read_items ) ) {
                case content_newness::NEW:
                    return content_newness::NEW;
                case content_newness::MIGHT_BE_HIDDEN:
                    ret = content_newness::MIGHT_BE_HIDDEN;
                    break;
                case content_newness::SEEN:
                    break;
            }
        }
    }
    return ret;
}

std::vector<const item *> item_contents::all_items_container_top() const
{
    return all_items_top( []( const item_pocket & pocket ) {
        return pocket.is_container_like_type();
    } );
}

std::vector<item *> item_contents::all_items_container_top()
{
    return all_items_top( []( item_pocket & pocket ) {
        return pocket.is_container_like_type();
    } );
}

std::vector<item *> item_contents::all_known_contents()
{
    return all_items_top( []( item_pocket & pocket ) {
        return pocket.is_standard_type() && pocket.transparent();
    } );
}

std::vector<const item *> item_contents::all_known_contents() const
{
    return all_items_top( []( const item_pocket & pocket ) {
        return pocket.is_standard_type() && pocket.transparent();
    } );
}

std::vector<item *> item_contents::all_holstered_items()
{
    return all_items_top( []( item_pocket & pocket ) {
        return pocket.is_type( pocket_type::CONTAINER ) && pocket.is_holster();
    } );
}

std::vector<const item *> item_contents::all_holstered_items() const
{
    return all_items_top( []( const item_pocket & pocket ) {
        return pocket.is_type( pocket_type::CONTAINER ) && pocket.is_holster();
    } );
}

std::vector<item *> item_contents::all_ablative_armor()
{
    return all_items_top( []( item_pocket & pocket ) {
        return pocket.is_ablative();
    } );
}

std::vector<const item *> item_contents::all_ablative_armor() const
{
    return all_items_top( []( const item_pocket & pocket ) {
        return pocket.is_ablative();
    } );
}

item &item_contents::legacy_front()
{
    if( empty() ) {
        debugmsg( "naively asked for first content item and will get a nullptr" );
        return null_item_reference();
    }
    return *all_items_top().front();
}

const item &item_contents::legacy_front() const
{
    return const_cast<item_contents *>( this )->legacy_front();
}

bool item_contents::allows_speedloader( const itype_id &speedloader_id ) const
{
    ensure_pockets();
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::MAGAZINE ) ) {
            return pocket.allows_speedloader( speedloader_id );
        }
    }
    return false;
}

std::vector<const item *> item_contents::mods() const
{
    ensure_pockets();
    std::vector<const item *> mods;
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::MOD ) ) {
            for( const item *it : pocket.all_items_top() ) {
                mods.insert( mods.end(), it );
            }
        }
    }
    return mods;
}

void item_contents::update_modified_pockets(
    std::vector<const pocket_data *> mag_or_mag_wells,
    std::vector<const pocket_data *> container_pockets )
{
    ensure_pockets();
    for( auto pocket_iter = pockets.begin(); pocket_iter != pockets.end(); ) {
        item_pocket &pocket = *pocket_iter;
        if( pocket.is_type( pocket_type::CONTAINER ) ) {

            const pocket_data *current = pocket.get_pocket_data();
            bool found = false;
            // this loop is to make sure the pockets on the current item are already here from @container_pockets,
            // so we don't need to clear them (saving the favorite data)
            for( auto container_pocket = container_pockets.begin(); container_pocket != container_pockets.end();
               ) {
                // comparing pointers because each pocket is uniquely defined in json as its own.
                if( *container_pocket == current ) {
                    container_pocket = container_pockets.erase( container_pocket );
                    found = true;
                    // there will not be more than one pocket with the same pocket_data pointer, so exit early
                    break;
                } else {
                    ++container_pocket;
                }
            }

            if( !found ) {
                if( !pocket.empty() ) {
                    // in case the debugmsg wasn't clear, this should never happen
                    debugmsg( "Oops!  deleted some items when updating pockets that were added via toolmods" );
                }
                pocket_iter = pockets.erase( pocket_iter );
            } else {
                ++pocket_iter;
            }

        } else if( pocket.is_type( pocket_type::MAGAZINE ) ||
                   pocket.is_type( pocket_type::MAGAZINE_WELL ) ) {
            const pocket_data *current = pocket.get_pocket_data();
            bool matched = false;
            const auto found = std::find( mag_or_mag_wells.begin(), mag_or_mag_wells.end(), current );
            if( found != mag_or_mag_wells.end() ) {
                mag_or_mag_wells.erase( found );
                matched = true;
            }
            if( matched ) {
                ++pocket_iter;
            } else {
                if( !pocket.empty() ) {
                    debugmsg( "Oops!  deleted some items when updating pockets that were added via toolmods" );
                }
                pocket_iter = pockets.erase( pocket_iter );
            }
        } else {
            ++pocket_iter;
        }
    }

    // we've deleted all of the superfluous copies already, so time to add the new pockets
    // CDF: the MIGRATION pocket stays last
    const auto add_before_migration = [this]( const pocket_data * data ) {
        auto at = std::ranges::find_if( pockets, []( const item_pocket & p ) {
            return p.is_type( pocket_type::MIGRATION );
        } );
        pockets.emplace( at, owner, data );
    };
    for( const pocket_data *container_pocket : container_pockets ) {
        add_before_migration( container_pocket );
    }
    for( const pocket_data *mw : mag_or_mag_wells ) {
        add_before_migration( mw );
    }
    contents_changed();
}

std::set<itype_id> item_contents::magazine_compatible() const
{
    ensure_pockets();
    std::set<itype_id> ret;
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::MAGAZINE_WELL ) ) {
            for( const itype_id &id : pocket.item_type_restrictions() ) {
                if( item_is_blacklisted( id ) ) {
                    continue;
                }
                ret.emplace( id );
            }
        }
    }
    return ret;
}

itype_id item_contents::magazine_default() const
{
    ensure_pockets();
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::MAGAZINE_WELL ) ) {
            return pocket.magazine_default();
        }
    }
    return itype_id::NULL_ID();
}

std::vector<itype_id> item_contents::magazines_default() const
{
    ensure_pockets();
    std::vector<itype_id> result;
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::MAGAZINE_WELL ) ) {
            result.push_back( pocket.magazine_default() );
        }
    }
    return result;
}

units::length item_contents::item_length_modifier() const
{
    ensure_pockets();
    units::length total_length = 0_mm;
    for( const item_pocket &pocket : pockets ) {
        total_length = std::max( pocket.item_length_modifier(), total_length );
    }
    return total_length;
}

units::mass item_contents::total_container_weight_capacity( const bool unrestricted_pockets_only )
const
{
    ensure_pockets();
    units::mass total_weight = 0_gram;

    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_forbidden() ) {
            continue;
        }
        bool restriction_condition = pocket.is_type( pocket_type::CONTAINER ) &&
                                     !pocket.is_ablative() && pocket.weight_capacity() < pocket_data::max_weight_for_container;
        if( unrestricted_pockets_only ) {
            restriction_condition = restriction_condition && !pocket.is_restricted();
        }
        if( restriction_condition ) {
            total_weight += pocket.weight_capacity();
        }
    }
    return total_weight;
}

std::vector<const item_pocket *> item_contents::get_pockets( const
        std::function<bool( const item_pocket & )> &filter ) const
{
    ensure_pockets();
    std::vector<const item_pocket *> result;
    for( const item_pocket &pocket : pockets ) {
        if( filter( pocket ) ) {
            result.push_back( &pocket );
        }
    }
    return result;
}

std::vector<item_pocket *> item_contents::get_pockets( const
        std::function<bool( const item_pocket & )> &filter )
{
    ensure_pockets();
    std::vector<item_pocket *> result;
    for( item_pocket &pocket : pockets ) {
        if( filter( pocket ) ) {
            result.push_back( &pocket );
        }
    }
    return result;
}

std::vector<const item_pocket *> item_contents::get_container_pockets() const
{
    return get_pockets( []( item_pocket const & pocket ) {
        return pocket.is_type( pocket_type::CONTAINER );
    } );
}

std::vector<item_pocket *> item_contents::get_container_pockets()
{
    return get_pockets( []( item_pocket const & pocket ) {
        return pocket.is_type( pocket_type::CONTAINER );
    } );
}

std::vector<const item_pocket *> item_contents::get_standard_pockets() const
{
    return get_pockets( []( item_pocket const & pocket ) {
        return pocket.is_standard_type();
    } );
}

std::vector<item_pocket *> item_contents::get_standard_pockets()
{
    return get_pockets( []( item_pocket const & pocket ) {
        return pocket.is_standard_type();
    } );
}

std::vector<const item_pocket *> item_contents::get_ablative_pockets() const
{
    return get_pockets( []( item_pocket const & pocket ) {
        return pocket.is_ablative();
    } );
}

std::vector<item_pocket *> item_contents::get_ablative_pockets()
{
    return get_pockets( []( item_pocket const & pocket ) {
        return pocket.is_ablative();
    } );
}

std::vector<const item_pocket *> item_contents::get_container_and_mod_pockets() const
{
    return get_pockets( []( item_pocket const & pocket ) {
        return pocket.is_type( pocket_type::CONTAINER ) || pocket.is_type( pocket_type::MOD );
    } );
}

std::vector<item_pocket *> item_contents::get_container_and_mod_pockets()
{
    return get_pockets( []( item_pocket const & pocket ) {
        return pocket.is_type( pocket_type::CONTAINER ) || pocket.is_type( pocket_type::MOD );
    } );
}

std::vector<const item_pocket *> item_contents::get_all_reloadable_pockets() const
{
    ensure_pockets();
    std::vector<const item_pocket *> result;

    for( const item_pocket &pocket : pockets ) {
        if( ( pocket.is_type( pocket_type::CONTAINER ) && pocket.watertight() ) ||
            pocket.is_type( pocket_type::MAGAZINE ) ||
            pocket.is_type( pocket_type::MAGAZINE_WELL ) ) {
            result.push_back( &pocket );
        }
    }
    return result;
}

int item_contents::get_used_holsters() const
{
    ensure_pockets();
    int holsters = 0;
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::CONTAINER ) && pocket.holster_full() ) {
            holsters++;
        }
    }
    return holsters;
}

int item_contents::get_total_holsters() const
{
    ensure_pockets();
    int holsters = 0;
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::CONTAINER ) && pocket.is_holster() ) {
            holsters++;
        }
    }
    return holsters;
}

units::volume item_contents::get_used_holster_volume() const
{
    ensure_pockets();
    units::volume holster_volume = 0_ml;
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::CONTAINER ) && pocket.holster_full() ) {
            holster_volume += pocket.volume_capacity();
        }
    }
    return holster_volume;
}

units::volume item_contents::get_total_holster_volume() const
{
    ensure_pockets();
    units::volume holster_volume = 0_ml;
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::CONTAINER ) && pocket.is_holster() ) {
            holster_volume += pocket.volume_capacity();
        }
    }
    return holster_volume;
}

units::mass item_contents::get_used_holster_weight() const
{
    ensure_pockets();
    units::mass holster_weight = units::mass();
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::CONTAINER ) && pocket.is_holster() ) {
            holster_weight += pocket.contains_weight();
        }
    }
    return holster_weight;
}

units::mass item_contents::get_total_holster_weight() const
{
    ensure_pockets();
    units::mass holster_weight = units::mass();
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::CONTAINER ) && pocket.is_holster() ) {
            holster_weight += pocket.weight_capacity();
        }
    }
    return holster_weight;
}

units::volume item_contents::volume_capacity( const std::function<bool( const item_pocket & )> &
        include_pocket ) const
{
    units::volume total_vol = 0_ml;
    for( const item_pocket *pocket : get_pockets( include_pocket ) ) {
        total_vol += pocket->volume_capacity();
    }
    return total_vol;
}

units::volume item_contents::volume_capacity_recursive( const
        std::function<bool( const item_pocket & )> &
        include_pocket,
        const std::function<bool( const item_pocket & )> &check_pocket_tree,
        units::volume &out_volume_expansion ) const
{
    units::volume ret = 0_ml;
    for( const item_pocket *pocket : get_pockets( check_pocket_tree ) ) {
        units::volume pocket_capacity = pocket->volume_capacity();
        units::volume pocket_adds_capacity = 0_ml;
        if( include_pocket( *pocket ) ) {
            pocket_adds_capacity = pocket_capacity;
            // assume child pockets can't be larger than their parent pocket
            ret += pocket_adds_capacity;
        } else {
            units::volume nested_capacity = 0_ml;
            units::volume nested_expansion = 0_ml;
            for( const auto *it : pocket->all_items_top() ) {
                nested_capacity += it->get_volume_capacity_recursive( include_pocket, check_pocket_tree,
                                   nested_expansion );
            }
            units::volume rigid_child_capacity = nested_capacity - nested_expansion;
            units::volume pocket_adds_capacity = std::min( pocket_capacity,
                                                 nested_expansion ); //the pocket can't be used directly, but it's children can expand into it

            ret += pocket_adds_capacity + rigid_child_capacity;
        }

        if( !pocket->rigid() ) {
            out_volume_expansion += pocket_adds_capacity;
        }
    }
    return ret;
}

units::volume item_contents::biggest_pocket_capacity() const
{
    ensure_pockets();
    units::volume max_vol = 0_ml;
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::CONTAINER ) ) {
            max_vol = std::max( max_vol, pocket.volume_capacity() );
        }
    }
    return max_vol;
}

units::volume item_contents::remaining_volume( const std::function<bool( const item_pocket & )> &
        include_pocket ) const
{
    units::volume total_vol = 0_ml;
    for( const item_pocket *pocket : get_pockets( include_pocket ) ) {
        total_vol += pocket->remaining_volume();
    }
    return total_vol;
}

units::volume item_contents::remaining_volume_recursive( const
        std::function<bool( const item_pocket & )> &
        include_pocket,
        const std::function<bool( const item_pocket & )> &check_pocket_tree,
        units::volume &out_volume_expansion ) const
{
    units::volume ret = 0_ml;
    for( const item_pocket *pocket : get_pockets( check_pocket_tree ) ) {
        units::volume pocket_remaining_space = pocket->remaining_volume();
        units::volume nested_remaining_space = 0_ml;
        units::volume nested_expansion = 0_ml;
        for( const auto *it : pocket->all_items_top() ) {
            nested_remaining_space += it->get_remaining_volume_recursive( include_pocket, check_pocket_tree,
                                      nested_expansion );
        }
        units::volume nested_void_space = nested_remaining_space -
                                          nested_expansion; // free space 'trapped' in rigid pockets
        units::volume this_pocket_space = std::max(
                                              include_pocket( *pocket ) ? pocket_remaining_space : 0_ml, //the pocket counts directly, or
                                              std::min( pocket_remaining_space, nested_expansion ) //the pocket's children can expand into it
                                          );

        ret += this_pocket_space + nested_void_space;
        if( !pocket->rigid() ) {
            out_volume_expansion += this_pocket_space;
        }
    }
    return ret;
}

units::volume item_contents::contents_volume( const std::function<bool( const item_pocket & )>
        &include_pocket ) const
{
    units::volume total_vol = 0_ml;
    for( const item_pocket *pocket : get_pockets( include_pocket ) ) {
        total_vol += pocket->contents_volume();
    }
    return total_vol;
}

units::mass item_contents::remaining_container_capacity_weight( const bool
        unrestricted_pockets_only ) const
{
    ensure_pockets();
    units::mass total_weight = 0_gram;
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_forbidden() ) {
            continue;
        }
        bool restriction_condition = pocket.is_type( pocket_type::CONTAINER );
        if( unrestricted_pockets_only ) {
            restriction_condition = restriction_condition && !pocket.is_restricted();
        }
        if( restriction_condition ) {
            total_weight += pocket.remaining_weight();
        }
    }
    return total_weight;
}

units::mass item_contents::total_contained_weight( const bool unrestricted_pockets_only ) const
{
    ensure_pockets();
    units::mass total_weight = 0_gram;
    for( const item_pocket &pocket : pockets ) {
        bool restriction_condition = pocket.is_type( pocket_type::CONTAINER );
        if( unrestricted_pockets_only ) {
            restriction_condition = restriction_condition && !pocket.is_restricted();
        }
        if( restriction_condition ) {
            total_weight += pocket.contains_weight();
        }
    }
    return total_weight;
}

int item_contents::remaining_capacity_for_liquid( const item &liquid ) const
{
    ensure_pockets();
    int charges_of_liquid = 0;
    for( const item_pocket &pocket : pockets ) {
        if( !pocket.is_type( pocket_type::CONTAINER ) ) {
            continue;
        }
        charges_of_liquid += pocket.remaining_capacity_for_item( liquid );
    }
    return charges_of_liquid;
}

float item_contents::relative_encumbrance() const
{
    ensure_pockets();
    units::volume nonrigid_max_volume;
    units::volume nonrigid_volume;
    for( const item_pocket &pocket : pockets ) {
        if( !pocket.is_type( pocket_type::CONTAINER ) ) {
            continue;
        }
        if( pocket.rigid() ) {
            continue;
        }
        // need to modify by pockets volume encumbrance modifier since some pockets may have less effect than others
        float modifier = pocket.get_pocket_data()->volume_encumber_modifier;
        nonrigid_volume += std::max( 0_ml,
                                     ( pocket.contents_volume() - pocket.magazine_well() ) ) * modifier;
        nonrigid_max_volume += ( pocket.volume_capacity() - pocket.magazine_well() ) * modifier;
    }
    if( nonrigid_volume > nonrigid_max_volume ) {
        // volume exceeds capacity and will spill until 1 or lower if picked up, so assume 1
        return 1;
    }
    if( nonrigid_max_volume == 0_ml ) {
        return 0;
    }
    return nonrigid_volume * 1.0 / nonrigid_max_volume;
}

bool item_contents::all_pockets_rigid() const
{
    ensure_pockets();
    for( const item_pocket &pocket : pockets ) {
        if( !pocket.is_type( pocket_type::CONTAINER ) ) {
            continue;
        }
        if( !pocket.rigid() ) {
            return false;
        }
    }
    return true;
}

bool item_contents::container_type_pockets_empty() const
{
    ensure_pockets();
    for( const item_pocket &pocket : pockets ) {
        if( !pocket.is_type( pocket_type::CONTAINER ) ) {
            continue;
        }
        if( !pocket.empty() ) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// J5: DDA additional pockets (MOLLE), item_contents.cpp:2380-2500. CDF: T1/T2, the pouch's
// pockets are rebuilt from its pocket data and its items are moved, not copied.
// ---------------------------------------------------------------------------------------------

size_t item_contents::pouch_pocket_count( const item &pouch )
{
    return pouch.contents.get_container_pockets().size();
}

size_t item_contents::added_pocket_count() const
{
    size_t count = 0;
    for( const item *shell : additional_pockets ) {
        count += pouch_pocket_count( *shell );
    }
    return count;
}

std::list<item_pocket>::iterator item_contents::end_of_real_pockets()
{
    auto end_it = pockets.end();
    if( !pockets.empty() && pockets.back().is_type( pocket_type::MIGRATION ) ) {
        --end_it;
    }
    return end_it;
}

std::list<item_pocket>::const_iterator item_contents::end_of_real_pockets() const
{
    auto end_it = pockets.end();
    if( !pockets.empty() && pockets.back().is_type( pocket_type::MIGRATION ) ) {
        --end_it;
    }
    return end_it;
}

std::list<item_pocket>::iterator item_contents::first_pocket_of_pouch( int index )
{
    // the added pockets are the last ones before the migration pocket
    size_t after = 0;
    const std::vector<item *> &shells = additional_pockets.as_vector();
    for( size_t i = static_cast<size_t>( index ); i < shells.size(); ++i ) {
        after += pouch_pocket_count( *shells[i] );
    }
    auto it = end_of_real_pockets();
    for( size_t i = 0; i < after && it != pockets.begin(); ++i ) {
        --it;
    }
    return it;
}

bool item_contents::has_additional_pockets() const
{
    return !additional_pockets.empty();
}

std::vector<const item *> item_contents::get_added_pockets() const
{
    const std::vector<item *> &shells = additional_pockets.as_vector();
    return std::vector<const item *>( shells.begin(), shells.end() );
}

std::vector<item *> item_contents::get_added_pockets_mutable()
{
    return additional_pockets.as_vector();
}

void item_contents::add_pocket( detached_ptr<item> &&pouch_item )
{
    if( !pouch_item ) {
        return;
    }
    ensure_pockets();
    item &pouch = *pouch_item;
    units::volume total_nonrigid_volume = 0_ml;
    units::volume effective_nonrigid_volume = 0_ml;
    // need to insert before the end since the final pocket is the migration pocket
    const auto at = end_of_real_pockets();
    for( item_pocket *source : pouch.contents.get_container_pockets() ) {
        item_pocket &added = *pockets.emplace( at, owner, source->get_pocket_data() );
        // these pockets should fall back to using the item name as a description
        added.name_as_description = true;
        // CDF: pockets are not copyable, so the items move across
        for( detached_ptr<item> &moved : source->items().clear() ) {
            added.items().push_back( std::move( moved ) );
        }
        added.settings = source->settings;
        if( source->sealed() ) {
            added.seal();
        }
        total_nonrigid_volume += source->volume_capacity();
        effective_nonrigid_volume += ( source->volume_capacity() - source->magazine_well() ) *
                                     source->get_pocket_data()->volume_encumber_modifier;
    }
    additional_pockets_volume += total_nonrigid_volume;
    additional_pockets_effective_volume += effective_nonrigid_volume;
    additional_pockets_space_used += pouch.get_pocket_size();
    pouch.on_contents_changed();
    additional_pockets.push_back( std::move( pouch_item ) );
    contents_changed();
}

void item_contents::add_pocket( const item &pouch_item )
{
    add_pocket( item::spawn( pouch_item ) );
}

detached_ptr<item> item_contents::remove_pocket( int index )
{
    ensure_pockets();
    if( index < 0 || index >= static_cast<int>( additional_pockets.size() ) ) {
        return detached_ptr<item>();
    }
    item &pouch = *additional_pockets.as_vector()[index];
    // find the pockets to remove from the item
    auto it = first_pocket_of_pouch( index );
    units::volume total_nonrigid_volume = 0_ml;
    units::volume effective_nonrigid_volume = 0_ml;
    for( item_pocket *target : pouch.contents.get_container_pockets() ) {
        if( it == pockets.end() ) {
            debugmsg( "MOLLE pouch %s has fewer pockets on %s than it should", pouch.typeId().str(),
                      owner != nullptr ? owner->typeId().str() : "an item" );
            break;
        }
        total_nonrigid_volume += target->volume_capacity();
        effective_nonrigid_volume += ( target->volume_capacity() - target->magazine_well() ) *
                                     target->get_pocket_data()->volume_encumber_modifier;
        // move items from the consolidated pockets back to the item that will be returned
        for( detached_ptr<item> &moved : it->items().clear() ) {
            target->items().push_back( std::move( moved ) );
        }
        target->settings = it->settings;
        if( it->sealed() ) {
            target->seal();
        }
        // finally remove the pocket
        it = pockets.erase( it );
    }
    additional_pockets_volume -= total_nonrigid_volume;
    additional_pockets_effective_volume -= effective_nonrigid_volume;
    additional_pockets_space_used -= pouch.get_pocket_size();

    detached_ptr<item> ret = additional_pockets.remove( &pouch );
    if( ret ) {
        ret->on_contents_changed();
    }
    contents_changed();
    return ret;
}

void item_contents::clear_added_pockets()
{
    while( !additional_pockets.empty() ) {
        // dropped on purpose: whoever calls this replaces the whole contents
        remove_pocket( static_cast<int>( additional_pockets.size() ) - 1 );
    }
}

const item_pocket *item_contents::get_added_pocket( int index ) const
{
    if( index < 0 || index >= static_cast<int>( additional_pockets.size() ) ) {
        return nullptr;
    }
    ensure_pockets();
    return &*const_cast<item_contents *>( this )->first_pocket_of_pouch( index );
}

int item_contents::get_additional_pocket_encumbrance( float mod ) const
{
    return additional_pockets_effective_volume * mod / 250_ml;
}

int item_contents::get_additional_space_used() const
{
    return additional_pockets_space_used;
}

units::mass item_contents::get_additional_weight() const
{
    units::mass ret = 0_kilogram;
    for( const item *pouch : additional_pockets ) {
        ret += pouch->weight( false );
    }
    return ret;
}

units::volume item_contents::get_additional_volume() const
{
    units::volume ret = 0_ml;
    for( const item *pouch : additional_pockets ) {
        ret += pouch->volume();
    }
    return ret;
}

// Per DDA item_contents.cpp:2886 (item_contents::info): the per-pocket text of the item info.
// CDF: items attached through additional_pockets are pointers (T1).
void item_contents::info( std::vector<iteminfo> &info, const iteminfo_query *parts ) const
{
    int pocket_number = 1;
    std::vector<iteminfo> contents_info;
    std::vector<const item_pocket *> found_pockets; // CDF: item_pockets are not copyable
    std::map<int, int> pocket_num; // index, amount
    const bool describe_contents = parts->test( iteminfo_parts::DESCRIPTION_CONTENTS );
    for( const item_pocket &pocket : pockets ) {
        if( pocket.is_type( pocket_type::CONTAINER ) ) {
            bool found = false;
            int idx = 0;
            for( const item_pocket *found_pocket : found_pockets ) {
                if( *found_pocket == pocket ) {
                    found = true;
                    pocket_num[idx]++;
                    break;
                }
                idx++;
            }
            if( !found ) {
                found_pockets.push_back( &pocket );
                pocket_num[idx]++;
            }
            if( describe_contents ) {
                pocket.contents_info( contents_info, pocket_number++, pockets.size() != 1 );
            }
        }
    }
    if( parts->test( iteminfo_parts::DESCRIPTION_POCKETS ) ) {
        // start by saying what items are attached to this directly
        if( !additional_pockets.empty() ) {
            info.emplace_back( "CONTAINER", _( "<bold>This item incorporates</bold>:" ) );
            for( const item *it : additional_pockets ) {
                info.emplace_back( "CONTAINER", string_format( _( "%s." ),
                                   it->display_name() ) );
            }
            info.back().bNewLine = true;
        }

        // If multiple pockets and/or multiple kinds of pocket, show total capacity section
        units::volume capacity = volume_capacity();
        units::mass weight = total_container_weight_capacity();
        if( ( found_pockets.size() > 1 || pocket_num[0] > 1 ) && capacity > 0_ml && weight > 0_gram ) {
            insert_separation_line( info );
            info.emplace_back( "CONTAINER", _( "<bold>Total capacity</bold>:" ) );
            info.push_back( vol_to_info( "CONTAINER", _( "Volume: " ), capacity, 2, false ) );
            info.push_back( weight_to_info( "CONTAINER", _( "  Weight: " ), weight,
                                            2, false ) );
            info.back().bNewLine = true;
        }

        int idx = 0;
        int numbering = 1;
        for( const item_pocket *pocket : found_pockets ) {
            if( pocket->is_forbidden() ) {
                continue;
            }
            insert_separation_line( info );
            // If there are multiple similar pockets, show their capacity as a set
            if( pocket_num[idx] > 1 ) {
                std::vector<int> pocket_numbers( pocket_num[idx] );
                std::iota( pocket_numbers.begin(), pocket_numbers.end(), numbering );
                std::string pocket_numbers_enumeration = enumerate_as_string( pocket_numbers.begin(),
                pocket_numbers.end(), []( int n ) {
                    // std::to_string is not addressable, can't be passed directly
                    return std::to_string( n );
                } );
                info.emplace_back( "DESCRIPTION", string_format( _( "<bold>Pockets %s</bold>" ),
                                   pocket_numbers_enumeration ) );
            } else {
                // If this is the only pocket the item has, label it "Total capacity"
                // Otherwise, give it a generic "Pocket" heading (is one of several pockets)
                bool only_one_pocket = ( found_pockets.size() == 1 && pocket_num[0] == 1 );
                if( only_one_pocket ) {
                    info.emplace_back( "DESCRIPTION", _( "<bold>Total capacity</bold>:" ) );
                } else {
                    info.emplace_back( "DESCRIPTION", string_format( _( "<bold>Pocket %d</bold>" ), numbering ) );
                }
            }
            numbering += pocket_num[idx];
            idx++;
            pocket->general_info( info, idx, false );
        }
    }
    if( describe_contents ) {
        info.insert( info.end(), contents_info.begin(), contents_info.end() );
    }
}
