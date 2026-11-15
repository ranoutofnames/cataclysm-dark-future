#include "npctrade.h"

#include "avatar.h"
#include "carried_stacks.h"
#include "faction.h"
#include "game.h"
#include "item.h"
#include "item_category.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "map/map_selector.h"
#include "npc.h"
#include "player.h"
#include "skill.h"
#include "string_utils.h"
#include "trade_win.h"
#include "type_id.h"
#include "vehicle/vehicle_selector.h"
#include "visitable.h"

#include <algorithm>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

static const skill_id skill_barter( "barter" );
static const flag_id json_flag_NO_UNWIELD( "NO_UNWIELD" );

// CDF: G35/TR1. The contents of a traded row: DDA npctrade.cpp:127 items_with() lists every
// carried item, nested ones too, so a bag inside a backpack and the items inside that bag are
// rows of their own. CDF lists what sits in a container pocket of a listed row; a tool's or
// gun's magazine pockets stay inside the row that owns them (they are not CONTAINER pockets).
// The top-level rows come from carried_item_stacks(), which walks the carry/holster pockets of
// the worn items (character.cpp); every level below a row is walked here.
static auto is_row_pocket( const item_pocket &pocket ) -> bool
{
    return pocket.is_type( pocket_type::CONTAINER );
}

// DDA npctrade.cpp:184 (adjusted_price) and :213 (_trading_price) price a trade entry from
// item::price_no_contents and walk its pockets for the contents. CDF's list gives the items in a
// container pocket of a listed row a trade row of their own (TR1), so those are left out here:
// adding them would price the nested rows a second time when a container and its contents are
// sold together. DDA's faction price rules are not ported, and the barter adjustment stays in
// item_pricing::adjust_values().
static auto trading_price( const item &it ) -> float
{
    float price = it.price_no_contents( true );
    // DDA item.cpp:1900: item::price() does not descend into guns and magazines
    if( it.is_gun() || ( it.is_magazine() && !it.is_tool() ) ) {
        return price;
    }
    for( const item_pocket &pocket : it.contents.get_pockets() ) {
        if( is_row_pocket( pocket ) ) {
            continue;
        }
        for( const item *child : pocket.items() ) {
            price += trading_price( *child );
        }
    }
    return price;
}

// CDF: G35, a nested row that is selected on its own must leave the container before the
// container row moves, or it would detach from whatever the receiver now holds.
static auto carry_depth( const item &it ) -> int
{
    auto depth = 0;
    for( const item *parent = it.parent_item(); parent != nullptr; parent = parent->parent_item() ) {
        depth++;
    }
    return depth;
}

void npc_trading::transfer_items( std::vector<item_pricing> &stuff, Character &giver,
                                  Character &receiver, bool npc_gives )
{
    auto selected = std::vector<item_pricing *> {};
    for( item_pricing &ip : stuff ) {
        if( ip.selected ) {
            selected.push_back( &ip );
        }
    }
    std::ranges::stable_sort( selected, []( const item_pricing * a, const item_pricing * b ) {
        return carry_depth( *a->locs.front() ) > carry_depth( *b->locs.front() );
    } );

    // DDA npctrade.cpp:58-79 spills the contents of a container that the NPC would not buy (or,
    // when the NPC is the giver, would not sell) back to the giver. CDF's row list also lets a
    // container and its contents be marked separately (TR1), so a content that has a row of its
    // own but was not selected stays with the giver too: DDA's selector keeps such contents with
    // their container and cannot split them, while here the container row is priced without them.
    npc *wants_npc = nullptr;
    auto npc_is_giver = false;
    if( giver.is_npc() ) {
        wants_npc = giver.as_npc();
        npc_is_giver = true;
    } else if( receiver.is_npc() ) {
        wants_npc = receiver.as_npc();
    }
    const auto has_row = [&stuff]( const item * it ) {
        return std::ranges::any_of( stuff, [it]( const item_pricing & ip ) {
            return std::ranges::find( ip.locs, it ) != ip.locs.end();
        } );
    };
    const auto spill_contents = [&]( item &container ) {
        if( wants_npc == nullptr || !container.is_container() ) {
            return;
        }
        // copy: remove_top() invalidates item_contents::all_items_top()
        const auto contents = container.contents.all_items_top();
        for( item *it : contents ) {
            auto spill = has_row( it );
            if( !spill ) {
                const auto market_price = static_cast<int>( trading_price( *it ) );
                const auto val = wants_npc->value( *it, market_price );
                spill = npc_is_giver
                        ? !wants_npc->wants_to_sell( *it, val, market_price )
                        : !wants_npc->wants_to_buy( *it, val, market_price );
            }
            if( spill ) {
                giver.i_add( container.contents.remove_top( it ) );
            }
        }
    };

    std::ranges::for_each( selected, [&]( item_pricing * ip_ptr ) {
        auto &ip = *ip_ptr;
        auto &gift = *ip.locs.front();
        const auto charges = npc_gives ? ip.u_charges : ip.npc_charges;

        if( ip.charges ) {
            auto to_give = gift.split( charges );
            to_give->set_owner( receiver );

            receiver.i_add( std::move( to_give ) );
        } else {
            const auto count = npc_gives ? ip.u_has : ip.npc_has;
            gift.set_owner( receiver );
            std::ranges::for_each( std::views::take( ip.locs, count ), [&]( auto * it ) {
                spill_contents( *it );
                receiver.i_add( it->detach() );
            } );
        }
    } );
}

std::vector<item_pricing> npc_trading::init_selling( npc &np )
{
    auto result = std::vector<item_pricing> {};
    const auto add_stacks = [&]( const const_invslice & slice ) {
        std::ranges::for_each( slice, [&]( const auto & i ) {
            auto &it = *i->front();

            // DDA init_selling: npc::value( it ) values the whole item; give_equipment()
            // detaches it with its contents, so there are no separate rows to leave out.
            const auto price = it.price( true );
            auto val = np.value( it );
            if( np.wants_to_sell( it, val, price ) ) {
                result.emplace_back( *i, val, static_cast<int>( i->size() ) );
            }
        } );
    };
    for( const carried_stack &stack : carried_item_stacks( np ) ) {
        auto &it = *stack.front();
        const auto price = it.price( true );
        auto val = np.value( it );
        if( np.wants_to_sell( it, val, price ) ) {
            result.emplace_back( stack, val, static_cast<int>( stack.size() ) );
        }
    }
    add_stacks( np.shop_stock.const_slice() );

    if( np.will_exchange_items_freely() ) {
        std::ranges::for_each( np.wielded_items(), [&]( auto * weapon ) {
            if( !weapon->has_flag( json_flag_NO_UNWIELD ) ) {
                result.emplace_back( std::vector<item *> { weapon }, np.value( *weapon ), 0 );
            }
        } );
    }

    return result;
}

double npc_trading::net_price_adjustment( const Character &buyer, const Character &seller )
{
    // Adjust the prices based on your barter skill.
    // cap adjustment so nothing is ever sold below value
    ///\EFFECT_INT_NPC slightly increases bartering price changes, relative to your INT

    ///\EFFECT_BARTER_NPC increases bartering price changes, relative to your BARTER

    ///\EFFECT_INT slightly increases bartering price changes, relative to NPC INT

    ///\EFFECT_BARTER increases bartering price changes, relative to NPC BARTER
    double adjust = 0.05 * ( seller.int_cur - buyer.int_cur ) +
                    price_adjustment( seller.get_skill_level( skill_barter ) -
                                      buyer.get_skill_level( skill_barter ) );
    return( std::max( adjust, 1.0 ) );
}

template <typename T, typename Callback>
void buy_helper( T &src, Callback cb )
{
    src.visit_items( [&cb]( item * node ) {
        cb( node, 1 );

        return VisitResponse::SKIP;
    } );
}

std::vector<item_pricing> npc_trading::init_buying( Character &buyer, Character &seller,
        bool is_npc )
{
    std::vector<item_pricing> result;
    npc *np_p = dynamic_cast<npc *>( &buyer );
    if( is_npc ) {
        np_p = dynamic_cast<npc *>( &seller );
    }
    npc &np = *np_p;
    faction *fac = np.get_faction();

    double adjust = net_price_adjustment( buyer, seller );

    const auto check_item = [fac, adjust, is_npc, &np, &result,
                                  &seller]( const std::vector<item *> &locs, int count = 1,
    bool contents_are_rows = false ) {
        item *it_ptr = locs.front();
        if( it_ptr == nullptr || it_ptr->is_null() ) {
            return;
        }
        item &it = *it_ptr;

        // Don't sell items we don't own.
        if( !it.is_owned_by( seller ) ) {
            return;
        }

        // DDA npctrade.cpp:184: a row is priced from price_no_contents. Only the carried
        // stacks get a row per nested item (TR1), so only there must the row leave its
        // contents out; stock, map and vehicle rows move whole and keep item::price().
        const auto base_price = contents_are_rows ? trading_price( it ) : it.price( true );
        const auto market_price = static_cast<int>( base_price );
        int val = np.value( it, market_price );
        if( ( is_npc && np.wants_to_sell( it, val, market_price ) ) ||
            np.wants_to_buy( it, val, market_price ) ) {
            result.emplace_back( locs, val, count );
            result.back().adjust_values( adjust, fac );
        }
    };

    // DDA: npctrade.cpp:127 items_with() lists every carried item, nested ones too, so one item
    // can be sold out of a container while the container and the rest stay (G35). The outer
    // stacks keep carried_item_stacks() grouping; nested siblings group the same way. Every
    // level below a row is walked through its container pockets (a bag's contents are rows too).
    const auto add_nested = [&]( item &parent, auto &&self ) -> void {
        auto stacks = std::vector<carried_stack> {};
        for( item_pocket &pocket : parent.contents.get_pockets() ) {
            if( !is_row_pocket( pocket ) ) {
                continue;
            }
            for( item *child : pocket.items() ) {
                const auto found = std::ranges::find_if( stacks, [child]( const carried_stack & st ) {
                    return st.front()->stacks_with( *child );
                } );
                if( found != stacks.end() ) {
                    found->push_back( child );
                } else {
                    stacks.push_back( { child } );
                }
            }
        }
        for( const carried_stack &stack : stacks ) {
            check_item( stack, stack.size(), true );
            for( item *child : stack ) {
                self( *child, self );
            }
        }
    };

    for( const carried_stack &stack : carried_item_stacks( seller ) ) {
        check_item( stack, stack.size(), true );
        for( item *child : stack ) {
            add_nested( *child, add_nested );
        }
    }

    if( is_npc ) {
        const_invslice stock = np.shop_stock.const_slice();
        std::ranges::for_each( stock, [&]( const auto & i ) {
            check_item( *i, i->size() );
        } );
    }

    if( !seller.primary_weapon().has_flag( json_flag_NO_UNWIELD ) ) {
        check_item( {&seller.primary_weapon()}, 1 );
    }

    //nearby items owned by the NPC will only show up in
    //the trade window if the NPC is also a shopkeeper
    if( np.is_shopkeeper() ) {
        std::ranges::for_each( map_selector( seller.bub_pos(), PICKUP_RANGE ),
        [&]( auto & cursor ) {
            cursor.visit_items( [&check_item]( item * node ) {
                check_item( {node}, 1 );
                return VisitResponse::SKIP;
            } );
        } );
    }

    std::ranges::for_each( vehicle_selector( seller.bub_pos(), 1 ), [&]( auto & cursor ) {
        cursor.visit_items( [&check_item]( item * node ) {
            check_item( {node}, 1 );
            return VisitResponse::SKIP;
        } );
    } );

    const auto cmp = []( const item_pricing & a, const item_pricing & b ) {
        // Sort items by category first, then name.
        return localized_compare( std::make_pair( a.locs.front()->get_category(),
                                  a.locs.front()->display_name() ),
                                  std::make_pair( b.locs.front()->get_category(), b.locs.front()->display_name() ) );
    };

    std::ranges::sort( result, cmp );

    return result;
}

void item_pricing::set_values( int ip_count )
{
    const item *i_p = locs.front();
    is_container = i_p->is_container() || i_p->is_ammo_container();
    vol = i_p->volume();
    weight = i_p->weight();
    if( is_container || i_p->count() == 1 ) {
        count = ip_count;
    } else {
        charges = i_p->count();
        price /= charges;
        vol /= charges;
        weight /= charges;
    }
}

// Adjusts the pricing of an item, *unless* it is the currency of the
// faction we're trading with, as that should always be worth face value.
void item_pricing::adjust_values( const double adjust, const faction *fac )
{
    if( !fac || fac->currency != locs.front()->typeId() ) {
        price *= adjust;
    }
}

static const trait_id trait_DEBUG_STORAGE( "DEBUG_STORAGE" );

auto npc_trading::carry_room( const Character &who ) -> units::volume
{
    // CDF: DEBUG_STORAGE has no limit; a half-max keeps "+= vol" from overflowing
    if( who.has_trait( trait_DEBUG_STORAGE ) ) {
        return units::volume_max / 2;
    }
    return who.carried_capacity() - who.carried_volume();
}

auto npc_trading::trade_pane_volume( const Character &who,
                                     units::volume net_volume_in ) -> pane_volume
{
    pane_volume ret;
    ret.max = who.carried_capacity();
    ret.used = who.carried_volume() + net_volume_in;
    return ret;
}

auto npc_trading::setup_trade_state( trade_state &state, int cost, npc &np ) -> void
{
    // Populate the list of what the NPC is willing to buy, and the prices they pay
    // Note that the NPC's barter skill is factored into these prices.
    // TODO: Recalc item values every time a new item is selected
    // Trading is not linear - starving NPC may pay $100 for 3 jerky, but not $100000 for 300 jerky
    state.theirs = npc_trading::init_buying( g->u, np, true );
    state.yours = npc_trading::init_buying( np, g->u, false );

    if( np.will_exchange_items_freely() ) {
        state.your_balance = 0;
    } else {
        state.your_balance = np.op_of_u.owed - cost;
    }
}

auto npc_trading::npc_will_accept_trade( const trade_state &state, const npc &np ) -> bool
{
    return np.will_exchange_items_freely() || state.your_balance + np.max_credit_extended() > 0;
}

auto npc_trading::calc_npc_owes_you( const trade_state &state, const npc &np ) -> int
{
    // Friends don't hold debts against friends.
    if( np.will_exchange_items_freely() ) {
        return 0;
    }

    // If they're going to owe you more than before, and it's more than they're willing
    // to owe, then cap the amount owed at the present level or their willingness to owe
    // (whichever is bigger).
    //
    // When could they owe you more than max_willing_to_owe? It could be from quest rewards,
    // when they were less angry, or from when you were better friends.
    if( state.your_balance > np.op_of_u.owed && state.your_balance > np.max_willing_to_owe() ) {
        return std::max( np.op_of_u.owed, np.max_willing_to_owe() );
    }

    // Fair's fair. NPC will remember this debt (or credit they've extended)
    return state.your_balance;
}

auto npc_trading::update_npc_owed( const trade_state &state, npc &np ) -> void
{
    np.op_of_u.owed = calc_npc_owes_you( state, np );
}

// Oh my aching head
// op_of_u.owed is the positive when the NPC owes the player, and negative if the player owes the
// NPC
// cost is positive when the player owes the NPC money for a service to be performed
auto npc_trading::trade( npc &np, int cost, const std::string &deal ) -> bool
{
    // Only allow actual shopkeepers to refresh their inventory like this
    if( np.is_shopkeeper() ) {
        np.shop_restock();
    }
    //np.drop_items( np.weight_carried() - np.weight_capacity(),
    //               np.volume_carried() - np.volume_capacity() );
    // skip drop invalid inventory for merchants to avoid item dump on floor
    np.drop_invalid_inventory();

    auto state = trade_state{};
    npc_trading::setup_trade_state( state, cost, np );
    auto trade_win = trading_window( state );

    const auto traded = trade_win.perform_trade( np, deal );
    if( traded ) {
        auto practice = 0;

        npc_trading::transfer_items( state.yours, g->u, np, false );
        npc_trading::transfer_items( state.theirs, np, g->u, true );

        // NPCs will remember debts, to the limit that they'll extend credit or previous debts
        if( !np.will_exchange_items_freely() ) {
            npc_trading::update_npc_owed( state, np );
            g->u.practice( skill_barter, practice / 10000 );
        }
    }
    return traded;
}
