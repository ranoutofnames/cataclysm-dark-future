#include "game_inventory.h"

#include "avatar.h"
#include "avatar_action.h"
#include "avatar_functions.h"
#include "bionics.h"
#include "calendar.h"
#include "cata_utility.h"
#include "character.h"
#include "character_functions.h"
#include "character_martial_arts.h"
#include "color.h"
#include "crafting.h"
#include "cursesdef.h"
#include "damage.h"
#include "debug.h"
#include "enums.h"
#include "examine_item_menu.h"
#include "flag.h"
#include "activity_actor_definitions.h"
#include "game.h"
#include "input.h"
#include "inventory.h"
#include "inventory_ui.h"
#include "item.h"
#include "item_functions.h"
#include "messages.h"
#include "item_parent_checks.h"
#include "iteminfo_format_utils.h"
#include "item_category.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "pocket_type.h"
#include "map/map.h"
#include "material.h"
#include "npc.h"
#include "options.h"
#include "output.h"
#include "player.h"
#include "player_activity.h"
#include "point.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "reload/reload.h"
#include "reload/reload_selection.h"
#include "requirements.h"
#include "ret_val.h"
#include "salvage.h"
#include "skill.h"
#include "stomach.h"
#include "string_formatter.h"
#include "string_id.h"
#include "string_utils.h"
#include "translations.h"
#include "type_id.h"
#include "ui_manager.h"
#include "units.h"
#include "units_utility.h"
#include "value_ptr.h"

#include <algorithm>
#include <bitset>
#include <cmath>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

static const activity_id ACT_EAT_MENU( "ACT_EAT_MENU" );
static const activity_id ACT_CONSUME_FOOD_MENU( "ACT_CONSUME_FOOD_MENU" );
static const activity_id ACT_CONSUME_DRINK_MENU( "ACT_CONSUME_DRINK_MENU" );
static const activity_id ACT_CONSUME_MEDS_MENU( "ACT_CONSUME_MEDS_MENU" );

static const fault_id fault_bionic_nonsterile( "fault_bionic_nonsterile" );

static const skill_id skill_computer( "computer" );
static const skill_id skill_electronics( "electronics" );
static const skill_id skill_firstaid( "firstaid" );

static const quality_id qual_ANESTHESIA( "ANESTHESIA" );

static const bionic_id bio_painkiller( "bio_painkiller" );
static const bionic_id bio_taste_blocker( "bio_taste_blocker" );

static const trait_id trait_DEBUG_BIONICS( "DEBUG_BIONICS" );
static const trait_id trait_NOPAIN( "NOPAIN" );
static const trait_id trait_SAPROPHAGE( "SAPROPHAGE" );
static const trait_id trait_SAPROVORE( "SAPROVORE" );
static const trait_id trait_INFRESIST( "INFRESIST" );

static const std::string flag_LIQUIDCONT( "LIQUIDCONT" );
static const std::string iuse_TOGGLE_UPS_CHARGING( "TOGGLE_UPS_CHARGING" );

static const flag_id flag_BIONIC_NPC_USABLE( "BIONIC_NPC_USABLE" );

using item_filter = std::function<bool ( const item & )>;

class inventory_filter_preset : public inventory_selector_preset
{
    public:
        inventory_filter_preset( const item_filter &filter );

        bool is_shown( const item *location ) const override;

    private:
        item_filter filter;
};

namespace
{

class common_inventory_selector : public inventory_pick_selector
{
    public:
        explicit common_inventory_selector( player &p ) : inventory_pick_selector( p ) {
            ctxt.register_action( "unload_all",
                                  to_translation( "Unload every carried item that can be unloaded" ) );
            enable_view_mode_toggle();
        }

        auto clear_shortcuts() -> void {
            unload_all_selected = false;
        }

        [[nodiscard]] auto selected_unload_all() const -> bool {
            return unload_all_selected;
        }

    protected:
        auto handle_action( const std::string &action ) -> bool override {
            if( action == "ORGANIZE_MENU" ) {
                // Per DDA inventory_ui.cpp:3971
                u.organize_items_menu();
                return true;
            }
            if( action == "VIEW_CATEGORY_MODE" ) {
                toggle_categorize_contained();
                return true;
            }
            if( action != "unload_all" ) {
                return false;
            }
            unload_all_selected = true;
            return true;
        }

    private:
        bool unload_all_selected = false;
};

std::string good_bad_none( int value )
{
    if( value > 0 ) {
        return string_format( "<good>+%d</good>", value );
    } else if( value < 0 ) {
        return string_format( "<bad>%d</bad>", value );
    }
    return std::string();
}

int anesthetic_requirement( int mult )
{
    const requirement_data req_anesth = *requirement_id( "anesthetic" ) * mult;
    if( !req_anesth.get_tools().empty() && !req_anesth.get_tools().front().empty() ) {
        return req_anesth.get_tools().front().front().count;
    }

    return 0;
}

} // namespace

inventory_filter_preset::inventory_filter_preset( const item_filter &filter )
    : filter( filter )
{}

bool inventory_filter_preset::is_shown( const item *location ) const
{
    return filter( *location );
}

static item *inv_internal( player &u, const inventory_selector_preset &preset,
                           const std::string &title, int radius,
                           const std::string &none_message,
                           const std::string &hint = std::string(),
                           bool include_fake_items = false )
{
    inventory_pick_selector inv_s( u, preset );

    inv_s.set_title( title );
    inv_s.set_hint( hint );
    inv_s.set_display_stats( false );

    std::pair<size_t, size_t> init_pair;
    bool init_selection = false;
    std::string init_filter;
    bool has_init_filter = false;
    auto init_type = itype_id::NULL_ID();
    bool has_init_type = false;

    const std::vector<activity_id> consuming {
        ACT_EAT_MENU,
        ACT_CONSUME_FOOD_MENU,
        ACT_CONSUME_DRINK_MENU,
        ACT_CONSUME_MEDS_MENU };

    if( u.has_activity( consuming ) && u.activity->values.size() >= 2 ) {
        init_pair.first = u.activity->values[0];
        init_pair.second = u.activity->values[1];
        init_selection = true;
    }
    if( u.has_activity( consuming ) && !u.activity->str_values.empty() ) {
        init_filter = u.activity->str_values[0];
        has_init_filter = true;
    }
    if( u.has_activity( consuming ) && u.activity->str_values.size() >= 2 &&
        !u.activity->str_values[1].empty() ) {
        init_type = itype_id( u.activity->str_values[1] );
        has_init_type = true;
    }

    do {

        inv_s.clear_items();
        inv_s.add_character_items( u );
        inv_s.add_nearby_items( radius );

        if( include_fake_items ) {
            inv_s.add_fake_items( u );
        }
        if( has_init_filter ) {
            inv_s.set_filter( init_filter );
            has_init_filter = false;
        }
        // Set position after filter to keep cursor at the right position.
        // Stored types guard against restoring a stale row that now holds another item.
        if( init_selection && has_init_type ) {
            inv_s.restore_selection( init_pair, init_type );
        } else if( has_init_type ) {
            inv_s.select_item_type( init_type );
        }
        init_selection = false;
        has_init_type = false;

        if( inv_s.empty() ) {
            const std::string msg = none_message.empty()
                                    ? _( "You don't have the necessary item at hand." )
                                    : none_message;
            popup( msg, PF_GET_KEY );
            return nullptr;
        }

        item *location = inv_s.execute();

        if( inv_s.keep_open ) {
            inv_s.keep_open = false;
            continue;
        }

        if( u.has_activity( consuming ) ) {
            u.activity->values.clear();
            init_pair = inv_s.get_selection_position();
            u.activity->values.push_back( init_pair.first );
            u.activity->values.push_back( init_pair.second );
            u.activity->str_values.clear();
            u.activity->str_values.emplace_back( inv_s.get_filter() );
            u.activity->str_values.emplace_back( location != nullptr ? location->typeId().str() :
                                                 std::string() );
        }

        return location;

    } while( true );
}

void game_menus::inv::common( avatar &you )
{
    common_inventory_selector inv_s( you );

    inv_s.set_title( _( "Inventory" ) );
    inv_s.set_hint( string_format(
                        _( "Item hotkeys assigned: <color_light_gray>%d</color>/<color_light_gray>%d</color>" ),
                        you.allocated_invlets().count(), inv_chars.size() ) );

    bool started_action = false;
    do {
        inv_s.clear_items();
        inv_s.add_character_items( you );

        inv_s.clear_shortcuts();
        auto *location = inv_s.execute();

        if( inv_s.selected_unload_all() ) {
            avatar_action::unload_all( you );
            started_action = true;
            continue;
        }

        if( location == nullptr ) {
            if( inv_s.keep_open ) {
                inv_s.keep_open = false;
                continue;
            } else {
                break;
            }
        }

        const auto func_pos_x = []() {
            return 0;
        };
        const auto func_width = []() {
            return 50;
        };
        started_action = examine_item_menu::run( *location, func_pos_x, func_width,
                         examine_item_menu::menu_pos_t::right );
    } while( !started_action );
}

item *game_menus::inv::titled_filter_menu( const item_filter &filter, avatar &you,
        const std::string &title, const std::string &none_message, int radius )
{
    return inv_internal( you, inventory_filter_preset( filter ),
                         title, radius, none_message );
}

item *game_menus::inv::titled_menu( avatar &you, const std::string &title,
                                    const std::string &none_message )
{
    const std::string msg = none_message.empty() ? _( "Your inventory is empty." ) : none_message;
    return inv_internal( you, inventory_selector_preset(), title, -1, msg );
}

class armor_inventory_preset: public inventory_selector_preset
{
    public:
        armor_inventory_preset( player &pl, const std::string &color_in ) :
            p( pl ), color( color_in ) {

            append_cell( [ this ]( const item * loc ) {
                return get_number_string( loc->get_avg_encumber( p ) );
            }, _( "AVG ENCUMBRANCE" ) );

            append_cell( [ this ]( const item * loc ) {
                return loc->get_storage() > 0_ml ? string_format( "<%s>%s</color>", color,
                        format_volume( loc->get_storage() ) ) : std::string();
            }, _( "STORAGE" ) );

            append_cell( [ this ]( const item * loc ) {
                return string_format( "<%s>%d%%</color>", color, loc->get_avg_coverage() );
            }, _( "AVG COVERAGE" ) );

            append_cell( [ this ]( const item * loc ) {
                return get_number_string( loc->get_warmth() );
            }, _( "WARMTH" ) );

            append_cell( [ this ]( const item * loc ) {
                return get_number_string( loc->bash_resist() );
            }, _( "BASH" ) );

            append_cell( [ this ]( const item * loc ) {
                return get_number_string( loc->cut_resist() );
            }, _( "CUT" ) );

            append_cell( [ this ]( const item * loc ) {
                return get_number_string( loc->bullet_resist() );
            }, _( "BULLET" ) );

            append_cell( [ this ]( const item * loc ) {
                return get_number_string( loc->acid_resist() );
            }, _( "ACID" ) );

            append_cell( [ this ]( const item * loc ) {
                return get_number_string( loc->fire_resist() );
            }, _( "FIRE" ) );

            append_cell( [ this ]( const item * loc ) {
                return get_number_string( loc->get_env_resist() );
            }, _( "ENV" ) );
        }

    protected:
        player &p;
    private:
        std::string get_number_string( int number ) const {
            return number ? string_format( "<%s>%d</color>", color, number ) : std::string();
        }

        const std::string color;
};

class wear_inventory_preset: public armor_inventory_preset
{
    public:
        wear_inventory_preset( player &p, const std::string &color ) :
            armor_inventory_preset( p, color )
        {}

        bool is_shown( const item *loc ) const override {
            return loc->is_armor() && !p.is_worn( *loc );
        }

        std::string get_denial( const item *loc ) const override {
            const auto ret = p.can_wear( *loc );

            if( !ret.success() ) {
                return trim_punctuation_marks( ret.str() );
            }

            return std::string();
        }
};

item *game_menus::inv::wear( player &p )
{
    return inv_internal( p, wear_inventory_preset( p, "color_yellow" ), _( "Wear item" ), 1,
                         _( "You have nothing to wear." ) );
}

class take_off_inventory_preset: public armor_inventory_preset
{
    public:
        take_off_inventory_preset( player &p, const std::string &color ) :
            armor_inventory_preset( p, color )
        {}

        bool is_shown( const item *loc ) const override {
            return loc->is_armor() && p.is_worn( *loc );
        }

        std::string get_denial( const item *loc ) const override {
            const ret_val<bool> ret = p.can_takeoff( *loc );

            if( !ret.success() ) {
                return trim_punctuation_marks( ret.str() );
            }

            return std::string();
        }
};

item *game_menus::inv::take_off( avatar &you )
{
    return inv_internal( you, take_off_inventory_preset( you, "color_red" ), _( "Take off item" ), 1,
                         _( "You aren't wearing anything." ) );
}

item *game::inv_map_splice( const item_filter &filter, const std::string &title, int radius,
                            const std::string &none_message )
{
    return inv_internal( u, inventory_filter_preset( filter ),
                         title, radius, none_message );
}

item *game_menus::inv::container_for( avatar &you, const item &liquid, int radius )
{
    const auto filter = [ &liquid ]( const item & location ) {
        // Reject containers that already contain a different liquid (different set_vars)
        if( location.is_container() && !location.is_container_empty() ) {
            const item &cont_liq = location.get_contained();

            // Compare set_vars – if they don't match, skip this container. used for DNA comparison
            if( cont_liq.get_var( "specimen_sample" ) != liquid.get_var( "specimen_sample" ) ) {
                return false;
            }
        }

        if( location.where() == item_location_type::character ) {
            Character *character = g->critter_at<Character>( location.abs_pos() );
            if( character == nullptr ) {
                debugmsg( "Invalid location supplied to the liquid filter: no character found." );
                return false;
            }
            return location.get_remaining_capacity_for_liquid( liquid, *character ) > 0;
        }

        const bool allow_buckets = location.where() == item_location_type::map;
        return location.get_remaining_capacity_for_liquid( liquid, allow_buckets ) > 0;
    };

    return inv_internal( you, inventory_filter_preset( filter ),
                         string_format( _( "Container for %s" ), liquid.display_name( liquid.charges ) ), radius,
                         string_format( _( "You don't have a suitable container for carrying %s." ),
                                        liquid.tname() ) );
}

class disassemble_inventory_preset : public pickup_inventory_preset
{
    public:
        disassemble_inventory_preset( const player &p, const temp_crafting_inventory &inv ) :
            pickup_inventory_preset( p ), p( p ), inv( inv ) {

            check_components = true;

            append_cell( [ this ]( const item * loc ) {
                const auto &req = get_recipe( loc ).disassembly_requirements();
                if( req.is_empty() ) {
                    return std::string();
                }
                const item *i = loc;
                const auto components = i->get_uncraft_components();
                return enumerate_as_string( components.begin(), components.end(),
                []( const decltype( components )::value_type & comps ) {
                    return comps.to_string();
                } );
            }, _( "YIELD" ) );

            append_cell( [ this ]( const item * loc ) {
                return to_string_clipped( time_duration::from_turns( get_recipe( loc ).time / 100 ) );
            }, _( "TIME" ) );
        }

        bool is_shown( const item *loc ) const override {
            return get_recipe( loc );
        }

        std::string get_denial( const item *loc ) const override {
            const ret_val<bool> ret = crafting::can_disassemble( p, *loc, inv );
            if( !ret.success() ) {
                return ret.str();
            }
            return pickup_inventory_preset::get_denial( loc );
        }

    protected:
        const recipe &get_recipe( const item *loc ) const {
            return recipe_dictionary::get_uncraft( loc->typeId() );
        }

    private:
        const player &p;
        const temp_crafting_inventory &inv;
};

item *game_menus::inv::disassemble( player &p )
{
    return inv_internal( p, disassemble_inventory_preset( p, p.crafting_inventory() ),
                         _( "Disassemble item" ), 1,
                         _( "You don't have any items you could disassemble." ) );
}

class comestible_inventory_preset : public inventory_selector_preset
{
    public:
        comestible_inventory_preset( const player &p ) : p( p ) {

            append_cell( [ &p, this ]( const item * loc ) {
                const nutrients nutr = p.compute_effective_nutrients( get_consumable_item( loc ) );
                return good_bad_none( nutr.kcal );
            }, _( "CALORIES" ) );

            append_cell( [ this ]( const item * loc ) {
                return good_bad_none( get_edible_comestible( loc ).quench );
            }, _( "QUENCH" ) );

            append_cell( [ &p, this ]( const item * loc ) {
                const int consume_fun = p.fun_for( get_consumable_item( loc ) ).first;
                if( consume_fun < 0 && p.has_active_bionic( bio_taste_blocker ) &&
                    p.get_power_level() > units::from_kilojoule( -consume_fun ) ) {
                    return string_format( "<color_light_gray>[%d]</color>", consume_fun );
                } else {
                    return good_bad_none( consume_fun );
                }
            }, _( "JOY" ) );

            append_cell( [ this ]( const item * loc ) {
                const time_duration spoils = get_edible_comestible( loc ).spoils;
                if( spoils > 0_turns ) {
                    return to_string_clipped( spoils );
                }
                //~ Used for permafood shelf life in the Eat menu
                return std::string( _( "indefinite" ) );
            }, _( "SHELF LIFE" ) );

            append_cell( [ this ]( const item * loc ) {
                const item &it = get_consumable_item( loc );

                int converted_volume_scale = 0;
                const int charges = std::max( it.charges, 1 );
                const double converted_volume = round_up( convert_volume( it.volume().value() / charges,
                                                &converted_volume_scale ), 2 );

                //~ Eat menu Volume: <num><unit>
                return string_format( _( "%.2f%s" ), converted_volume, volume_units_abbr() );
            }, _( "VOLUME" ) );

            append_cell( [this]( const item * loc ) {
                if( g->u.can_estimate_rot() ) {
                    const islot_comestible item = get_edible_comestible( loc );
                    if( item.spoils > 0_turns ) {
                        return get_freshness( loc );
                    }
                    return std::string( "---" );
                }
                return std::string();
            }, _( "FRESHNESS" ) );

            append_cell( [ this ]( const item * loc ) {
                if( g->u.can_estimate_rot() ) {
                    const islot_comestible item = get_edible_comestible( loc );
                    if( item.spoils > 0_turns ) {
                        if( !get_consumable_item( loc ).rotten() ) {
                            return get_time_left_rounded( loc );
                        }
                    }
                    return std::string( "---" );
                }
                return std::string();
            }, _( "SPOILS IN" ) );

            append_cell( [ this, &p ]( const item * loc ) {
                std::string cbm_name;

                switch( p.get_cbm_rechargeable_with( get_consumable_item( loc ) ) ) {
                    case rechargeable_cbm::none:
                        break;
                    case rechargeable_cbm::reactor:
                        cbm_name = _( "Reactor" );
                        break;
                    case rechargeable_cbm::furnace:
                        cbm_name = _( "Furnace" );
                        break;
                    case rechargeable_cbm::other:
                        std::vector<bionic_id> bids = p.get_bionic_fueled_with( get_consumable_item( loc ) );
                        if( !bids.empty() ) {
                            bionic_id bid = p.get_most_efficient_bionic( bids );
                            cbm_name = bid->name.translated();
                        }
                        break;
                }

                if( !cbm_name.empty() ) {
                    return string_format( "<color_cyan>%s</color>", cbm_name );
                }

                return std::string();
            }, _( "CBM" ) );

            append_cell( [ this, &p ]( const item * loc ) {
                return good_bad_none( p.get_acquirable_energy( get_consumable_item( loc ) ) );
            }, _( "ENERGY (kJ)" ) );
        }

        bool is_shown( const item *loc ) const override {
            // If an item was inserted into a non-container, we can't eat it.
            // For example, we couldn't eat an item mod made of meat
            return p.can_consume( *loc ) &&
                   ( loc->where() != item_location_type::container || loc->parent_item()->is_container() );
        }

        std::string get_denial( const item *loc ) const override {
            const item &med = !( *loc ).is_container_empty() && ( *loc ).get_contained().is_medication() &&
                              ( *loc ).get_contained().type->has_use() ? ( *loc ).get_contained() : *loc;

            if( loc->made_of( LIQUID ) && !g->m.has_flag( flag_LIQUIDCONT, loc->bub_pos() ) ) {
                return _( "Can't drink spilt liquids" );
            }

            if( med.is_medication() && !p.can_use_heal_item( med ) ) {
                return _( "Your biology is not compatible with that item." );
            }

            const auto &it = get_consumable_item( loc );
            const auto res = p.can_eat( it );
            const auto cbm = p.get_cbm_rechargeable_with( it );

            if( !res.success() && cbm == rechargeable_cbm::none ) {
                return res.str();
            } else if( cbm == rechargeable_cbm::other && ( p.get_fuel_capacity( it.typeId() ) <= 0 ) ) {
                return string_format( _( "No space to store more %s" ), it.tname() );
            }

            return inventory_selector_preset::get_denial( loc );
        }

        bool sort_compare( const inventory_entry &lhs, const inventory_entry &rhs ) const override {
            time_duration time_a = get_time_left( lhs.any_item() );
            time_duration time_b = get_time_left( rhs.any_item() );
            int order_a = get_order( lhs.any_item(), time_a );
            int order_b = get_order( rhs.any_item(), time_b );

            return order_a < order_b
                   || ( order_a == order_b && time_a < time_b )
                   || ( order_a == order_b && time_a == time_b &&
                        inventory_selector_preset::sort_compare( lhs, rhs ) );
        }

    protected:
        int get_order( const item *loc, const time_duration &time ) const {
            if( time > 0_turns && !( loc->type->container && loc->type->container->preserves ) ) {
                return 0;
            } else if( get_consumable_item( loc ).rotten() ) {
                if( p.has_trait( trait_SAPROPHAGE ) || p.has_trait( trait_SAPROVORE ) ) {
                    return 1;
                } else {
                    return 4;
                }
            } else if( time == 0_turns ) {
                return 3;
            } else {
                return 2;
            }
        }

        // WARNING: this can return consumables which are not necessarily possessing
        // the comestible type. please dereference responsibly.
        const item &get_consumable_item( const item *loc ) const {
            return p.get_consumable_from( const_cast<item &>( *loc ) );
        }

        const islot_comestible &get_edible_comestible( const item *loc ) const {
            return get_edible_comestible( get_consumable_item( loc ) );
        }

        const islot_comestible &get_edible_comestible( const item &it ) const {
            if( it.is_comestible() && p.can_eat( it ).success() ) {
                // Ok since can_eat() returns false if is_craft() is true
                return *it.type->comestible;
            }
            static const islot_comestible dummy {};
            return dummy;
        }

        time_duration get_time_left( const item *loc ) const {
            time_duration time_left = 0_turns;
            const time_duration shelf_life = get_edible_comestible( loc ).spoils;
            if( shelf_life > 0_turns ) {
                const item &it = get_consumable_item( loc );
                const double relative_rot = it.get_relative_rot();
                time_left = shelf_life - shelf_life * relative_rot;

                // Correct for an estimate that exceeds shelf life -- this happens especially with
                // fresh items.
                if( time_left > shelf_life ) {
                    time_left = shelf_life;
                }
            }

            return time_left;
        }

        std::string get_time_left_rounded( const item *loc ) const {
            const item &it = get_consumable_item( loc );
            if( it.is_going_bad() ) {
                return _( "soon!" );
            }

            time_duration time_left = get_time_left( loc );
            return to_string_approx( time_left );
        }

        std::string get_freshness( const item *loc ) {
            const item &it = get_consumable_item( loc );
            const double rot_progress = it.get_relative_rot();
            if( it.is_fresh() ) {
                return _( "fresh" );
            } else if( rot_progress < 0.3 ) {
                return _( "quite fresh" );
            } else if( rot_progress < 0.5 ) {
                return _( "near midlife" );
            } else if( rot_progress < 0.7 ) {
                return _( "past midlife" );
            } else if( rot_progress < 0.9 ) {
                return _( "getting older" );
            } else if( !it.rotten() ) {
                return _( "old" );
            } else {
                return _( "rotten" );
            }
        }

    private:
        const player &p;
};

static std::string get_consume_needs_hint( player &p )
{
    auto hint = std::string();
    auto desc = p.get_hunger_description();
    hint.append( string_format( "%s %s", _( "Food :" ), colorize( desc.first, desc.second ) ) );
    hint.append( string_format( " %s ", LINE_XOXO_S ) );
    desc = p.get_thirst_description();
    hint.append( string_format( "%s %s", _( "Drink:" ), colorize( desc.first, desc.second ) ) );
    hint.append( string_format( " %s ", LINE_XOXO_S ) );
    desc = p.get_pain_description();
    hint.append( string_format( "%s %s", _( "Pain :" ), colorize( desc.first, desc.second ) ) );
    hint.append( string_format( " %s ", LINE_XOXO_S ) );
    desc = p.get_fatigue_description();
    hint.append( string_format( "%s %s", _( "Rest :" ), colorize( desc.first, desc.second ) ) );
    return hint;
}

item *game_menus::inv::consume( player &p )
{
    if( !g->u.has_activity( ACT_EAT_MENU ) ) {
        g->u.assign_activity( ACT_EAT_MENU );
    }

    return inv_internal( p, comestible_inventory_preset( p ),
                         _( "Consume item" ), 1,
                         _( "You have nothing to consume." ),
                         get_consume_needs_hint( p ) );
}

class comestible_filtered_inventory_preset : public comestible_inventory_preset
{
    public:
        comestible_filtered_inventory_preset( const player &p, bool( *predicate )( const item &it ) ) :
            comestible_inventory_preset( p ), predicate( predicate ) {}

        bool is_shown( const item *loc ) const override {
            return comestible_inventory_preset::is_shown( loc ) &&
                   predicate( get_consumable_item( loc ) );
        }

    private:
        bool( *predicate )( const item &it );
};

item *game_menus::inv::consume_food( player &p )
{
    if( !g->u.has_activity( ACT_CONSUME_FOOD_MENU ) ) {
        g->u.assign_activity( ACT_CONSUME_FOOD_MENU );
    }

    return inv_internal( p, comestible_filtered_inventory_preset( p, []( const item & it ) {
        return ( it.is_comestible() && it.get_comestible()->comesttype == "FOOD" ) ||
               it.has_flag( flag_USE_EAT_VERB );
    } ),
    _( "Consume food" ), 1,
    _( "You have no food to consume." ),
    get_consume_needs_hint( p ) );
}

item *game_menus::inv::consume_drink( player &p )
{
    if( !g->u.has_activity( ACT_CONSUME_DRINK_MENU ) ) {
        g->u.assign_activity( ACT_CONSUME_DRINK_MENU );
    }

    return inv_internal( p, comestible_filtered_inventory_preset( p, []( const item & it ) {
        return it.is_comestible() && it.get_comestible()->comesttype == "DRINK" &&
               !it.has_flag( flag_USE_EAT_VERB );
    } ),
    _( "Consume drink" ), 1,
    _( "You have no drink to consume." ),
    get_consume_needs_hint( p ) );
}

item *game_menus::inv::consume_meds( player &p )
{
    if( !g->u.has_activity( ACT_CONSUME_MEDS_MENU ) ) {
        g->u.assign_activity( ACT_CONSUME_MEDS_MENU );
    }

    return inv_internal( p, comestible_filtered_inventory_preset( p, []( const item & it ) {
        return it.is_medication();
    } ),
    _( "Consume medication" ), 1,
    _( "You have no medication to consume." ),
    get_consume_needs_hint( p ) );
}

class activatable_inventory_preset : public pickup_inventory_preset
{
    public:
        activatable_inventory_preset( const player &p ) : pickup_inventory_preset( p ), p( p ) {
            if( get_option<bool>( "INV_USE_ACTION_NAMES" ) ) {
                append_cell( [ this ]( const item * loc ) {
                    const item &it = !( *loc ).is_container_empty() && ( *loc ).get_contained().is_medication() &&
                                     ( *loc ).get_contained().type->has_use() ? ( *loc ).get_contained() : *loc;
                    return string_format( "<color_light_green>%s</color>", get_action_name( it ) );
                }, _( "ACTION" ) );
            }
        }

        bool is_shown( const item *loc ) const override {
            if( !( *loc ).is_container_empty() && ( *loc ).get_contained().is_medication() &&
                ( *loc ).get_contained().type->has_use() ) {
                return true;
            }
            return loc->type->has_use();
        }

        std::string get_denial( const item *loc ) const override {
            const item &it = !( *loc ).is_container_empty() && ( *loc ).get_contained().is_medication() &&
                             ( *loc ).get_contained().type->has_use() ? ( *loc ).get_contained() : *loc;
            const auto &uses = it.type->use_methods;

            if( uses.size() == 1 ) {
                const auto ret = uses.begin()->second.can_call( p, it, false, p.bub_pos() );
                if( !ret.success() ) {
                    return trim_punctuation_marks( ret.str() );
                }
            }

            if( it.is_medication() && !p.can_use_heal_item( it ) && !it.is_craft() ) {
                return _( "Your biology is not compatible with that item." );
            }

            if( !p.has_enough_charges( it, false ) && !uses.contains( iuse_TOGGLE_UPS_CHARGING ) ) {
                return string_format(
                           vgettext( "Needs at least %d charge",
                                     "Needs at least %d charges", loc->ammo_required() ),
                           loc->ammo_required() );
            }

            if( !it.has_flag( flag_ALLOWS_REMOTE_USE ) && !it.has_flag( flag_TEMPORARY_ITEM ) ) {
                return pickup_inventory_preset::get_denial( loc );
            }

            return std::string();
        }

    protected:
        std::string get_action_name( const item &it ) const {
            const auto &uses = it.type->use_methods;

            if( uses.size() == 1 ) {
                return uses.begin()->second.get_name();
            } else if( uses.size() > 1 ) {
                return _( "…" );
            }

            return std::string();
        }

    private:
        const player &p;
};

item *game_menus::inv::use( avatar &you )
{
    return inv_internal( you, activatable_inventory_preset( you ),
                         _( "Use item" ), 1,
                         _( "You don't have any items you can use." ),
                         std::string(), true );
}

class gunmod_inventory_preset : public inventory_selector_preset
{
    public:
        gunmod_inventory_preset( const player &p, const item &gunmod ) : p( p ), gunmod( gunmod ) {
            append_cell( [ this ]( const item * loc ) {
                const auto odds = get_odds( loc );

                if( odds.first >= 100 ) {
                    return string_format( "<color_light_green>%s</color>", _( "always" ) );
                }

                return string_format( "<color_light_green>%d%%</color>", odds.first );
            }, _( "SUCCESS CHANCE" ) );

            append_cell( [ this ]( const item * loc ) {
                return good_bad_none( get_odds( loc ).second );
            }, _( "DAMAGE RISK" ) );
        }

        bool is_shown( const item *loc ) const override {
            return loc->is_gun() && !loc->is_gunmod();
        }

        std::string get_denial( const item *loc ) const override {
            const auto ret = loc->is_gunmod_compatible( gunmod );

            if( !ret.success() ) {
                return ret.str();
            }

            if( !p.meets_requirements( gunmod, &*loc ) ) {
                return string_format( _( "requires at least %s" ),
                                      p.enumerate_unmet_requirements( gunmod, &*loc ) );
            }

            if( get_odds( loc ).first <= 0 ) {
                return _( "is too difficult for you to modify" );
            }

            return std::string();
        }

        bool sort_compare( const inventory_entry &lhs, const inventory_entry &rhs ) const override {
            const auto a = get_odds( lhs.any_item() );
            const auto b = get_odds( rhs.any_item() );

            if( a.first > b.first || ( a.first == b.first && a.second < b.second ) ) {
                return true;
            }

            return inventory_selector_preset::sort_compare( lhs, rhs );
        }

    protected:
        /** @return Odds for successful installation (pair.first) and gunmod damage (pair.second) */
        std::pair<int, int> get_odds( const item *gun ) const {
            return avatar_funcs::gunmod_installation_odds( *p.as_avatar(), *gun, gunmod );
        }

    private:
        const player &p;
        const item &gunmod;
};

item *game_menus::inv::gun_to_modify( player &p, const item &gunmod )
{
    return inv_internal( p, gunmod_inventory_preset( p, gunmod ),
                         _( "Select gun to modify" ), -1,
                         _( "You don't have any guns to modify." ) );
}

class read_inventory_preset final: public inventory_selector_preset
{
    public:
        read_inventory_preset( const player &p ) : p( p ) {
            const std::string unknown = _( "<color_dark_gray>?</color>" );

            append_cell( [ this, &p ]( const item * loc ) -> std::string {
                if( loc->type->can_use( "MA_MANUAL" ) ) {
                    return _( "martial arts" );
                }
                const islot_book &book = get_book( loc );
                if( !book.skill ) {
                    return std::string();
                }

                const SkillLevel &skill = p.get_skill_level_object( book.skill );
                if( skill.level() < book.req ) {
                    //~ %1$s: book skill name, %3$d: book required skill level, %3$d: book skill level, %4$d: player skill level
                    return string_format( pgettext( "skill", "%1$s from %2$d to %3$d (%4$d)" ), book.skill->name(),
                                          book.req, book.level,
                                          skill.level() );
                }

                //~ %1$s: book skill name, %2$d: book skill level, %3$d: player skill level
                const auto result = string_format( pgettext( "skill", "%1$s to %2$d (%3$d)" ),
                                                   book.skill->name(), book.level, skill.level() );

                return skill.can_train() ? result : colorize( result, c_brown );
            }, _( "TRAINS (CURRENT)" ), unknown );

            append_cell( [ this ]( const item * loc ) -> std::string {
                const islot_book &book = get_book( loc );
                const int unlearned = book.recipes.size() - get_known_recipes( book );

                return unlearned > 0 ? std::to_string( unlearned ) : std::string();
            }, _( "RECIPES" ), unknown );
            append_cell( [ &p ]( const item * loc ) -> std::string {
                return good_bad_none( character_funcs::get_book_fun_for( p, *loc ) );
            }, _( "FUN" ), unknown );

            append_cell( [ this, &p, unknown ]( const item * loc ) -> std::string {
                std::vector<std::string> dummy;

                // This is terrible and needs to be removed asap when this entire file is refactored
                // to use the new avatar class
                const Character *reader = nullptr;
                if( const avatar *av = p.as_avatar() ) {
                    reader = av->get_book_reader( *loc, dummy );
                } else if( const npc *n = p.as_npc() ) {
                    reader = n;
                }
                if( reader == nullptr ) {
                    return unknown;
                }

                int time_to_read = 0;
                // HACK: Need to refactor this
                // after moving reading methods from `npc` and `avatar`.
                if( const npc *npc_reader = reader->as_npc() ) {
                    time_to_read = npc_reader->time_to_read( *loc, *reader );
                } else if( const avatar *av = reader->as_avatar() ) {
                    time_to_read = av->time_to_read( *loc, *reader );
                } else {
                    debugmsg( "Reader is not NPC or avatar" );
                    time_to_read = 1;
                }
                // Actual reading time (in turns). Can be penalized.
                const int actual_turns = time_to_read / to_moves<int>( 1_turns );
                const std::string duration = to_string_approx( time_duration::from_turns( actual_turns ), false );

                // Theoretical reading time (in turns) based on the reader speed. Free of penalties.
                const int normal_turns = get_book( loc ).time * reader->read_speed() / to_moves<int>( 1_turns );
                if( actual_turns > normal_turns ) { // Longer - complicated stuff.
                    return string_format( "<color_light_red>%s</color>", duration );
                }

                return duration; // Normal speed.
            }, _( "CHAPTER IN" ), unknown );
        }

        bool is_shown( const item *loc ) const override {
            return loc->is_book();
        }

        std::string get_denial( const item *loc ) const override {
            // This is terrible and needs to be removed asap when this entire file is refactored
            // to use the new avatar class
            const avatar *u = p.as_avatar();
            if( !u ) {
                return std::string();
            }

            std::vector<std::string> denials;
            if( u->get_book_reader( *loc, denials ) == nullptr && !denials.empty() &&
                !loc->type->can_use( "learn_spell" ) && u->has_identified( loc->typeId() ) ) {
                return std::move( denials.front() );
            }
            return std::string();
        }

        nc_color get_color( const inventory_entry &entry ) const override {
            if( !entry.is_item() ) {
                return inventory_selector_preset::get_color( entry );
            }
            return entry.any_item()->color_in_inventory( p );
        }

        /** Splits books into groups: Unknown, CanTrainSkill, CanNotTrainSkillAnymore, ForFun.
        * 1. Unknown sorted by default algorithm.
        * 2. CanTrainSkill grouped by skill and sorted by time to read
        *    because player probably wants to level up certain skill faster.
        * 3. CanNotTrainSkillAnymore grouped by skill.
        * 4. ForFun sorted to make most fun books first.
        */
        bool sort_compare( const inventory_entry &lhs, const inventory_entry &rhs ) const override {
            const bool base_sort = inventory_selector_preset::sort_compare( lhs, rhs );

            // Player doesn't really interested if NPC knows about book.
            if( p.is_avatar() ) {
                const bool known_a = is_known( lhs.any_item() );
                const bool known_b = is_known( rhs.any_item() );

                // If we don't know book, it should be first.
                // Since we don't know it's contents,
                // we don't apply our skill based sortings here.
                if( !known_a || !known_b ) {
                    return ( !known_a && !known_b ) ? base_sort : !known_a;
                }
            }

            struct localized_string {
                std::string s;
                bool operator==( const localized_string &other ) const {
                    return s == other.s;
                }
                bool operator<( const localized_string &other ) const {
                    return localized_compare( s, other.s );
                }
            };

            // Used to unify martial arts and skills.
            struct book_info {
                    bool can_teach = false;
                    bool can_still_learn = false;
                    bool is_learnable_already = true;
                    int time_to_levelup = 0;
                    int fun = 0;

                    book_info( const islot_book &book, const player &p ):
                        time_to_levelup( book.time ),
                        fun( book.fun ),
                        book( book ) {
                        if( book.martial_art ) {
                            can_teach = true;
                            can_still_learn = !p.martial_arts_data->has_martialart( book.martial_art );
                        }
                        if( book.skill ) {
                            const int skill_level = p.get_skill_level( book.skill );

                            can_teach = true;
                            can_still_learn = skill_level < book.level;
                            is_learnable_already = skill_level >= book.req;
                        }
                    }

                    localized_string get_localized_skill()const {
                        assert( can_teach );

                        if( book.martial_art ) {
                            return { _( "martial arts" ) };
                        }
                        return { book.skill->name() };
                    }

                private:
                    const islot_book &book;
            };

            const islot_book &book_a = get_book( lhs.any_item() );
            const islot_book &book_b = get_book( rhs.any_item() );

            const book_info info_a( book_a, p );
            const book_info info_b( book_b, p );

            if( !info_a.can_teach && !info_b.can_teach ) {
                return ( info_a.fun == info_b.fun ) ? base_sort : info_a.fun > info_b.fun;
            } else if( info_a.can_teach != info_b.can_teach ) {
                return info_a.can_teach;
            }

            if( info_a.can_still_learn != info_b.can_still_learn ) {
                return info_a.can_still_learn;
            }
            const bool can_still_learn = info_a.can_still_learn;

            const localized_string skill_a = info_a.get_localized_skill();
            const localized_string skill_b = info_b.get_localized_skill();
            if( can_still_learn ) {
                const auto a = std::make_tuple(
                                   skill_a,
                                   info_a.is_learnable_already ? 0 : 1,
                                   info_a.time_to_levelup
                               );
                const auto b = std::make_tuple(
                                   skill_b,
                                   info_b.is_learnable_already ? 0 : 1,
                                   info_b.time_to_levelup
                               );
                return ( a == b ) ? base_sort : ( a < b );
            }

            if( skill_a == skill_b ) {
                return base_sort;
            }
            return skill_a < skill_b;
        }

    private:
        const islot_book &get_book( const item *loc ) const {
            return *loc->type->book;
        }

        bool is_known( const item *loc ) const {
            // This is terrible and needs to be removed asap when this entire file is refactored
            // to use the new avatar class
            if( const avatar *u = dynamic_cast<const avatar *>( &p ) ) {
                return u->has_identified( loc->typeId() );
            }
            return false;
        }

        int get_known_recipes( const islot_book &book ) const {
            int res = 0;
            for( const auto &elem : book.recipes ) {
                if( p.knows_recipe( elem.recipe ) ) {
                    ++res; // If the player knows it, they recognize it even if it's not clearly stated.
                }
            }
            return res;
        }

        const player &p;
};

item *game_menus::inv::read( player &pl )
{
    const std::string none_msg = pl.is_player() ? _( "You have nothing to read." ) :
                                 string_format( _( "%s has nothing to read." ), pl.disp_name() );
    return inv_internal( pl, read_inventory_preset( pl ), _( "Read" ), 1, none_msg );
}

class steal_inventory_preset : public pickup_inventory_preset
{
    public:
        steal_inventory_preset( const avatar &p, const player &victim ) :
            pickup_inventory_preset( p ), victim( victim ) {}

        bool is_shown( const item *loc ) const override {
            return !victim.is_worn( *loc ) && &victim.primary_weapon() != &( *loc );
        }

    private:
        const player &victim;
};

item *game_menus::inv::steal( avatar &you, player &victim )
{
    return inv_internal( victim, steal_inventory_preset( you, victim ),
                         string_format( _( "Steal from %s" ), victim.name ), -1,
                         string_format( _( "%s's inventory is empty." ), victim.name ) );
}

class weapon_inventory_preset: public inventory_selector_preset
{
    public:
        weapon_inventory_preset( const player &p ) : p( p ) {
            append_cell( [ this ]( const item * loc ) {
                if( !loc->is_gun() ) {
                    return std::string();
                }

                const int total_damage = loc->gun_damage( true ).total_damage();

                if( loc->ammo_data() && loc->ammo_remaining() ) {
                    const int basic_damage = loc->gun_damage( false ).total_damage();
                    if( loc->ammo_data()->ammo->damage.damage_units.front().damage_multiplier != 1.0f ) {
                        const float ammo_mult =
                            loc->ammo_data()->ammo->damage.damage_units.front().damage_multiplier;

                        return string_format( "%s<color_light_gray>*</color>%s <color_light_gray>=</color> %s",
                                              get_damage_string( basic_damage, true ),
                                              get_damage_string( ammo_mult, true ),
                                              get_damage_string( total_damage, true )
                                            );
                    } else {
                        const int ammo_damage = loc->ammo_data()->ammo->damage.total_damage();

                        return string_format( "%s<color_light_gray>+</color>%s <color_light_gray>=</color> %s",
                                              get_damage_string( basic_damage, true ),
                                              get_damage_string( ammo_damage, true ),
                                              get_damage_string( total_damage, true )
                                            );
                    }
                } else {
                    return get_damage_string( total_damage );
                }
            }, pgettext( "Shot as damage", "SHOT" ) );

            append_cell( [ this ]( const item * loc ) {
                return get_damage_string( loc->damage_melee( DT_BASH ) );
            }, _( "BASH" ) );

            append_cell( [ this ]( const item * loc ) {
                return get_damage_string( loc->damage_melee( DT_CUT ) );
            }, _( "CUT" ) );

            append_cell( [ this ]( const item * loc ) {
                return get_damage_string( loc->damage_melee( DT_STAB ) );
            }, _( "STAB" ) );

            append_cell( [ this ]( const item * loc ) {
                if( deals_melee_damage( *loc ) ) {
                    return good_bad_none( loc->type->m_to_hit );
                }
                return std::string();
            }, _( "MELEE" ) );

            append_cell( [ this ]( const item * loc ) {
                if( deals_melee_damage( *loc ) ) {
                    return string_format( "<color_yellow>%d</color>", this->p.attack_cost( *loc ) );
                }
                return std::string();
            }, _( "MOVES" ) );
        }

        std::string get_denial( const item *loc ) const override {
            const auto ret = p.can_wield( *loc );

            if( !ret.success() ) {
                return trim_punctuation_marks( ret.str() );
            }

            return std::string();
        }

    private:
        bool deals_melee_damage( const item &it ) const {
            return it.damage_melee( DT_BASH ) || it.damage_melee( DT_CUT ) || it.damage_melee( DT_STAB );
        }

        std::string get_damage_string( float damage, bool display_zeroes = false ) const {
            return damage ||
                   display_zeroes ? string_format( "<color_yellow>%g</color>", damage ) : std::string();
        }

        const player &p;
};

item *game_menus::inv::wield( avatar &you )
{
    return inv_internal( you, weapon_inventory_preset( you ), _( "Wield item" ), 1,
                         _( "You have nothing to wield." ) );
}

namespace
{
/**
 * Preset for putting items inside a container (DDA inventory_ui.cpp:907 inventory_holster_preset).
 * CDF: items are plain pointers, so "is in the holster" tests walk item::parent_item().
 */
class insert_inventory_preset : public inventory_selector_preset
{
    public:
        insert_inventory_preset( item &holster, const Character &who ) : holster( holster ),
            who( who ) {
        }

        const item *get_holster() const override {
            return &holster;
        }

        bool is_shown( const item *contained ) const override {
            if( contained == &holster ) {
                return false;
            }
            if( is_ancestor( contained, holster ) || is_ancestor( &holster, *contained ) ) {
                return false;
            }
            if( !contained->is_container() && contained->made_of( phase_id::LIQUID ) &&
                !contained->is_frozen_liquid() ) {
                // spilt liquid cannot be picked up
                return false;
            }
            // only hide if it is in the toplevel of holster (to allow shuffling of items inside a bag)
            return contained->parent_item() != &holster;
        }

        std::string get_denial( const item *it ) const override {
            if( who.is_worn( *it ) ) {
                const ret_val<bool> ret = who.can_takeoff( *it );
                if( !ret.success() ) {
                    return ret.str();
                }
            }

            if( it->is_bucket_nonempty() ) {
                return "item would spill";
            }

            detached_ptr<item> item_copy = item::spawn( *it );
            item_copy->charges = 1;
            ret_val<void> ret = holster.can_contain( *item_copy, false, false, true, false,
                                it->parent_item() );
            if( !ret.success() ) {
                return !ret.str().empty() ? ret.str() : "item can't be stored there";
            }

            ret = parents_can_contain_recursive( holster, *item_copy );
            if( !ret.success() ) {
                return !ret.str().empty() ? ret.str() : "item can't be stored there";
            }

            return {};
        }

    private:
        static bool is_ancestor( const item *maybe_ancestor, const item &it ) {
            for( const item *parent = it.parent_item(); parent != nullptr;
                 parent = parent->parent_item() ) {
                if( parent == maybe_ancestor ) {
                    return true;
                }
            }
            return false;
        }

        /// The item that we are putting something into.
        item &holster;
        const Character &who;
};

bool valid_unload_container( const item &container )
{
    return
        // Item must be a container.
        container.is_container()
        // Item must be able to be unloaded
        && !container.has_flag( flag_NO_UNLOAD )
        // Container must contain at least one item
        && !container.empty_container()
        // Not all contents should not be liquid, gas or plasma
        && !container.contains_no_solids();
}
} // namespace

// Per DDA game_inventory.cpp:2064 (game_menus::inv::holster, multi-select insert menu).
// CDF: P3-UI4, the selector is UI3's inventory_insert_selector (DDA inventory_ui.h:1181-1192), the
// drop selector with the DDA stats header, and warn_liquid is false as in DDA game_inventory.cpp:2076.
static drop_locations choose_items_to_insert( item &holster )
{
    avatar &you = get_avatar();
    const std::string holster_name = holster.tname( 1, false );
    const use_function *use = holster.type->get_use( "holster" );
    const holster_actor *actor = use == nullptr ? nullptr : dynamic_cast<const holster_actor *>
                                 ( use->get_actor_ptr() );

    const std::string title = ( actor ? actor->holster_prompt.empty() : true )
                              ? string_format( _( "Put item into %s" ), holster.tname() )
                              : _( actor->holster_prompt );
    const std::string hint = string_format( _( "Choose an item to put into your %s" ),
                                            holster_name );

    const insert_inventory_preset holster_preset( holster, you );

    inventory_insert_selector insert_menu( you, holster_preset, _( "ITEMS TO INSERT" ), false );
    insert_menu.add_character_items( you );
    insert_menu.add_nearby_items( 1 );
    insert_menu.set_display_stats( true );

    insert_menu.set_title( title );
    insert_menu.set_hint( hint );

    return insert_menu.execute();
}

void game_menus::inv::insert_items( item &holster )
{
    avatar &you = get_avatar();
    if( holster.will_spill_if_unsealed()
        && holster.where() != item_location_type::map
        && !you.is_wielding( holster ) ) {

        you.add_msg_if_player( m_info, _( "The %s would spill unless it's on the ground or wielded." ),
                               holster.type_name() );
        return;
    }
    const drop_locations holstered_list = choose_items_to_insert( holster );

    if( !holstered_list.empty() ) {
        you.assign_activity( std::make_unique<player_activity>(
                                 std::make_unique<insert_item_activity_actor>( holster, holstered_list ) ) );
    }
}

// Per DDA game.cpp:6590 (game::insert_item( drop_locations & )): choose another container for
// the items that could not be inserted.
void game_menus::inv::insert_items_again( const drop_locations &targets )
{
    if( targets.empty() || !targets.front().loc ) {
        return;
    }
    avatar &you = get_avatar();
    const item *source_parent = targets.front().loc->parent_item();
    const std::string title = string_format( _( "%s: %s and %d items" ), _( "Insert item" ),
                              targets.front().loc->tname(), targets.size() - 1 );
    item *container = g->inv_map_splice( [&you, source_parent]( const item & it ) {
        if( source_parent == &it ) {
            return false;
        }
        return it.is_container() && !it.is_corpse() && examine_item_menu::rate_action_insert( you, it ) == examine_item_menu::hint_rating::good;
    }, title, 1, _( "You have no container to insert items." ) );

    if( container == nullptr ) {
        add_msg( _( "Never mind." ) );
        return;
    }

    you.assign_activity( std::make_unique<player_activity>(
                             std::make_unique<insert_item_activity_actor>( *container, targets, true ) ) );
}

// Per DDA game_inventory.cpp:2119 (game_menus::inv::unload_container).
drop_locations game_menus::inv::unload_container()
{
    avatar &you = get_avatar();
    const inventory_filter_preset unload_preset( valid_unload_container );

    inventory_drop_selector insert_menu( you, unload_preset );
    insert_menu.add_character_items( you );
    insert_menu.add_nearby_items( 1 );
    insert_menu.set_display_stats( false );

    insert_menu.set_title( _( "Select containers to unload" ) );

    drop_locations dropped;
    for( const drop_location &droplc : insert_menu.execute() ) {
        for( item *it : droplc.loc->all_items_top( pocket_type::CONTAINER, true ) ) {
            // no liquids and no items marked as favorite
            if( ( !it->made_of( phase_id::LIQUID ) || it->is_frozen_liquid() ) && !it->is_favorite ) {
                dropped.emplace_back( *it, it->count() );
            }
        }
    }
    return dropped;
}

// CDF: P3-UI4, test hooks (precedent: unload_selector_shows). The insert preset is in an anonymous
// namespace, so tests/ reach its is_shown and get_denial through these.
bool game_menus::inv::insert_preset_shows( item &holster, const Character &who, const item &it )
{
    return insert_inventory_preset( holster, who ).is_shown( &it );
}

std::string game_menus::inv::insert_preset_denial( item &holster, const Character &who,
        const item &it )
{
    return insert_inventory_preset( holster, who ).get_denial( &it );
}

// CDF: P3-5-4, DDA game_inventory.cpp:3124-3128 (unload_selector_preset::is_shown) as a function on
// items, for tests/. CDF's preset has no pocket-type member (DDA _pk_type = { CONTAINER, MOD }), so the
// pocket test is done here: an entry that lies inside another item is listed only when it lies in a
// CONTAINER or MOD pocket, which leaves out the magazine seated in a gun's well and the rounds in it.
bool game_menus::inv::unload_selector_shows( const item &it )
{
    const item *parent = it.parent_item();
    if( parent != nullptr ) {
        const item_pocket *pocket = parent->contents.pocket_of( &it );
        if( pocket == nullptr ||
            !( pocket->is_type( pocket_type::CONTAINER ) || pocket->is_type( pocket_type::MOD ) ) ) {
            return false;
        }
    }
    return !item_funcs::is_invisible_installed_gunmod( it ) &&
           examine_item_menu::rate_action_unload( get_avatar(), it ) ==
           examine_item_menu::hint_rating::good;
}

namespace
{
class unload_selector_preset : public inventory_selector_preset
{
    public:
        bool is_shown( const item *loc ) const override {
            return game_menus::inv::unload_selector_shows( *loc );
        }
};
} // namespace

// CDF: P3-5-4, DDA game_inventory.cpp:3134-3153.
unload_selection game_menus::inv::unload( player &you )
{
    const unload_selector_preset preset;
    unload_selector inv_s( you, preset );

    inv_s.set_title( _( "Unload item" ) );
    inv_s.set_display_stats( false );

    inv_s.clear_items();

    inv_s.add_character_items( you );
    inv_s.add_nearby_items( 1 );

    if( inv_s.empty() ) {
        popup( _( "You have nothing to unload." ), PF_GET_KEY );
        return { nullptr, false };
    }

    return inv_s.execute();
}

// Per DDA inventory_ui.cpp:2050 (inventory_selector::drag_drop_item): the "Select pocket" menu.
bool game_menus::inv::select_pocket_and_store( Character &you, item &source, item &dest )
{
    if( &source == &dest ) {
        return false;
    }
    std::vector<item_pocket *> pockets = dest.get_container_pockets();
    if( pockets.empty() ) {
        popup( _( "Destination has no pockets." ) );
        return false;
    }
    int base_move_cost = 0;
    for( const item *parent = source.parent_item(); parent != nullptr;
         parent = parent->parent_item() ) {
        base_move_cost += parent->obtain_cost( source );
    }
    uilist action_menu;
    action_menu.text = _( "Select pocket" );
    int index = 0;
    bool any_containable = false;
    for( item_pocket *pocket : pockets ) {
        const std::string pocket_text = string_format( _( "%s - %s/%s | %d moves" ),
                                        pocket->get_description().translated(),
                                        vol_to_info( "", "", pocket->contents_volume() ).sValue,
                                        vol_to_info( "", "", pocket->volume_capacity() ).sValue,
                                        pocket->moves() + base_move_cost );
        action_menu.addentry( index++, true, 0, pocket_text );
        const ret_val<item_pocket::contain_code> contain = pocket->can_contain( source );
        if( contain.success() ) {
            any_containable = true;
        } else {
            action_menu.entries.back().enabled = false;
        }
    }
    if( !any_containable ) {
        popup( string_format( _( "%s contains no pockets that can contain item." ),
                              dest.tname( 1, false ) ) );
        return false;
    }
    action_menu.query();
    if( action_menu.ret >= 0 && static_cast<size_t>( action_menu.ret ) < pockets.size() ) {
        const ret_val<item_pocket::contain_code> contain =
            pockets[action_menu.ret]->can_contain( source );
        if( contain.success() ) {
            // storage should mimick character inserting
            you.store( pockets[action_menu.ret], source, true, base_move_cost );
            return true;
        } else {
            popup( _( "Cannot put item in pocket: %s" ), contain.str() );
            return false;
        }
    }

    return false;
}

class saw_barrel_inventory_preset: public weapon_inventory_preset
{
    public:
        saw_barrel_inventory_preset( const player &p, const item &tool, const saw_barrel_actor &actor ) :
            weapon_inventory_preset( p ), p( p ), tool( tool ), actor( actor ) {
        }

        bool is_shown( const item *loc ) const override {
            return loc->is_gun();
        }

        std::string get_denial( const item *loc ) const override {
            const auto ret = actor.can_use_on( p, tool, *loc );

            if( !ret.success() ) {
                return trim_punctuation_marks( ret.str() );
            }

            return std::string();
        }

    private:
        const player &p;
        const item &tool;
        const saw_barrel_actor &actor;
};

class saw_stock_inventory_preset : public weapon_inventory_preset
{
    public:
        saw_stock_inventory_preset( const player &p, const item &tool, const saw_stock_actor &actor ) :
            weapon_inventory_preset( p ), p( p ), tool( tool ), actor( actor ) {
        }

        bool is_shown( const item *loc ) const override {
            return loc->is_gun();
        }

        std::string get_denial( const item *loc ) const override {
            const auto ret = actor.can_use_on( p, tool, *loc );

            if( !ret.success() ) {
                return trim_punctuation_marks( ret.str() );
            }

            return std::string();
        }

    private:
        const player &p;
        const item &tool;
        const saw_stock_actor &actor;
};

class attach_molle_inventory_preset : public inventory_selector_preset
{
    public:
        attach_molle_inventory_preset( const molle_attach_actor *actor, const item *vest ) :
            actor( actor ), vest( vest ) {
        }

        bool is_shown( const item *loc ) const override {
            return loc != vest && loc->can_attach_as_pocket();
        }

        std::string get_denial( const item *loc ) const override {
            if( actor->size - vest->contents.get_additional_space_used() < loc->get_pocket_size() ) {
                return "not enough space left on the vest.";
            }
            return std::string();
        }

    private:
        const molle_attach_actor *actor;
        const item *vest;
};

class salvage_inventory_preset : public pickup_inventory_preset
{
    public:
        salvage_inventory_preset( const player &p, const temp_crafting_inventory &inv ):
            pickup_inventory_preset( p ),
            qualities( salvage::get_quality_cache( inv ) ) {

            append_cell( []( const item * loc ) {
                auto components = salvage::salvage_results( *loc );
                return enumerate_as_string( components.begin(), components.end(),
                []( const decltype( components )::value_type & comps ) {
                    int c = std::floor( comps.second );
                    //%1$s: item name, % 2$d :  count
                    return string_format( "%1$d %2$s", c, comps.first->nname( c ) );
                } );
            }, _( "YIELD" ) );

            append_cell( []( const item * loc ) {
                return to_string_clipped( time_duration::from_turns( salvage::moves_to_salvage(
                                              *loc ) / 100 ) );
            }, _( "TIME" ) );
        }

        std::string get_denial( const item *loc ) const override {
            auto q_cache = qualities;
            const ret_val<bool> ret = salvage::try_salvage( *loc, q_cache );
            if( !ret.success() ) {
                return ret.str();
            }
            return pickup_inventory_preset::get_denial( loc );
        }

        bool is_shown( const item *loc ) const override {
            return loc->is_salvageable();
        }

    private:
        salvage::quality_cache qualities;
};

item *game_menus::inv::salvage( player &p )
{

    return inv_internal( p, salvage_inventory_preset( p, p.crafting_inventory() ),
                         _( "Salvage what?" ), 1,
                         _( "You have nothing to salvage." ) );
}

class repair_inventory_preset: public inventory_selector_preset
{
    public:
        repair_inventory_preset( const repair_item_actor *actor, const item *main_tool,
                                 player &character ) :
            actor( actor ), main_tool( main_tool ), character( character ) {
            append_cell( [ this, actor, &character ]( const item * loc ) {
                int comp_needed = actor->get_material_amt_needed( *loc, true );
                auto valid_entries = actor->get_valid_materials( *loc );

                const auto &crafting_inv = character.crafting_inventory();
                auto listed_components = std::set<itype_id> {};
                auto material_list = std::vector<std::string> {};
                std::ranges::for_each( valid_entries, [ this, &listed_components, &material_list, &crafting_inv,
                      comp_needed ]( const auto & entry ) {
                    const auto &component_id = entry.obj().repaired_with();
                    if( listed_components.contains( component_id ) ) {
                        return;
                    }
                    listed_components.emplace( component_id );
                    const auto num_comp = get_cached_component_count( crafting_inv, component_id );
                    if( num_comp > 0 ) {
                        material_list.emplace_back( colorize( string_format( _( "%s (%d)" ), item::nname( component_id ),
                                                              num_comp ), num_comp < comp_needed ? c_red : c_unset ) );
                    }
                } );

                auto ret = join( material_list, ", " );
                if( ret.empty() ) {
                    ret = _( "<color_red>NONE</color>" );
                }
                return ret;
            }, _( "MATERIALS AVAILABLE" ) );

            append_cell( [ this, actor ]( const item * loc ) {
                const auto amt = actor->get_material_amt_needed( *loc, true );
                return string_format( _( "%d" ), amt );
            }, _( "NEED" ) );

            append_cell( [ this ]( const item * loc ) {
                const auto chance = get_cached_repair_chance( *loc );
                return colorize( string_format( "%0.1f%%", 100.0f * chance.first ),
                                 chance.first == 0 ? c_yellow : ( chance.second == 0 ? c_light_green : c_unset ) );
            }, _( "SUCCESS" ) );

            append_cell( [ this ]( const item * loc ) {
                const auto chance = get_cached_repair_chance( *loc );
                return colorize( string_format( "%0.1f%%", 100.0f * chance.second ),
                                 chance.second > chance.first ? c_yellow : ( chance.second == 0 &&
                                         chance.first > 0 ? c_light_green : c_unset ) );
            }, _( "DMG" ) );
        }

        bool is_shown( const item *loc ) const override {
            return loc->made_of_any( actor->materials ) && ( !loc->count_by_charges() ||
                    loc->is_stackable() ) && ( loc->damage() > -1 ||
                                               ( loc->has_flag( flag_VARSIZE ) && !loc->has_flag( flag_FIT ) ) ) && !loc->count_by_charges() &&
                   !loc->is_firearm() && !loc->has_flag( flag_NO_REPAIR ) &&
                   &*loc != main_tool;
        }

    private:
        auto get_cached_component_count( const temp_crafting_inventory &crafting_inv,
                                         const itype_id &component_id ) const -> int {
            auto [ iter, inserted ] = component_count_cache.try_emplace( component_id, 0 );
            if( !inserted ) {
                return iter->second;
            }

            if( item::count_by_charges( component_id ) ) {
                if( crafting_inv.charges_of( component_id, 1 ) >= 1 ) {
                    iter->second = crafting_inv.charges_of( component_id );
                }
            } else if( crafting_inv.has_amount( component_id, 1, false, is_crafting_component ) ) {
                iter->second = crafting_inv.amount_of( component_id, false );
            }
            return iter->second;
        }

        auto get_cached_repair_chance( const item &loc ) const -> std::pair<float, float> {
            auto [ iter, inserted ] = chance_cache.try_emplace( &loc );
            if( inserted ) {
                const auto level = character.get_skill_level( actor->used_skill );
                const auto action_type = actor->default_action( loc, level );
                iter->second = actor->repair_chance( character, loc, action_type );
            }
            return iter->second;
        }

        const repair_item_actor *actor;
        const item *main_tool;
        player &character;
        mutable std::unordered_map<const item *, std::pair<float, float>> chance_cache;
        mutable std::unordered_map<itype_id, int> component_count_cache;
};

static auto get_repair_hint( const player &character, const repair_item_actor *actor,
                             const item *main_tool ) -> std::string
{
    auto hint = std::string{};
    hint.append( string_format( _( "Tool: <color_cyan>%s</color>" ), main_tool->display_name() ) );
    hint.append( " | " );
    hint.append( string_format( _( "Skill used: <color_cyan>%s (%d)</color>" ),
                                actor->used_skill.obj().name(),
                                character.get_skill_level( actor->used_skill ) ) );
    return hint;
}

item *game_menus::inv::repair( player &p, const repair_item_actor *actor,
                               const item *main_tool )
{
    return inv_internal( p, repair_inventory_preset( actor, main_tool, p ),
                         _( "Repair what?" ), 1,
                         string_format( _( "You have no items that could be repaired with a %s." ),
                                        main_tool->type_name( 1 ) ),
                         get_repair_hint( p, actor, main_tool ) );
}

item *game_menus::inv::saw_barrel( player &p, item &tool )
{
    const auto actor = dynamic_cast<const saw_barrel_actor *>
                       ( tool.type->get_use( "saw_barrel" )->get_actor_ptr() );

    if( !actor ) {
        debugmsg( "Tried to use a wrong item." );
        return nullptr;
    }

    return inv_internal( p, saw_barrel_inventory_preset( p, tool, *actor ),
                         _( "Saw barrel" ), 1,
                         _( "You don't have any guns." ),
                         string_format( _( "Choose a weapon to use your %s on" ),
                                        tool.tname( 1, false )
                                      )
                       );
}

item *game_menus::inv::molle_attach( player &p, item &tool )
{
    const molle_attach_actor *actor = dynamic_cast<const molle_attach_actor *>
                                      ( tool.type->get_use( "attach_molle" )->get_actor_ptr() );

    if( !actor ) {
        debugmsg( "Tried to use a wrong item." );
        return nullptr;
    }

    const int vacancies = actor->size - tool.contents.get_additional_space_used();

    return inv_internal( p, attach_molle_inventory_preset( actor, &tool ),
                         _( "Attach pouch" ), 1,
                         _( "You have no pouches that attach to MOLLE webbing." ),
                         string_format( "%s\n%s",
                                        string_format( _( "Choose a pouch to attach to your %s" ),
                                                tool.tname( 1, false ) ),
                                        string_format( vgettext( "There is space for %d small pouch",
                                                "There is space for %d small pouches", vacancies ), vacancies ) ) );
}

item *game_menus::inv::saw_stock( player &p, item &tool )
{
    const auto actor = dynamic_cast<const saw_stock_actor *>
                       ( tool.type->get_use( "saw_stock" )->get_actor_ptr() );

    if( !actor ) {
        debugmsg( "Tried to use a wrong item." );
        return &null_item_reference();
    }

    return inv_internal( p, saw_stock_inventory_preset( p, tool, *actor ),
                         _( "Saw stock" ), 1,
                         _( "You don't have any guns." ),
                         string_format( _( "Choose a weapon to use your %s on" ),
                                        tool.tname( 1, false )
                                      )
                       );
}

drop_locations game_menus::inv::multidrop( player &p )
{

    const inventory_filter_preset preset( [ &p ]( const item & location ) {
        const item &itm = location;
        if( p.is_wielding( itm ) ) {
            return p.can_unwield( itm ).success();
        } else if( p.is_wearing( itm ) ) {
            return p.can_takeoff( itm ).success();
        } else {
            return true;
        }
    } );

    inventory_drop_selector inv_s( p, preset );

    inv_s.add_character_items( p );
    inv_s.set_title( _( "Multidrop" ) );
    inv_s.set_hint( _( "To drop x items, type a number before selecting." ) );

    while( true ) {
        inv_s.clear_items();
        inv_s.add_character_items( p );

        if( inv_s.empty() ) {
            popup( std::string( _( "You have nothing to drop." ) ), PF_GET_KEY );
            return drop_locations();
        }

        drop_locations result = inv_s.execute();
        // an item has been favorited, reopen the UI
        if( inv_s.keep_open ) {
            continue;
        } else {
            return result;
        }
    }
}

std::vector<pickup::pick_drop_selection> game_menus::inv::pickup_from_tile( player &p,
        const tripoint_bub_ms &target )
{

    pickup_inventory_preset preset( p );
    inventory_pickup_selector inv_s( p, preset );

    inv_s.set_title( _( "Multipickup" ) );
    inv_s.set_hint( _( "To pickup x items, type a number before selecting." ) );

    while( true ) {
        inv_s.clear_items();
        inv_s.add_map_items( target );
        inv_s.add_vehicle_items( target );

        if( inv_s.empty() ) {
            popup( std::string( _( "You have nothing to pickup." ) ), PF_GET_KEY );
            return std::vector<pickup::pick_drop_selection>();
        }

        std::vector<pickup::pick_drop_selection> result = inv_s.execute();
        return result;
    }
}

std::vector<pickup::pick_drop_selection> game_menus::inv::pickup_nearby( player &p )
{

    pickup_inventory_preset preset( p );
    inventory_pickup_selector inv_s( p, preset );

    inv_s.set_title( _( "Multipickup" ) );
    inv_s.set_hint( _( "To pickup x items, type a number before selecting." ) );

    while( true ) {
        inv_s.clear_items();
        inv_s.add_nearby_items();

        if( inv_s.empty() ) {
            popup( std::string( _( "You have nothing to pickup." ) ), PF_GET_KEY );
            return std::vector<pickup::pick_drop_selection>();
        }

        std::vector<pickup::pick_drop_selection> result = inv_s.execute();
        return result;
    }
}


void game_menus::inv::compare( player &p, const std::optional<tripoint_rel_ms> &offset )
{

    inventory_compare_selector inv_s( p );

    inv_s.add_character_items( p );
    inv_s.set_title( _( "Compare" ) );
    inv_s.set_hint( _( "Select two items to compare them." ) );

    if( offset ) {
        inv_s.add_map_items( p.bub_pos() + *offset );
        inv_s.add_vehicle_items( p.bub_pos() + *offset );
    } else {
        inv_s.add_nearby_items();
    }

    if( inv_s.empty() ) {
        popup( std::string( _( "There are no items to compare." ) ), PF_GET_KEY );
        return;
    }

    do {
        const auto to_compare = inv_s.execute();

        if( to_compare.first == nullptr || to_compare.second == nullptr ) {
            break;
        }

        compare( *to_compare.first, *to_compare.second );
    } while( true );
}

void game_menus::inv::compare( const item &l, const item &r )
{
    std::string action;
    input_context ctxt;
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "UP" );
    ctxt.register_action( "DOWN" );
    ctxt.register_action( "PAGE_UP" );
    ctxt.register_action( "PAGE_DOWN" );

    std::vector<iteminfo> lhs_info = l.info();
    std::vector<iteminfo> rhs_info = r.info();
    std::string lhs_tname = l.tname();
    std::string rhs_tname = r.tname();
    std::string lhs_type_name = l.type_name();
    std::string rhs_type_name = r.type_name();

    int lhs_scroll_pos = 0;
    int rhs_scroll_pos = 0;

    item_info_data lhs_item_info( lhs_tname, lhs_type_name, lhs_info, rhs_info, rhs_scroll_pos );
    lhs_item_info.without_getch = true;

    item_info_data rhs_item_info( rhs_tname, rhs_type_name, rhs_info, lhs_info, lhs_scroll_pos );
    rhs_item_info.without_getch = true;

    catacurses::window w_lhs_item_info;
    catacurses::window w_rhs_item_info;
    ui_adaptor ui;
    ui.on_screen_resize( [&]( ui_adaptor & ui ) {
        const int half_width = TERMX / 2;
        const int height = TERMY;
        w_lhs_item_info = catacurses::newwin( height, half_width, point_zero );
        w_rhs_item_info = catacurses::newwin( height, half_width, point( half_width, 0 ) );
        ui.position( point_zero, point( half_width * 2, height ) );
    } );
    ui.mark_resize();

    ui.on_redraw( [&]( const ui_adaptor & ) {
        draw_item_info( w_lhs_item_info, lhs_item_info );
        draw_item_info( w_rhs_item_info, rhs_item_info );
    } );

    do {
        ui_manager::redraw();

        action = ctxt.handle_input();

        if( action == "UP" || action == "PAGE_UP" ) {
            lhs_scroll_pos--;
            rhs_scroll_pos--;
        } else if( action == "DOWN" || action == "PAGE_DOWN" ) {
            lhs_scroll_pos++;
            rhs_scroll_pos++;
        }

    } while( action != "QUIT" );
}

void game_menus::inv::reassign_letter( Character &who, item &it, int invlet )
{
    if( avatar *you = who.as_avatar() ) {
        you->reassign_item( it, invlet );
    }
}

void game_menus::inv::prompt_reassign_letter( Character &who, item &it )
{
    while( true ) {
        const int invlet = popup_getkey(
                               _( "Enter new letter.  Press SPACE to clear a manually assigned letter, ESCAPE to cancel." ) );

        if( invlet == KEY_ESCAPE ) {
            break;
        } else if( invlet == ' ' ) {
            reassign_letter( who, it, 0 );
            const std::string auto_setting = get_option<std::string>( "AUTO_INV_ASSIGN" );
            if( auto_setting == "enabled" || ( auto_setting == "favorites" && it.is_favorite ) ) {
                popup_getkey(
                    _( "Note: The Auto Inventory Letters setting might still reassign a letter to this item.\n"
                       "If this is undesired, you may wish to change the setting in Options." ) );
            }
            break;
        } else if( inv_chars.valid( invlet ) ) {
            reassign_letter( who, it, invlet );
            break;
        }
    }
}

void game_menus::inv::swap_letters( player &p )
{

    inventory_pick_selector inv_s( p );

    inv_s.add_character_items( p );
    inv_s.set_title( _( "Swap Inventory Letters" ) );
    inv_s.set_display_stats( false );

    if( inv_s.empty() ) {
        popup( std::string( _( "Your inventory is empty." ) ), PF_GET_KEY );
        return;
    }

    while( true ) {
        const std::string invlets = colorize_symbols( inv_chars.get_allowed_chars(),
        [ &p ]( const std::string::value_type & elem ) {
            if( g->u.invlet_is_assigned( elem ) ) {
                return c_yellow;
            } else if( p.invlet_to_item( elem ) != nullptr ) {
                return c_white;
            } else {
                return c_dark_gray;
            }
        } );

        inv_s.set_hint( invlets );

        auto loc = inv_s.execute();

        if( !loc ) {
            break;
        }

        prompt_reassign_letter( p, *loc );
    }
}

static item *autodoc_internal( player &u, player &patient,
                               const inventory_selector_preset &preset,
                               int radius, bool uninstall = false, bool surgeon = false )
{
    inventory_pick_selector inv_s( u, preset );
    std::string hint;

    if( !surgeon ) {//surgeon use their own anesthetic, player just need money
        if( patient.has_trait( trait_NOPAIN ) ) {
            hint = _( "<color_yellow>Patient has Deadened nerves.  Anesthesia unneeded.</color>" );
        } else if( patient.has_bionic( bio_painkiller ) ) {
            hint = _( "<color_yellow>Patient has Sensory Dulling CBM installed.  Anesthesia unneeded.</color>" );
        } else if( patient.has_trait( trait_DEBUG_BIONICS ) ) {
            hint = _( "<color_yellow>Bug-hunters don't need anesthetics to withstand pain.</color>" );
        } else {
            const temp_crafting_inventory &crafting_inv = u.crafting_inventory();
            std::vector<item *> a_filter = crafting_inv.items_with( []( const item & it ) {
                return it.has_quality( qual_ANESTHESIA );
            } );
            const int drug_count = std::accumulate( a_filter.begin(), a_filter.end(), 0,
            []( int sum, const item * it ) {
                return sum + it->ammo_remaining();
            } );
            hint = string_format( _( "<color_yellow>Available anesthetic: %i mL</color>" ), drug_count );
        }
    }

    std::vector<item *> install_programs = patient.crafting_inventory().items_with( [](
            const item & it ) -> bool { return it.has_flag( flag_BIONIC_INSTALLATION_DATA ); } );

    if( !install_programs.empty() ) {
        hint += string_format(
                    _( "\n<color_light_green>Found bionic installation data.  Affected CBMs are marked with an asterisk.</color>" ) );
    }

    const auto title = uninstall
                       ? _( "Bionic removal patient: %s" ) : _( "Bionic installation patient: %s" );
    inv_s.set_title( string_format( title, patient.get_name() ) );

    inv_s.set_hint( hint );
    inv_s.set_display_stats( false );

    do {

        inv_s.clear_items();
        inv_s.add_character_items( u );
        inv_s.add_nearby_items( radius );

        if( inv_s.empty() ) {
            popup( _( "You don't have any bionics to install." ), PF_GET_KEY );
            return nullptr;
        }

        item *location = inv_s.execute();

        if( inv_s.keep_open ) {
            inv_s.keep_open = false;
            continue;
        }

        return location;

    } while( true );
}

// Menu used by autodoc when installing a bionic
class bionic_install_preset: public inventory_selector_preset
{
    public:
        bionic_install_preset( player &pl, player &patient ) :
            p( pl ), pa( patient ) {
            append_cell( [ this ]( const item * loc ) {
                return get_failure_chance( loc );
            }, _( "COMPLICATION CHANCE" ) );

            append_cell( [ this ]( const item * loc ) {
                return get_operation_duration( loc );
            }, _( "OPERATION DURATION" ) );

            append_cell( [this]( const item * loc ) {
                return get_anesth_amount( loc );
            }, _( "ANESTHETIC REQUIRED" ) );
        }

        bool is_shown( const item *loc ) const override {
            return loc->is_bionic();
        }

        std::string get_denial( const item *loc ) const override {
            const item *it = loc;
            const itype *itemtype = it->type;
            const bionic_id &bid = itemtype->bionic->id;

            if( it->has_fault( fault_bionic_nonsterile ) && !pa.has_trait( trait_INFRESIST ) ) {
                // NOLINTNEXTLINE(cata-text-style): single space after the period for symmetry
                return _( "/!\\ CBM is not sterile. /!\\ Please use autoclave or other methods to sterilize." );
            } else if( !bid->has_flag( flag_MULTIINSTALL ) && pa.has_bionic( bid ) ) {
                return _( "CBM already installed" );
            } else if( !pa.can_install_cbm_on_bp( get_occupied_bodyparts( bid ) ) ) {
                return _( "CBM not compatible with patient's body." );
            } else if( bid->upgraded_bionic &&
                       !pa.has_bionic( bid->upgraded_bionic ) &&
                       it->is_upgrade() ) {
                return _( "No base version installed" );
            } else if( character_funcs::has_upgraded_bionic( pa, bid ) ) {
                return _( "Superior version installed" );
            } else if( pa.is_npc() && !bid->has_flag( flag_BIONIC_NPC_USABLE ) ) {
                return _( "CBM not usable by NPC's" );
            } else if( units::energy_max - pa.get_max_power_level() < bid->capacity ) {
                return _( "Max power capacity already reached" );
            } else if( !has_enough_anesthesia( itemtype, p, pa ) ) {
                const int weight = 7;
                const int duration = loc->type->bionic->difficulty * 2;
                return string_format( _( "%i mL" ), anesthetic_requirement( duration * weight ) );
            }

            return std::string();
        }

    protected:
        player &p;
        player &pa;

    private:
        // Returns a formatted string of how long the operation will take.
        std::string get_operation_duration( const item *loc ) {
            const int difficulty = loc->type->bionic->difficulty;
            // 20 minutes per bionic difficulty.
            return to_string( time_duration::from_minutes( difficulty * 20 ) );
        }

        // Failure chance for bionic install. Combines multiple other functions together.
        std::string get_failure_chance( const item *loc ) {

            const int difficulty = loc->type->bionic->difficulty;
            int chance_of_failure = 100;
            player &installer = p;

            std::vector<item *> install_programs = p.crafting_inventory().items_with( [loc](
                    const item & it ) -> bool { return it.typeId() == loc->type->bionic->installation_data; } );

            const bool has_install_program = !install_programs.empty();

            const int adjusted_skill = installer.bionics_adjusted_skill( skill_firstaid,
                                       skill_computer,
                                       skill_electronics,
                                       -1 );

            if( g->u.has_trait( trait_DEBUG_BIONICS ) ) {
                chance_of_failure = 0;
            } else {
                chance_of_failure = has_install_program ? 1 : 100 - bionic_manip_cos( adjusted_skill, difficulty );
            }

            return string_format( has_install_program ? _( "<color_white>*</color> %i%%" ) : _( "%i%%" ),
                                  chance_of_failure );
        }

        std::string get_anesth_amount( const item *loc ) {
            if( !cbm_needs_anesthesia( pa ) ) {
                return std::string( "-" );
            }
            const int weight = 7;
            const int duration = loc->type->bionic->difficulty * 2;
            return string_format( _( "%i mL" ), anesthetic_requirement( duration * weight ) );
        }
};

// Menu used by surgeon when installing a bionic
class bionic_install_surgeon_preset : public inventory_selector_preset
{
    public:
        bionic_install_surgeon_preset( player &pl, player &patient ) :
            p( pl ), pa( patient ) {
            append_cell( [this]( const item * loc ) {
                return get_failure_chance( loc );
            }, _( "FAILURE CHANCE" ) );

            append_cell( [this]( const item * loc ) {
                return get_operation_duration( loc );
            }, _( "OPERATION DURATION" ) );

            append_cell( [this]( const item * loc ) {
                return get_money_amount( loc );
            }, _( "PRICE" ) );
        }

        bool is_shown( const item *loc ) const override {
            return loc->is_bionic();
        }

        std::string get_denial( const item *loc ) const override {
            const item *it = loc;
            const itype *itemtype = it->type;
            const bionic_id &bid = itemtype->bionic->id;

            if( it->has_fault( fault_bionic_nonsterile ) ) {
                return _( "CBM is not sterile." );
            } else if( !bid->has_flag( flag_MULTIINSTALL ) && pa.has_bionic( bid ) ) {
                return _( "CBM is already installed." );
            } else if( bid->upgraded_bionic &&
                       !pa.has_bionic( bid->upgraded_bionic ) &&
                       it->is_upgrade() ) {
                return _( "No base version installed." );
            } else if( std::any_of( bid->available_upgrades.begin(),
                                    bid->available_upgrades.end(),
                                    std::bind( &player::has_bionic, &pa,
                                               std::placeholders::_1 ) ) ) {
                return _( "Superior version installed." );
            } else if( pa.is_npc() && !bid->has_flag( flag_BIONIC_NPC_USABLE ) ) {
                return _( "CBM is not compatible with patient." );
            }

            return std::string();
        }

    protected:
        player &p;
        player &pa;

    private:
        // Returns a formatted string of how long the operation will take.
        std::string get_operation_duration( const item *loc ) {
            const int difficulty = loc->type->bionic->difficulty;
            // 20 minutes per bionic difficulty.
            return to_string( time_duration::from_minutes( difficulty * 20 ) );
        }

        // Failure chance for bionic install. Combines multiple other functions together.
        std::string get_failure_chance( const item *loc ) {

            const int difficulty = loc->type->bionic->difficulty;
            int chance_of_failure = 100;
            player &installer = p;

            // Override player's skills with surgeon skill
            const int adjusted_skill = installer.bionics_adjusted_skill( skill_firstaid,
                                       skill_computer,
                                       skill_electronics,
                                       20 );

            if( g->u.has_trait( trait_DEBUG_BIONICS ) ) {
                chance_of_failure = 0;
            } else {
                chance_of_failure = 100 - bionic_manip_cos( adjusted_skill, difficulty );
            }

            return string_format( _( "%i%%" ), chance_of_failure );
        }

        std::string get_money_amount( const item *loc ) {
            return format_money( loc->price( true ) * 2 );
        }
};

item *game_menus::inv::install_bionic( player &p, player &patient, bool surgeon )
{
    if( surgeon ) {
        return autodoc_internal( p, patient, bionic_install_surgeon_preset( p, patient ), 5, false,
                                 surgeon );
    } else {
        return autodoc_internal( p, patient, bionic_install_preset( p, patient ), 5 );
    }

}
// Menu used by autodoc when uninstalling a bionic
class bionic_uninstall_preset : public inventory_selector_preset
{
    public:
        bionic_uninstall_preset( player &pl, player &patient ) :
            p( pl ), pa( patient ) {
            append_cell( [this]( const item * loc ) {
                return get_failure_chance( loc );
            }, _( "FAILURE CHANCE" ) );

            append_cell( [this]( const item * loc ) {
                return get_operation_duration( loc );
            }, _( "OPERATION DURATION" ) );

            append_cell( [this]( const item * loc ) {
                return get_anesth_amount( loc );
            }, _( "ANESTHETIC REQUIRED" ) );
        }

        bool is_shown( const item *loc ) const override {
            return loc->has_flag( flag_IN_CBM );
        }

        std::string get_denial( const item *loc ) const override {
            const itype *itemtype = loc->type;

            if( !has_enough_anesthesia( itemtype, p, pa ) ) {
                const int weight = 7;
                const int duration = loc->type->bionic->difficulty * 2;
                return string_format( _( "%i mL" ), anesthetic_requirement( duration * weight ) );
            }

            return std::string();
        }

    protected:
        player &p;
        player &pa;

    private:
        // Returns a formatted string of how long the operation will take.
        std::string get_operation_duration( const item *loc ) {
            const int difficulty = loc->type->bionic->difficulty;
            // 20 minutes per bionic difficulty.
            return to_string( time_duration::from_minutes( difficulty * 20 ) );
        }

        // Failure chance for bionic uninstall. Combines multiple other functions together.
        std::string get_failure_chance( const item *loc ) {

            // Uninstall difficulty gets a +2
            const int difficulty = loc->type->bionic->difficulty + 2;
            int chance_of_failure = 100;
            player &installer = p;

            const int adjusted_skill = installer.bionics_adjusted_skill( skill_firstaid,
                                       skill_computer,
                                       skill_electronics,
                                       -1 );

            if( g->u.has_trait( trait_DEBUG_BIONICS ) ) {
                chance_of_failure = 0;
            } else {
                chance_of_failure = 100 - bionic_manip_cos( adjusted_skill, difficulty );
            }

            return string_format( _( "%i%%" ), chance_of_failure );
        }

        std::string get_anesth_amount( const item *loc ) {
            if( !cbm_needs_anesthesia( pa ) ) {
                return std::string( "-" );
            }
            const int weight = 7;
            const int duration = loc->type->bionic->difficulty * 2;
            return string_format( _( "%i mL" ), anesthetic_requirement( duration * weight ) );
        }
};

item *game_menus::inv::uninstall_bionic( player &p, player &patient )
{
    return autodoc_internal( p, patient, bionic_uninstall_preset( p, patient ), 0, true );
}

// Menu used by autoclave when sterilizing a bionic
class bionic_sterilize_preset : public inventory_selector_preset
{
    public:
        bionic_sterilize_preset( player &pl ) :
            p( pl ) {
        }

        bool is_shown( const item *loc ) const override {
            return loc->has_fault( fault_bionic_nonsterile );
        }


    protected:
        player &p;
};

static item *autoclave_internal( player &u,
                                 const inventory_selector_preset &preset,
                                 int radius )
{
    inventory_pick_selector inv_s( u, preset );
    inv_s.set_title( _( "Sterilization" ) );
    inv_s.set_hint( _( "<color_yellow>Select a device to sterilize:</color>" ) );
    inv_s.set_display_stats( false );

    do {

        inv_s.clear_items();
        inv_s.add_character_items( u );
        inv_s.add_nearby_items( radius );

        if( inv_s.empty() ) {
            popup( _( "You don't have any devices to sterilize." ), PF_GET_KEY );
            return nullptr;
        }

        item *location = inv_s.execute();

        if( inv_s.keep_open ) {
            inv_s.keep_open = false;
            continue;
        }

        return location;

    } while( true );

}
item *game_menus::inv::sterilize_cbm( player &p )
{
    return autoclave_internal( p, bionic_sterilize_preset( p ), 6 );
}

// CDF: P3-5-3a, DDA game_inventory.cpp 3155-3286 (select_ammo_inventory_preset). Items are item
// pointers (D1): the cells take const item * or, where they need a mutable item (MOVES), the
// inventory_entry; `player &` and `item &` for Character & and the item_location target;
// describe_location( ch ) for item_location::describe( ch ). `_indent_entries = false` has no member
// here (the CDF column lists by category unless the selector toggles hierarchy) and
// `_collate_entries = true` is the collate_entries() override. is_shown is
// game_menus::inv::select_ammo_shows, so a test can reach it.
namespace
{
class select_ammo_inventory_preset : public inventory_selector_preset
{
    public:
        select_ammo_inventory_preset( player &you, item &target, bool empty ) : you( you ),
            target( target ), empty( empty ) {
            append_cell( [&you]( const item * loc ) {
                bool is_ammo_container = loc->is_ammo_container();
                Character &player_character = get_player_character();
                if( is_ammo_container || loc->is_container() ) {
                    if( is_ammo_container && you.is_worn( *loc ) ) {
                        return loc->type_name();
                    }
                    return string_format( _( "%s, %s" ), loc->type_name(),
                                          loc->describe_location( &player_character ) );
                }
                return loc->describe_location( &player_character );
            }, _( "LOCATION" ) );

            append_cell( [&you, host = &target]( const item * loc ) {
                const std::vector<reload_target> targets = get_possible_reload_targets( *host );
                const reload_target *match = find_matching_reload_target( targets, *loc );
                if( match == nullptr ) {
                    return std::string();
                }
                if( match->target == host ) {
                    return std::string();
                }
                return string_format( _( "%s, %s" ), match->target->type_name(),
                                      match->target->describe_location( &you ) );
            }, _( "DESTINATION" ) );

            append_cell( []( const inventory_entry & entry ) {
                if( entry.any_item()->is_ammo() ) {
                    return std::to_string( entry.chosen_count );
                }
                return std::string();
            }, _( "AMOUNT" ) );

            append_cell( [&you, host = &target]( const inventory_entry & entry ) {
                item_reload_option opt( &you, host, host, *entry.any_item(),
                                        item_reload_option::POCKET_FALLBACK );
                // propagate entry.chosen_count here somehow?
                return std::to_string( opt.moves() );
            }, _( "MOVES" ) );

            append_cell( []( const item * loc ) {
                const itype *ammo = loc->is_ammo_container() ? loc->first_ammo().ammo_data() :
                                    loc->ammo_data();
                if( ammo ) {
                    const damage_instance &dam = ammo->ammo->damage;
                    return std::to_string( static_cast<int>( dam.total_damage() ) );
                }
                return std::string();
            }, _( "DAMAGE" ) );

            append_cell( []( const item * loc ) {
                const itype *ammo = loc->is_ammo_container() ? loc->first_ammo().ammo_data() :
                                    loc->ammo_data();
                if( ammo ) {
                    const damage_instance &dam = ammo->ammo->damage;
                    return std::to_string( static_cast<int>( dam.empty() ? 0.0f : ( *dam.begin() ).res_pen ) );
                }
                return std::string();
            }, _( "PIERCE" ) );
        }

        bool collate_entries() const override {
            return true;
        }

        bool is_shown( const item *loc ) const override {
            return game_menus::inv::select_ammo_shows( target, *loc, empty );
        }

        // sort in order of move cost (ascending), then remaining ammo (descending) with empty magazines always last
        bool sort_compare( const inventory_entry &lhs, const inventory_entry &rhs ) const override {
            const item *left = lhs.any_item();
            const item *right = rhs.any_item();

            if( left->ammo_remaining( ) == 0 || right->ammo_remaining( ) == 0 ) {
                return ( left->ammo_remaining( ) != 0 ) > ( right->ammo_remaining( ) != 0 );
            }

            if( left->obtain_cost( you ) != right->obtain_cost( you ) ) {
                return left->obtain_cost( you ) < right->obtain_cost( you );
            }

            if( left->ammo_remaining( ) != right->ammo_remaining( ) ) {
                return left->ammo_remaining( ) > right->ammo_remaining( );
            }

            return inventory_selector_preset::sort_compare( lhs, rhs );
        }

    private:
        player &you;
        item &target;
        bool empty;
};
} // namespace

// CDF: P3-5-3a, DDA game_inventory.cpp 3226-3259 (select_ammo_inventory_preset::is_shown) as a
// function on items, for tests/ (G2). `loc.parent_item() == target` is it.parent_item() == &target;
// the liquid-on-ground test uses item::bub_pos() and item::where() in place of loc.pos_bub( here )
// and loc.where(); rt.target.can_reload_with( loc, true ) is reload_loc_can_reload_with (P3-5-2a).
bool game_menus::inv::select_ammo_shows( item &target, const item &it, bool empty )
{
    // todo: allow to reload a magazine/magazine well from a container pocket on the same item
    if( it.parent_item() == &target ) {
        return false;
    }

    if( it.made_of( phase_id::LIQUID ) && it.where() == item_location_type::map ) {
        if( !g->m.has_flag_ter_or_furn( flag_LIQUIDCONT, it.bub_pos() ) ) {
            return false;
        }
    }

    if( it.is_frozen_liquid() ) {
        return false;
    }

    if( !empty && it.is_magazine() && !it.ammo_remaining( ) ) {
        return false;
    }

    const std::vector<reload_target> targets = get_possible_reload_targets( target );
    for( const reload_target &rt : targets ) {
        // Speedloader compatibility is queried on the owner gun, not
        // on a specific well pocket.
        if( it.has_flag( flag_SPEEDLOADER ) && rt.target->allows_speedloader( it.typeId() ) &&
            it.ammo_remaining() > 1 && rt.target->ammo_remaining() < 1 &&
            reload_loc_can_reload_with( *rt.target, it, true ) ) {
            return true;
        }
    }
    return find_matching_reload_target( targets, it ) != nullptr;
}

// CDF: P3-5-3b, DDA game_inventory.cpp 3335-3414 (the label loop of the destination menu) as a
// function, so that tests/cdf_item_ui_p3_test.cpp can read the labels; the uilist stays in
// select_ammo. Declared here and in that test, not in game_inventory.h (a test hook there would
// rebuild its 26 includers); keep the two signatures identical.
namespace game_menus
{
namespace inv
{
auto reload_destination_labels( const std::vector<const reload_target *> &matches )
-> std::vector<std::string>;
} // namespace inv
} // namespace game_menus

auto game_menus::inv::reload_destination_labels( const std::vector<const reload_target *> &matches )
-> std::vector<std::string>
{
    // Disambiguate: prompt the player, suffixing labels with (well N) when
    // labels would otherwise collide on the same owner.
    std::map<const item *, int> wells_per_owner;
    std::map<std::pair<const item *, itype_id>, int> mags_per_owner_type;
    for( const reload_target *rt : matches ) {
        if( rt->kind == reload_target::kind::well ) {
            wells_per_owner[rt->owner]++;
        } else {
            mags_per_owner_type[ {rt->owner, rt->target->typeId()}]++;
        }
    }
    std::map<const item *, int> well_counters;
    std::map<std::pair<const item *, itype_id>, int> mag_counters;
    std::vector<std::string> labels;
    for( const reload_target *match : matches ) {
        const reload_target &rt = *match;
        std::string label;
        if( rt.kind == reload_target::kind::well ) {
            int idx = 0;
            std::string well_desc;
            for( const item_pocket *p : rt.owner->get_pockets( []( const item_pocket & ) {
            return true;
        } ) ) {
                if( idx == rt.pocket_index && p->is_type( pocket_type::MAGAZINE_WELL ) ) {
                    const pocket_data *pd = p->get_pocket_data();
                    const std::string display = pd ? pd->pocket_name.translated() : std::string();
                    const itype_id default_mag = p->magazine_default();
                    if( !display.empty() ) {
                        well_desc = display;
                    } else if( !default_mag.is_null() ) {
                        well_desc = item::nname( default_mag, 1 );
                    } else {
                        well_desc = string_format( _( "magazine well %d" ), rt.ui_well_index + 1 );
                    }
                    break;
                }
                ++idx;
            }
            label = string_format( _( "%s: %s" ), rt.owner->tname(), well_desc );
            if( wells_per_owner[rt.owner] > 1 ) {
                const int n = ++well_counters[rt.owner];
                //~ %1$s is owner+well description, %2$d is 1-indexed well number on that owner
                label = string_format( _( "%1$s (well %2$d)" ), label, n );
            }
        } else if( rt.kind == reload_target::kind::integral_magazine ) {
            int idx = 0;
            std::string mag_desc;
            for( const item_pocket *p : rt.owner->get_pockets( []( const item_pocket & ) {
            return true;
        } ) ) {
                if( idx == rt.pocket_index && p->is_type( pocket_type::MAGAZINE ) ) {
                    const pocket_data *pd = p->get_pocket_data();
                    const std::string display = pd ? pd->pocket_name.translated() : std::string();
                    if( !display.empty() ) {
                        mag_desc = display;
                    }
                    break;
                }
                ++idx;
            }
            label = mag_desc.empty()
                    ? string_format( _( "%s: integral magazine" ), rt.owner->tname() )
                    : string_format( _( "%s: %s" ), rt.owner->tname(), mag_desc );
        } else {
            //~ %1$s is the gun or gunmod that holds the magazine, %2$s is the magazine's tname
            label = string_format( _( "%1$s: top up %2$s" ), rt.owner->tname(),
                                   rt.target->tname() );
            const std::pair<const item *, itype_id> key { rt.owner, rt.target->typeId() };
            if( mags_per_owner_type[key] > 1 ) {
                const int n = ++mag_counters[key];
                //~ %1$s is "<owner>: top up <mag name>", %2$d is 1-indexed well number on that owner
                label = string_format( _( "%1$s (well %2$d)" ), label, n );
            }
        }
        labels.push_back( label );
    }
    return labels;
}

// CDF: P3-5-3a, DDA game_inventory.cpp 3288-3433; the destination menu (3334-3421) is P3-5-3b.
// `player &` and `item &` in place of Character & and const item_location &, item_reload_option
// for item::reload_option, drop_location is { loc, count } here (not first / second), and the
// trailing include_potential is CDF-only (ADV-14 ruling 4): accepted for the 6-3b and 8-2a-sel
// callers. P3-6-6a: it is read by the empty-list explanation below (DDA has no such argument).
item_reload_option game_menus::inv::select_ammo( player &you, item &loc, bool prompt, bool empty,
        bool include_potential )
{
    // CDF: P3-6-6a. DDA dispatches Character::select_ammo virtually: avatar::select_ammo opens this menu
    // (avatar.cpp:1435-1442), npc::select_ammo never does (npcmove.cpp:2797). player has no such
    // virtual here, so an npc takes the pure selection (the first discovered option), as the BN reload
    // menu did (reload_selection::prepare, an npc is never asked).
    if( you.is_npc() ) {
        return reload_selection::prepare( you, loc, {
            .prompt = prompt,
            .discovery = { .include_empty_mags = empty, .include_potential = include_potential }
        } ).selected;
    }
    const select_ammo_inventory_preset preset( you, loc, empty );
    ammo_inventory_selector inv_s( you, loc, preset );

    inv_s.set_title( string_format( loc.is_watertight_container() ? _( "Refill %s" ) :
                                    loc.has_flag( flag_RELOAD_AND_SHOOT ) ? _( "Select ammo for %s" ) : _( "Reload %s" ),
                                    loc.display_name() ) );
    inv_s.set_hint( _( "Choose ammo to reload" ) );
    inv_s.set_display_stats( false );

    inv_s.clear_items();
    inv_s.add_character_items( you );
    inv_s.add_nearby_items( 1 );
    inv_s.set_all_entries_chosen_count();

    if( inv_s.empty() ) {
        // CDF: P3-6-6a. The BN reload menu told the player why nothing can be reloaded; DDA only has
        // the popup. Name the missing magazine or ammunition in the message log and skip the popup.
        const reload_selection::selection_result why = reload_selection::prepare( you, loc, {
            .prompt = prompt,
            .discovery = { .include_empty_mags = empty, .include_potential = include_potential }
        } );
        // CDF: P3-6-6a. The selector lists the carried items and the items on the map and in vehicle cargo
        // around the avatar, nested ones included (P3-NESTAMMO1, P3-NESTAMMO2), but discovery can find an
        // option the selector does not list. With one discovered option and no prompt the BN reload menu
        // took it without a menu: keep that (the DDA single-entry shortcut below). Not for
        // include_potential (8-2a-sel): there the list is DDA's, a full gun lists nothing.
        if( !include_potential && why.outcome == reload_selection::selection_outcome::automatic ) {
            return why.selected;
        }
        if( why.outcome == reload_selection::selection_outcome::missing_magazine ||
            why.outcome == reload_selection::selection_outcome::nothing_to_reload ||
            why.outcome == reload_selection::selection_outcome::missing_ammunition ) {
            reload_selection::report( you, loc, why.outcome );
            return item_reload_option();
        }
        popup( _( "You have nothing to reload." ), PF_GET_KEY );
        return item_reload_option();
    }

    drop_location selected;
    if( !prompt && inv_s.item_entry_count() == 1 ) {
        selected = inv_s.get_only_choice();
    } else {
        selected = inv_s.execute();
    }

    if( !selected.loc ) {
        // CDF: P3-6-6a, the BN menu printed this on cancel
        reload_selection::report( you, loc, reload_selection::selection_outcome::empty_supplied );
        return item_reload_option();
    }

    // The selector lists one entry per ammo source. When the picked source
    // matches multiple reload targets (sibling wells, multiple loaded mags
    // of the same type), disambiguate via a follow-up uilist instead of
    // duplicating selector entries per (source, target) pair.
    const std::vector<reload_target> targets = get_possible_reload_targets( loc );
    const std::vector<const reload_target *> matches =
        find_all_matching_reload_targets( targets, *selected.loc );
    if( matches.empty() ) {
        return item_reload_option();
    }

    const reload_target *match = matches.front();
    bool disambiguated = false;
    if( matches.size() > 1 ) {
        // CDF: P3-5-3b, DDA 3334-3421. The label loop (3335-3414) is reload_destination_labels, so
        // that a test can read the labels; the uilist, its title, the cancel return and the two
        // assignments are DDA's.
        const std::vector<std::string> labels = reload_destination_labels( matches );
        uilist menu;
        menu.text = string_format( _( "Reload which destination on %s?" ),
                                   loc.display_name() );
        for( size_t i = 0; i < labels.size(); ++i ) {
            menu.addentry( static_cast<int>( i ), true, MENU_AUTOASSIGN, labels[i] );
        }
        menu.query();
        if( menu.ret < 0 || menu.ret >= static_cast<int>( matches.size() ) ) {
            // CDF: P3-6-6a, the BN menu printed this on cancel
            reload_selection::report( you, loc, reload_selection::selection_outcome::empty_supplied );
            return item_reload_option();
        }
        match = matches[menu.ret];
        disambiguated = true;
    }

    // CDF: P3-NESTAMMO1. A liquid row (the liquid in a carried jug, listed as DDA lists it) is reloaded
    // from the container that holds it: CDF reload options carry the container of a contained liquid
    // (character_functions.cpp:1083-1087, DDA's option carries the liquid itself).
    item *source = &*selected.loc;
    if( item *holder = source->parent_item(); holder != nullptr && holder->is_container() &&
        source->made_of( phase_id::LIQUID ) ) {
        source = holder;
    }
    item_reload_option opt( &you, match->target, &loc, *source, match->pocket_index );
    if( disambiguated && !inv_s.last_choice_count_player_adjusted() ) {
        // Refill to the chosen target's own capacity when the seeded count
        // (from a sibling match) came back unmodified.
        opt.qty( INT_MAX );
    } else {
        opt.qty( selected.count );
    }

    return opt;
}
