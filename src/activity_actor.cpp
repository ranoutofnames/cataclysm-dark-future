#include "activity_actor.h"

#include "action.h"
#include "action_time_scale.h"
#include "activity_actor_definitions.h"
#include "activity_handlers.h" // put_into_vehicle_or_drop and drop_on_map
#include "activity_speed.h"
#include "advanced_inv.h"
#include "avatar.h"
#include "avatar_action.h"
#include "avatar_functions.h"
#include "cached_options.h"
#include "calendar.h"
#include "character.h"
#include "character_functions.h"
#include "construction.h"
#include "construction_partial.h"
#include "craft_command.h"
#include "crafting.h"
#include "crafting_quality.h"
#include "debug.h"
#include "enchantments/enchanter.h"
#include "enums.h"
#include "event.h"
#include "event_bus.h"
#include "flag.h"
#include "game.h"
#include "game_constants.h"
#include "game_inventory.h"
#include "gates.h"
#include "gun_mode.h"
#include "handle_liquid.h"
#include "iexamine.h"
#include "int_id.h"
#include "item.h"
#include "item_group.h"
#include "item_hauling.h"
#include "item_parent_checks.h"
#include "item_pocket.h"
#include "itype.h"
#include "json.h"
#include "line.h"
#include "locations.h"
#include "map/field_type.h"
#include "map/map.h"
#include "map/map_selector.h"
#include "map/mapdata.h"
#include "map_iterator.h"
#include "messages.h"
#include "npc.h"
#include "options.h"
#include "output.h"
#include "pickup.h"
#include "player.h"
#include "pocket_type.h"
#include "player_activity.h"
#include "point.h"
#include "ranged.h"
#include "reload/reload.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "requirements.h"
#include "ret_val.h"
#include "rng.h"
#include "sounds.h"
#include "timed_event.h"
#include "translations.h"
#include "ui.h"
#include "uistate.h"
#include "vehicle/vehicle.h"
#include "vehicle/vehicle_part.h"
#include "vehicle/vpart_position.h"
#include "itype_slots/islot_gun.h"
#include "itype_slots/islot_magazine.h"
#include "itype_slots/islot_tool.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#define dbg(x) DebugLog((x),DC::Game)

static const ammotype ammo_plutonium( "plutonium" );

static const construction_str_id deconstruct_simple( "constr_deconstruct_simple" );
static const construction_str_id deconstruct( "constr_deconstruct" );
static const construction_group_str_id
advanced_object_deconstruction( "advanced_object_deconstruction" );

static const itype_id itype_bone_human( "bone_human" );
static const itype_id itype_efile_junk( "efile_junk" );
static const itype_id itype_efile_photos( "efile_photos" );
static const itype_id itype_electrohack( "electrohack" );
static const itype_id itype_paper( "paper" );
static const itype_id itype_plut_cell( "plut_cell" );
static const itype_id itype_plut_cell_charge( "plut_cell_charge" );

static const skill_id skill_computer( "computer" );
static const skill_id skill_mechanics( "mechanics" );

static const mtype_id mon_zombie( "mon_zombie" );
static const mtype_id mon_zombie_fat( "mon_zombie_fat" );
static const mtype_id mon_zombie_rot( "mon_zombie_rot" );
static const mtype_id mon_skeleton( "mon_skeleton" );
static const mtype_id mon_zombie_crawler( "mon_zombie_crawler" );

static const quality_id qual_LOCKPICK( "LOCKPICK" );

static const trait_id trait_DEBUG_HS( "DEBUG_HS" );

static const std::string has_thievery_witness( "has_thievery_witness" );

int simple_task::to_counter() const
{
    double ret = 10'000'000.0 / moves_total * ( moves_total - moves_left );
    return std::round( ret );
}

inline void progress_counter::pop()
{
    if( empty() ) {
        dbg( DL::Error ) << "task was popped out of empty progress queue";
        return;
    }
    moves_left -= targets.front().moves_left;
    targets.pop_front();
    idx++;
}

inline void progress_counter::purge()
{
    if( empty() ) {
        dbg( DL::Error ) << "task was purged out of empty progress queue";
        return;
    }
    moves_left -= targets.front().moves_left;
    moves_total -= targets.front().moves_total;
    total_tasks--;
    targets.pop_front();
}

inline void activity_actor::calc_all_moves( player_activity &act, Character &who )
{
    act.speed.calc_all_moves( who );
}

aim_activity_actor::aim_activity_actor() : fake_weapon( new fake_item_location() )
{
    initial_view_offset = get_avatar().view_offset;
}

std::unique_ptr<aim_activity_actor> aim_activity_actor::use_wielded()
{
    return std::make_unique<aim_activity_actor>();
}

std::unique_ptr<aim_activity_actor> aim_activity_actor::use_bionic( detached_ptr<item> &&fake_gun,
        const units::energy &cost_per_shot )
{
    std::unique_ptr<aim_activity_actor> act( new aim_activity_actor() );
    act->bp_cost_per_shot = cost_per_shot;
    act->fake_weapon = std::move( fake_gun );
    return act;
}

std::unique_ptr<aim_activity_actor> aim_activity_actor::use_gear( item *gun )
{
    std::unique_ptr<aim_activity_actor> act( new aim_activity_actor() );
    act->weapon = safe_reference<item>( gun );
    return act;
}

std::unique_ptr<aim_activity_actor> aim_activity_actor::use_mutation( detached_ptr<item>
        &&fake_gun )
{
    std::unique_ptr<aim_activity_actor> act( new aim_activity_actor() );
    act->fake_weapon = std::move( fake_gun );
    return act;
}

void aim_activity_actor::start( player_activity &/*act*/, Character &/*who*/ )
{
    // Time spent on aiming is determined on the go by the player
    // Dummy progress task to indicate ongoing activity
    progress.dummy();
}

void aim_activity_actor::do_turn( player_activity &act, Character &who )
{
    if( !who.is_avatar() ) {
        debugmsg( "ACT_AIM not implemented for NPCs" );
        aborted = true;
        progress.pop();
        return;
    }
    avatar &you = get_avatar();

    item *weapon = get_weapon();
    if( !weapon || !avatar_action::can_fire_weapon( you, get_map(), *weapon ) ) {
        aborted = true;
        progress.pop();
        return;
    }

    gun_mode gun = weapon->gun_current_mode();
    if( !gun->ammo_remaining() && !reload_loc && gun->has_flag( flag_RELOAD_AND_SHOOT ) ) {
        if( !load_RAS_weapon() ) {
            aborted = true;
            progress.pop();
            return;
        }
    }
    g->temp_exit_fullscreen();
    target_handler::trajectory trajectory;
    if( const auto shape_gen = ranged::get_shape_factory( *weapon ) ) {
        trajectory = target_handler::mode_shaped( you, *shape_gen, *this );
    } else {
        trajectory = target_handler::mode_fire( you, *this );
    }
    g->reenter_fullscreen();

    if( aborted ) {
        progress.pop();
    } else {
        if( !trajectory.empty() ) {
            fin_trajectory = trajectory;
            progress.pop();
        }

        // Allow interrupting activity only during 'aim and fire'.
        // Prevents '.' key for 'aim for 10 turns' from conflicting with '.' key for 'interrupt activity'
        // in case of high input lag (curses, sdl sometimes...), but allows to interrupt aiming
        // if a bug happens / stars align to cause an endless aiming loop.
        act.interruptable_with_kb = action != "AIM";
    }
}

void aim_activity_actor::finish( player_activity &act, Character &who )
{
    act.set_to_null();
    item *weapon = get_weapon();
    if( !weapon ) {
        restore_view();
        return;
    }
    if( aborted ) {
        // CDF: P3-8-6c, DDA activity_actor.cpp:603 (before the optional reload)
        unload_RAS_weapon();
        if( reload_requested ) {
            // Reload the gun / select different arrows
            // May assign ACT_RELOAD
            avatar_action::reload_wielded( true );
        }
        restore_view();
        return;
    }

    // Fire!
    gun_mode gun = weapon->gun_current_mode();

    item *ammo_loc = reload_loc ? &*reload_loc : nullptr;

    int shots_fired = ranged::fire_gun( who, fin_trajectory.back(), gun.qty, *gun, ammo_loc );

    if( shots_fired > 0 ) {
        // TODO: bionic power cost of firing should be derived from a value of the relevant weapon.
        if( bp_cost_per_shot > 0_J ) {
            who.mod_power_level( -bp_cost_per_shot * shots_fired );
        }
        if( stamina_cost_per_shot > 0 ) {
            who.mod_stamina( -stamina_cost_per_shot * shots_fired );
        }
    }

    if( !get_option<bool>( "AIM_AFTER_FIRING" ) ||
        who.recoil <= ranged::calculate_aim_cap( who, fin_trajectory.back() ) ) {
        restore_view();
        return;
    }

    // re-enter aiming UI with same parameters
    std::unique_ptr<aim_activity_actor> aim_actor = std::make_unique<aim_activity_actor>();
    aim_actor->abort_if_no_targets = true;
    aim_actor->fake_weapon = std::move( this->fake_weapon );
    aim_actor->bp_cost_per_shot = this->bp_cost_per_shot;
    aim_actor->initial_view_offset = this->initial_view_offset;

    // if invalid target or it's dead - reset it so a new one is acquired
    shared_ptr_fast<Creature> last_target = who.last_target.lock();
    if( last_target && last_target->is_dead_state() ) {
        who.last_target.reset();
    }
    who.assign_activity( std::make_unique<player_activity>( std::move( aim_actor ) ), false );
}

void aim_activity_actor::canceled( player_activity &/*act*/, Character &/*who*/ )
{
    // CDF: P3-8-6c, DDA activity_actor.cpp:639-642
    unload_RAS_weapon();
    restore_view();
}

void aim_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "fake_weapon", fake_weapon ? *fake_weapon : null_item_reference() );
    jsout.member( "bp_cost_per_shot", bp_cost_per_shot );
    jsout.member( "stamina_cost_per_shot", stamina_cost_per_shot );
    jsout.member( "action", action );
    jsout.member( "aif_duration", aif_duration );
    jsout.member( "aiming_at_critter", aiming_at_critter );
    jsout.member( "snap_to_target", snap_to_target );
    jsout.member( "shifting_view", shifting_view );
    jsout.member( "initial_view_offset", initial_view_offset );
    jsout.member( "should_unload_RAS", should_unload_RAS );
    jsout.member( "reload_loc", reload_loc );
    jsout.member( "aborted", aborted );
    jsout.member( "reload_requested", reload_requested );
    jsout.member( "abort_if_no_targets", abort_if_no_targets );

    jsout.end_object();
}

std::unique_ptr<activity_actor> aim_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<aim_activity_actor> actor( new aim_activity_actor() );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "fake_weapon", actor->fake_weapon );
    data.read( "bp_cost_per_shot", actor->bp_cost_per_shot );
    data.read( "stamina_cost_per_shot", actor->stamina_cost_per_shot );
    data.read( "action", actor->action );
    data.read( "aif_duration", actor->aif_duration );
    data.read( "aiming_at_critter", actor->aiming_at_critter );
    data.read( "snap_to_target", actor->snap_to_target );
    data.read( "shifting_view", actor->shifting_view );
    data.read( "initial_view_offset", actor->initial_view_offset );
    data.read( "should_unload_RAS", actor->should_unload_RAS );
    data.read( "reload_loc", actor->reload_loc );
    data.read( "aborted", actor->aborted );
    data.read( "reload_requested", actor->reload_requested );
    data.read( "abort_if_no_targets", actor->abort_if_no_targets );

    return actor;
}

item *aim_activity_actor::get_weapon()
{
    if( weapon ) {
        return &*weapon;
    }
    if( fake_weapon ) {
        // TODO: check if the player lost relevant bionic/mutation
        return &*fake_weapon;
    } else {
        // Check for lost gun (e.g. yanked by zombie technician)
        // TODO: check that this is the same gun that was used to start aiming
        item *weapon = &get_player_character().primary_weapon();
        return weapon->is_null() ? nullptr : weapon;
    }
}

void aim_activity_actor::restore_view()
{
    avatar &player_character = get_avatar();
    bool changed_z = player_character.view_offset.z() != initial_view_offset.z();
    player_character.view_offset = initial_view_offset;
    if( changed_z ) {
        get_map().invalidate_map_cache( player_character.view_offset.z() );
        g->invalidate_main_ui_adaptor();
    }
}

bool aim_activity_actor::load_RAS_weapon()
{
    // TODO: use activity for fetching ammo and loading weapon
    player &you = get_avatar();
    item *weapon = get_weapon();
    gun_mode gun = weapon->gun_current_mode();

    // Will burn (0.2% max base stamina * the strength required to fire)
    stamina_cost_per_shot = gun->get_min_str() * static_cast<int>
                            ( 0.002f * get_option<int>( "PLAYER_MAX_STAMINA" ) );
    if( you.get_stamina() < stamina_cost_per_shot ) {
        you.add_msg_if_player( m_bad, _( "You're too tired to draw your %s." ), weapon->tname() );
        return false;
    }

    const auto ammo_location_is_valid = [&]() -> bool {
        if( !you.ammo_location )
        {
            return false;
        }
        if( !gun->can_reload_with( you.ammo_location->typeId() ) )
        {
            return false;
        }
        if( square_dist( you.abs_pos(), you.ammo_location->abs_pos() ) > 1 )
        {
            return false;
        }
        return true;
    };
    auto opt = ammo_location_is_valid() ? item_reload_option( &you, weapon, weapon,
               *you.ammo_location ) : game_menus::inv::select_ammo( you, *gun );
    if( !opt ) {
        // Menu canceled
        return false;
    }

    reload_loc = opt.ammo;
    return true;
}

void aim_activity_actor::unload_RAS_weapon()
{
    // CDF: P3-8-6c, DDA activity_actor.cpp:749-763. The gun is unloaded only when it holds ammo;
    // unload_item( ..., true ) is DDA's you.unload( loc, true ) (bypass_activity).
    item *weapon = get_weapon();
    if( !weapon || !should_unload_RAS ) {
        return;
    }

    gun_mode gun = weapon->gun_current_mode();
    if( gun && gun->has_flag( flag_RELOAD_AND_SHOOT ) ) {
        if( gun->ammo_remaining() ) {
            avatar_funcs::unload_item( get_avatar(), *gun.target, true );
        }
    }
}

void autodrive_activity_actor::start( player_activity &/* act */, Character &who )
{
    const bool in_vehicle = who.in_vehicle && who.controlling_vehicle;
    const optional_vpart_position vp = get_map().veh_at( who.bub_pos() );
    if( !( vp && in_vehicle ) ) {
        who.cancel_activity();
        return;
    }

    player_vehicle = &vp->vehicle();
    if( player_vehicle->is_flying_in_air() ) {
        int min_speed = player_vehicle->get_takeoff_speed( "t/t" );
        if( player_vehicle->velocity * 0.8 < min_speed * vehicles::cmps_per_tile ) {
            if( !g->u.query_yn( "Warning: Current Speed is below recommened values, proceed?" ) ) {
                who.cancel_activity();
                return;
            }
        }
        if( player_vehicle->min_autodrive_speed * 0.8 < min_speed ) {
            if( !g->u.query_yn( "Warning: Min Autodrive Speed is below recommened values, proceed?" ) ) {
                who.cancel_activity();
                return;
            }
        }
        if( player_vehicle->max_autodrive_speed * 0.5 < min_speed ) {
            if( !g->u.query_yn( "Warning: Max Autodrive Speed is below recommened values, proceed?" ) ) {
                who.cancel_activity();
                return;
            }
        }
    }
    player_vehicle->is_autodriving = true;
    progress.dummy();
}

void autodrive_activity_actor::do_turn( player_activity &/* act */, Character &who )
{
    if( who.in_vehicle && who.controlling_vehicle && player_vehicle ) {
        if( who.moves <= 0 ) {
            // out of moves? the driver's not doing anything this turn
            // (but the vehicle will continue moving)
            return;
        }
        switch( player_vehicle->do_autodrive( who ) ) {
            case autodrive_result::ok:
                if( who.moves > 0 ) {
                    // if do_autodrive() didn't eat up all our moves, end the turn
                    // equivalent to player pressing the "pause" button
                    who.moves = 0;
                }
                sounds::reset_markers();
                break;
            case autodrive_result::abort:
                who.cancel_activity();
                break;
            case autodrive_result::finished:
                progress.pop();
                break;
        }
    } else {
        who.cancel_activity();
    }
}

void autodrive_activity_actor::canceled( player_activity &act, Character &who )
{
    who.add_msg_if_player( m_info, _( "Auto-drive canceled." ) );
    who.omt_path.clear();
    if( player_vehicle ) {
        player_vehicle->stop_autodriving( false );
    }
    act.set_to_null();
}

void autodrive_activity_actor::finish( player_activity &act, Character &who )
{
    who.add_msg_if_player( m_info, _( "You have reached your destination." ) );
    player_vehicle->stop_autodriving( false );
    act.set_to_null();
}

void autodrive_activity_actor::serialize( JsonOut &jsout ) const
{
    // Activity is not being saved but still provide some valid json if called.
    jsout.write_null();
}

std::unique_ptr<activity_actor> autodrive_activity_actor::deserialize( JsonIn & )
{
    return std::make_unique<autodrive_activity_actor>();
}

void dig_activity_actor::start( player_activity &/*act*/, Character & )
{
    map &here = get_map();
    ter_id ter_here = here.ter( location );
    const bool grave = ter_here == t_grave;
    const std::string name = grave
                             ? "grave"
                             : ter_here->name();
    progress.emplace( name, moves_total );
}

void dig_activity_actor::do_turn( player_activity &/*act*/, Character &who )
{
    if( progress.front().complete() ) {
        progress.pop();
        return;
    }
    sfx::play_activity_sound( "tool", "shovel", sfx::get_heard_volume( location, 60 ) );
    if( action_time_scale::once_every_this_tick( 1_minutes ) ) {
        //~ Sound of a shovel digging a pit at work!
        sound_event se;
        se.origin = location;
        se.volume = 60;
        se.category = sounds::sound_t::activity;
        se.description = _( "hsh!" );
        se.id =  "tool";
        se.variant = "shovel";
        se.from_player = who.is_player();
        se.from_npc = !se.from_player;
        se.faction = who.get_faction()->id;
        se.monfaction = who.get_faction()->mon_faction;
        sounds::sound( se );
    }
}

void dig_activity_actor::finish( player_activity &act, Character &who )
{
    map &here = get_map();
    const bool grave = here.ter( location ) == t_grave;

    if( grave ) {
        if( one_in( 10 ) ) {
            static const std::array<mtype_id, 5> monids = { {
                    mon_zombie, mon_zombie_fat,
                    mon_zombie_rot, mon_skeleton,
                    mon_zombie_crawler
                }
            };

            g->place_critter_at( random_entry( monids ), byproducts_location );
            here.furn_set( location, f_coffin_o );
            who.add_msg_if_player( m_warning, _( "Something crawls out of the coffin!" ) );
        } else {
            here.spawn_item( location, itype_bone_human, rng( 5, 15 ) );
            here.furn_set( location, f_coffin_c );
        }
        std::vector<item *> dropped = get_map().place_items( item_group_id( "allclothes" ), 50, location,
                                      location, false,
                                      calendar::turn );
        get_map().place_items( item_group_id( "grave" ), 25, location, location, false, calendar::turn );
        get_map().place_items( item_group_id( "jewelry_front" ), 20, location, location, false,
                               calendar::turn );
        for( item * const &it : dropped ) {
            if( it->is_armor() ) {
                it->set_damage( rng( 1, it->max_damage() - 1 ) );
            }
        }
        g->events().send<event_type::exhumes_grave>( who.getID() );
    }

    here.ter_set( location, ter_id( result_terrain ) );

    here.spawn_items( byproducts_location,
                      item_group::items_from( item_group_id( byproducts_item_group ),
                              calendar::turn ) );

    const int act_exertion = moves_total;

    who.mod_stored_kcal( std::min( -1, -act_exertion / to_moves<int>( 80_seconds ) ) );
    who.mod_thirst( std::max( 1, act_exertion / to_moves<int>( 12_minutes ) ) );
    who.mod_fatigue( std::max( 1, act_exertion / to_moves<int>( 6_minutes ) ) );
    who.mod_stamina( std::min( -1, -act_exertion / to_moves<int>( 10_seconds ) ) );
    if( grave ) {
        who.add_msg_if_player( m_good, _( "You finish exhuming a grave." ) );
    } else {
        who.add_msg_if_player( m_good, _( "You finish digging the %s." ),
                               here.ter( location )->name() );
    }

    act.set_to_null();
}

void dig_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "moves", moves_total );
    jsout.member( "location", location );
    jsout.member( "result_terrain", result_terrain );
    jsout.member( "byproducts_location", byproducts_location );
    jsout.member( "byproducts_item_group", byproducts_item_group );

    jsout.end_object();
}

std::unique_ptr<activity_actor> dig_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<dig_activity_actor> actor( new dig_activity_actor( 0, tripoint_bub_ms::zero(),
            {}, tripoint_bub_ms::zero(), {} ) );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "moves", actor->moves_total );
    data.read( "location", actor->location );
    data.read( "result_terrain", actor->result_terrain );
    data.read( "byproducts_location", actor->byproducts_location );
    data.read( "byproducts_item_group", actor->byproducts_item_group );

    return actor;
}

void dig_channel_activity_actor::start( player_activity &/*act*/, Character & )
{
    map &here = get_map();
    progress.emplace( here.ter( location )->name(), moves_total );
}

void dig_channel_activity_actor::do_turn( player_activity &/*act*/, Character &who )
{
    if( progress.front().complete() ) {
        progress.pop();
        return;
    }
    sfx::play_activity_sound( "tool", "shovel", sfx::get_heard_volume( location, 70 ) );
    if( action_time_scale::once_every_this_tick( 1_minutes ) ) {
        //~ Sound of a shovel digging a pit at work!
        sound_event se;
        se.origin = location;
        se.volume = 70;
        se.category = sounds::sound_t::activity;
        se.description = _( "hsh!" );
        se.id =  "tool";
        se.variant =  "shovel";
        se.from_player = who.is_player();
        se.from_npc = !se.from_player;
        se.faction = who.get_faction()->id;
        se.monfaction = who.get_faction()->mon_faction;
        sounds::sound( se );
    }
}

void dig_channel_activity_actor::finish( player_activity &act, Character &who )
{
    map &here = get_map();
    here.ter_set( location, ter_id( result_terrain ) );

    here.spawn_items( byproducts_location,
                      item_group::items_from( item_group_id( byproducts_item_group ),
                              calendar::turn ) );

    const int act_exertion = moves_total;

    who.mod_stored_kcal( std::min( -1, -act_exertion / to_moves<int>( 80_seconds ) ) );
    who.mod_thirst( std::max( 1, act_exertion / to_moves<int>( 12_minutes ) ) );
    who.mod_fatigue( std::max( 1, act_exertion / to_moves<int>( 6_minutes ) ) );
    who.mod_stamina( std::min( -1, -act_exertion / to_moves<int>( 10_seconds ) ) );
    who.add_msg_if_player( m_good, _( "You finish digging up %s." ),
                           here.ter( location )->name() );

    act.set_to_null();
}

void dig_channel_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "moves", moves_total );
    jsout.member( "location", location );
    jsout.member( "result_terrain", result_terrain );
    jsout.member( "byproducts_location", byproducts_location );
    jsout.member( "byproducts_item_group", byproducts_item_group );

    jsout.end_object();
}

std::unique_ptr<activity_actor> dig_channel_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<dig_channel_activity_actor> actor( new dig_channel_activity_actor( 0,
            tripoint_bub_ms::zero(),
            {}, tripoint_bub_ms::zero(), {} ) );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "moves", actor->moves_total );
    data.read( "location", actor->location );
    data.read( "result_terrain", actor->result_terrain );
    data.read( "byproducts_location", actor->byproducts_location );
    data.read( "byproducts_item_group", actor->byproducts_item_group );

    return actor;
}

bool disassemble_activity_actor::try_start_single( player_activity &/* act */, Character &who )
{
    if( targets.empty() ) {
        return false;
    }
    const iuse_location &target = targets.front();
    if( !target.loc ) {
        debugmsg( "Lost target of ACT_DISASSEMBLE" );
        targets.clear();
        return false;
    }
    const item &itm = *target.loc;

    // Have to check here again in case we ran out of tools
    const ret_val<bool> can_do = crafting::can_disassemble( who, itm, who.crafting_inventory() );
    if( !can_do.success() ) {
        who.add_msg_if_player( m_info, "%s", can_do.c_str() );
        return false;
    }
    return true;
}

inline void disassemble_activity_actor::process_target( player_activity &/*act*/,
        iuse_location &target )
{
    const item &itm = *target.loc;
    const recipe &dis = recipe_dictionary::get_uncraft( itm.typeId() );
    int moves_needed = dis.time * target.count;
    progress.emplace( itm.tname( target.count ), moves_needed );
}

inline void disassemble_activity_actor::calc_all_moves( player_activity &act, Character &who )
{
    const auto &target = targets.front().loc;
    auto reqs = activity_reqs_adapter( recipe_dictionary::get_uncraft( target->typeId() ),
                                       std::make_pair( target->weight(), target->volume() ) );
    act.speed.calc_all_moves( who, reqs );
}

void disassemble_activity_actor::start( player_activity &act, Character &who )
{
    if( !who.is_avatar() ) {
        debugmsg( "ACT_DISASSEMBLE is not implemented for NPCs" );
        act.set_to_null();
    } else if( !try_start_single( act, who ) ) {
        act.set_to_null();
    }
    for( auto &target : targets ) {
        process_target( act, target );
    }
}

void disassemble_activity_actor::do_turn( player_activity &act, Character &who )
{
    if( progress.front().complete() ) {
        const iuse_location &target = targets.front();
        if( !target.loc ) {
            debugmsg( "Lost target of ACT_DISASSEMBLY" );
        } else {
            crafting::complete_disassemble( who, target, abs_to_bub( pos ) );
        }
        targets.erase( targets.begin() );
        progress.pop();
        if( !progress.empty() ) {
            if( try_start_single( act, who ) ) {
                calc_all_moves( act, who );
            } else {
                act.set_to_null();
            }
        }
    }
}

void disassemble_activity_actor::finish( player_activity &act, Character &who )
{
    if( try_start_single( act, who ) ) {
        debugmsg( "disassemble_activity_actor call finish function while able to start new disassembly" );
    }
    // Make a copy to avoid use-after-free
    bool recurse = this->recursive;

    act.set_to_null();

    if( recurse ) {
        crafting::disassemble_all( *who.as_avatar(), recurse );
    }
}

void disassemble_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "targets", targets );
    jsout.member( "pos", pos );
    jsout.member( "recursive", recursive );

    jsout.end_object();
}

std::unique_ptr<activity_actor> disassemble_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<disassemble_activity_actor> actor( new disassemble_activity_actor() );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "targets", actor->targets );
    data.read( "pos", actor->pos );
    data.read( "recursive", actor->recursive );

    return actor;
}

drop_activity_actor::drop_activity_actor( Character &ch, const drop_locations &items,
        bool force_ground, const tripoint_rel_ms &relpos )
    : force_ground( force_ground ), relpos( relpos )
{
    this->items = pickup::reorder_for_dropping( ch, items );
}

void drop_activity_actor::start( player_activity &/* act */, Character & )
{
    // Dummy progress task to indicate ongoing activity
    progress.dummy();
}

void drop_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "items", items );
    jsout.member( "force_ground", force_ground );
    jsout.member( "relpos", relpos );

    jsout.end_object();
}

std::unique_ptr<activity_actor> drop_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<drop_activity_actor> actor( new drop_activity_actor() );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "items", actor->items );
    data.read( "force_ground", actor->force_ground );
    data.read( "relpos", actor->relpos );

    return actor;
}

enum hack_result {
    HACK_UNABLE,
    HACK_FAIL,
    HACK_NOTHING,
    HACK_SUCCESS
};

enum hack_type {
    HACK_SAFE,
    HACK_DOOR,
    HACK_GAS,
    HACK_NULL
};

static hack_type get_hack_type( tripoint_bub_ms examp )
{
    hack_type type = HACK_NULL;
    const map &here = get_map();
    const furn_t &xfurn_t = *here.furn( examp );
    const ter_t &xter_t = *here.ter( examp );
    if( xter_t.examine == &iexamine::pay_gas || xfurn_t.examine == &iexamine::pay_gas ) {
        type = HACK_GAS;
    } else if( xter_t.examine == &iexamine::cardreader || xfurn_t.examine == &iexamine::cardreader ) {
        type = HACK_DOOR;
    } else if( xter_t.examine == &iexamine::gunsafe_el || xfurn_t.examine == &iexamine::gunsafe_el ) {
        type = HACK_SAFE;
    }
    return type;
}

void hacking_activity_actor::start( player_activity &act, Character & )
{
    hack_type type = get_hack_type( abs_to_bub( act.placement ) );
    std::string name;

    switch( type ) {
        case hack_type::HACK_SAFE:
            name = "safe";
            break;
        case hack_type::HACK_DOOR:
            name = "door panel";
            break;
        case hack_type::HACK_GAS:
            name = "gas pump";
            break;
        default:
            name = "";
            break;
    }

    progress.emplace( name, to_moves<int>( 5_minutes ) );
}

void hacking_activity_actor::do_turn( player_activity &/*act*/, Character & )
{
    if( progress.front().complete() ) {
        progress.pop();
        return;
    }
}

static int hack_level( const Character &who )
{
    ///\EFFECT_COMPUTER increases success chance of hacking card readers
    // odds go up with int>8, down with int<8
    // 4 int stat is worth 1 computer skill here
    ///\EFFECT_INT increases success chance of hacking card readers
    return who.get_skill_level( skill_computer ) + ( who.int_cur - 8 ) / 4;
}

static hack_result hack_attempt( Character &who, const bool using_bionic )
{
    who.practice( skill_computer, 20 );
    // only skilled supergenius never cause short circuits, but the odds are low for people
    // with moderate skills
    const int hack_stddev = 5;
    int success = std::ceil( normal_roll( hack_level( who ), hack_stddev ) );
    if( success < 0 ) {
        who.add_msg_if_player( _( "You cause a short circuit!" ) );
        if( using_bionic ) {
            who.mod_power_level( -25_kJ );
        } else {
            who.use_charges( itype_electrohack, 25 );
        }

        if( success <= -5 ) {
            if( !using_bionic ) {
                who.add_msg_if_player( m_bad, _( "Your electrohack is ruined!" ) );
                who.use_amount( itype_electrohack, 1 );
            } else {
                who.add_msg_if_player( m_bad, _( "Your power is drained!" ) );
                who.mod_power_level( units::from_kilojoule( -rng( 25,
                                     units::to_kilojoule( who.get_power_level() ) ) ) );
            }
        }
        return HACK_FAIL;
    } else if( success < 6 ) {
        return HACK_NOTHING;
    } else {
        return HACK_SUCCESS;
    }
}

hacking_activity_actor::hacking_activity_actor( use_bionic )
    : using_bionic( true )
{
}

void hacking_activity_actor::finish( player_activity &act, Character &who )
{
    tripoint_bub_ms examp = abs_to_bub( act.placement );
    hack_type type = get_hack_type( examp );
    map &here = get_map();
    sound_event se;
    switch( hack_attempt( who, using_bionic ) ) {
        case HACK_UNABLE:
            who.add_msg_if_player( _( "You cannot hack this." ) );
            break;
        case HACK_FAIL:
            // currently all things that can be hacked have equivalent alarm failure states.
            // this may not always be the case with new hackable things.
            g->events().send<event_type::triggers_alarm>( who.getID() );
            se.origin = who.bub_pos();
            se.volume = 120;
            se.category = sounds::sound_t::music;
            se.description = _( "an alarm sound!" );
            se.id = "environment";
            se.variant = "alarm";
            sounds::sound( se );
            if( examp.z() > 0 && !g->timed_events.queued( TIMED_EVENT_WANTED ) ) {
                g->timed_events.add( TIMED_EVENT_WANTED, calendar::turn + 30_minutes, 0,
                                     who.abs_sm_pos() );
            }
            break;
        case HACK_NOTHING:
            who.add_msg_if_player( _( "You fail the hack, but no alarms are triggered." ) );
            break;
        case HACK_SUCCESS:
            if( type == HACK_GAS ) {
                int tankGasUnits;
                const std::optional<tripoint_bub_ms> pTank_ = iexamine::getNearFilledGasTank( examp, tankGasUnits );
                if( !pTank_ ) {
                    break;
                }
                const tripoint_bub_ms pTank = *pTank_;
                const std::optional<tripoint_bub_ms> pGasPump = iexamine::getGasPumpByNumber( examp,
                        uistate.ags_pay_gas_selected_pump );
                if( pGasPump && iexamine::toPumpFuel( pTank, *pGasPump, tankGasUnits ) ) {
                    who.add_msg_if_player( _( "You hack the terminal and route all available fuel to your pump!" ) );
                    se.origin = examp;
                    se.volume = 40;
                    se.category = sounds::sound_t::activity;
                    se.description = _( "Glug Glug Glug Glug Glug Glug Glug Glug Glug" );
                    se.id = "tool";
                    se.variant =  "gaspump";
                    se.from_player = who.is_player();
                    se.from_npc = !se.from_player;
                    se.faction = who.get_faction()->id;
                    se.monfaction = who.get_faction()->mon_faction;
                    sounds::sound( se );
                } else {
                    who.add_msg_if_player( _( "Nothing happens." ) );
                }
            } else if( type == HACK_SAFE ) {
                who.add_msg_if_player( m_good, _( "The door on the safe swings open." ) );
                here.furn_set( examp, furn_str_id( "f_gunsafe_o" ) );
            } else if( type == HACK_DOOR ) {
                who.add_msg_if_player( _( "You activate the panel!" ) );
                who.add_msg_if_player( m_good, _( "The nearby doors unlock." ) );
                here.ter_set( examp, t_card_reader_broken );
                for( const tripoint_bub_ms &tmp : here.points_in_radius( ( examp ), 3 ) ) {
                    if( here.ter( tmp ) == t_door_metal_locked ) {
                        here.ter_set( tmp, t_door_metal_c );
                    }
                }
            }
            break;
    }
    act.set_to_null();
}

void hacking_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "using_bionic", using_bionic );

    jsout.end_object();
}

std::unique_ptr<activity_actor> hacking_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<hacking_activity_actor> actor( new hacking_activity_actor() );
    if( jsin.test_null() ) {
        // Old saves might contain a null instead of an object.
        // Since we do not know whether a bionic or an item was chosen we assume
        // it was an item.
        actor->using_bionic = false;
    } else {
        JsonObject jsobj = jsin.get_object();
        jsobj.read( "using_bionic", actor->using_bionic );
        jsobj.read( "progress", actor->progress );
    }
    return actor;
}

void move_items_activity_actor::do_turn( player_activity &act, Character &who )
{
    const auto dest = relative_destination + who.abs_pos();

    while( who.moves > 0 && !target_items.empty() ) {
        safe_reference<item> target = std::move( target_items.back() );
        const int quantity = quantities.back();
        target_items.pop_back();
        quantities.pop_back();

        if( !target ) {
            //TODO!: might not be appropriate to debugmsg just because something was destroyed/unloaded
            debugmsg( "Lost target item of ACT_MOVE_ITEMS" );
            continue;
        }

        // Check that we can pick it up.
        if( target->made_of( LIQUID ) ) {
            continue;
        }

        // This is for hauling across zlevels, remove when going up and down stairs
        // is no longer teleportation
        // Also ignores items owned by other NPCs, unless they'd already attack on sight
        if( target->is_owned_by( who, true ) || target->get_owner()->likes_u < -10 ) {
            target->set_owner( who );
        } else {
            continue;
        }

        const auto src = target->abs_pos();
        detached_ptr<item> newit = quantity == 0 ? target->detach() : target->split( quantity );

        const int distance = src.z() == dest.z() ? std::max( rl_dist( src, dest ), 1 ) : 1;
        who.mod_moves( -pickup::cost_to_move_item( who, *newit ) * distance );

        std::vector<detached_ptr<item>> vec;
        vec.push_back( std::move( newit ) );
        if( to_vehicle ) {
            put_into_vehicle_or_drop( who, item_drop_reason::deliberate, vec, abs_to_bub( dest ) );
        } else {
            drop_on_map( who, item_drop_reason::deliberate, vec, abs_to_bub( dest ) );
        }
    }

    if( target_items.empty() ) {
        // Nuke the current activity, leaving the backlog alone.
        act.set_to_null();
        if( who.is_hauling() && !has_haulable_items( who.bub_pos() ) ) {
            who.stop_hauling();
        }
    }
}

void move_items_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "target_items", target_items );
    jsout.member( "quantities", quantities );
    jsout.member( "to_vehicle", to_vehicle );
    jsout.member( "relative_destination", relative_destination );

    jsout.end_object();
}

std::unique_ptr<activity_actor> move_items_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<move_items_activity_actor> actor( new move_items_activity_actor( {}, {}, false,
            tripoint_rel_ms::zero() ) );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "target_items", actor->target_items );
    data.read( "quantities", actor->quantities );
    data.read( "to_vehicle", actor->to_vehicle );
    data.read( "relative_destination", actor->relative_destination );

    return actor;
}

void pickup_activity_actor::do_turn( player_activity &act, Character &who )
{
    // If we don't have target items bail out
    if( target_items.empty() ) {
        who.cancel_activity();
        return;
    }

    // If the player moves while picking up (i.e.: in a moving vehicle) cancel
    // the activity, only populate starting_pos when grabbing from the ground
    if( starting_pos && *starting_pos != who.bub_pos() ) {
        who.cancel_activity();
        who.add_msg_if_player( _( "Moving canceled auto-pickup." ) );
        return;
    }

    // Auto_resume implies autopickup.
    const bool autopickup = who.activity->auto_resume;

    // False indicates that the player canceled pickup when met with some prompt
    const bool keep_going = pickup::do_pickup( target_items, autopickup );

    // Check thievey witness
    npc *witness = nullptr;
    if( !act.str_values.empty() && act.str_values[0] == has_thievery_witness ) {
        for( npc &guy : g->all_npcs() ) {
            if( guy.get_attitude() == NPCATT_RECOVER_GOODS ) {
                witness = &guy;
                break;
            }
        }
    }

    // If there are items left we ran out of moves, so continue the activity
    // Otherwise, we are done.
    if( !keep_going || target_items.empty() || witness ) {
        who.cancel_activity();

        if( who.get_value( "THIEF_MODE_KEEP" ) != "YES" ) {
            who.set_value( "THIEF_MODE", "THIEF_ASK" );
        }

        if( !keep_going ) {
            // The user canceled the activity, so we're done
            // AIM might have more pickup activities pending, also cancel them.
            // TODO: Move this to advanced inventory instead of hacking it in here
            cancel_aim_processing();
        }

        if( witness ) {
            witness->talk_to_u();
            // Then remove "has_thievery_witness" from the activity
            act.str_values.clear();
        }
    }
}

void pickup_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "target_items", target_items );
    jsout.member( "starting_pos", starting_pos );

    jsout.end_object();
}

std::unique_ptr<activity_actor> pickup_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<pickup_activity_actor> actor( new pickup_activity_actor( {}, std::nullopt ) );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "target_items", actor->target_items );
    data.read( "starting_pos", actor->starting_pos );

    return actor;
}

void hacksaw_activity_actor::start( player_activity &act, Character &/*who*/ )
{
    const map &here = get_map();

    if( here.has_furn( target ) ) {
        const furn_id furn_type = here.furn( target );
        if( !furn_type->hacksaw->valid() ) {
            if( !testing ) {
                debugmsg( "%s hacksaw is invalid", furn_type.id().str() );
            }
            act.set_to_null();
            return;
        }
        progress.emplace( furn_type->name(), to_moves<int>( furn_type->hacksaw->duration() ) );
    } else if( !here.ter( target )->is_null() ) {
        const ter_id ter_type = here.ter( target );
        if( !ter_type->hacksaw->valid() ) {
            if( !testing ) {
                debugmsg( "%s hacksaw is invalid", ter_type.id().str() );
            }
            act.set_to_null();
            return;
        }
        progress.emplace( ter_type->name(), to_moves<int>( ter_type->hacksaw->duration() ) );
    } else {
        if( !testing ) {
            debugmsg( "hacksaw activity called on invalid terrain" );
        }
        act.set_to_null();
        return;
    }
}

void hacksaw_activity_actor::do_turn( player_activity &/* act */, Character &who )
{
    if( progress.front().complete() ) {
        progress.pop();
        return;
    }
    if( tool->ammo_sufficient( &who ) ) {
        tool->ammo_consume( tool->ammo_required(), tool->bub_pos(), &who );
        sfx::play_activity_sound( "tool", "hacksaw", sfx::get_heard_volume( target, 80 ) );
        if( action_time_scale::once_every_this_tick( 1_minutes ) ) {
            //~ Sound of a metal sawing tool at work!
            sound_event se;
            se.origin = target;
            se.volume = 80;
            se.category = sounds::sound_t::destructive_activity;
            se.description = _( "grnd grnd grnd" );
            se.id = "tool";
            se.variant = "hacksaw";
            se.from_player = who.is_player();
            se.from_npc = !se.from_player;
            se.faction = who.get_faction()->id;
            se.monfaction = who.get_faction()->mon_faction;
            sounds::sound( se );
        }
    } else {
        if( who.is_avatar() ) {
            who.add_msg_if_player( m_bad, _( "Your %1$s ran out of charges." ), tool->tname() );
        } else { // who.is_npc()
            if( get_avatar().sees( who.bub_pos() ) ) {
                add_msg( _( "%1$s %2$s ran out of charges." ), who.disp_name( false,
                         true ), tool->tname() );
            }
        }
        who.cancel_activity();
    }
}

void hacksaw_activity_actor::finish( player_activity &act, Character &who )
{
    map &here = get_map();
    const activity_data_common *data;

    if( here.has_furn( target ) ) {
        const furn_id furn_type = here.furn( target );
        if( !furn_type->hacksaw->valid() ) {
            if( !testing ) {
                debugmsg( "%s hacksaw is invalid", furn_type.id().str() );
            }
            act.set_to_null();
            return;
        }

        const furn_str_id new_furn = furn_type->hacksaw->result();
        if( !new_furn.is_valid() ) {
            if( !testing ) {
                debugmsg( "hacksaw furniture: %s invalid furniture", new_furn.str() );
            }
            act.set_to_null();
            return;
        }

        data = static_cast<const activity_data_common *>( &*furn_type->hacksaw );
        here.furn_set( target, new_furn );
    } else if( !here.ter( target )->is_null() ) {
        const ter_id ter_type = here.ter( target );
        if( !ter_type->hacksaw->valid() ) {
            if( !testing ) {
                debugmsg( "%s hacksaw is invalid", ter_type.id().str() );
            }
            act.set_to_null();
            return;
        }

        const ter_str_id new_ter = ter_type->hacksaw->result();
        if( !new_ter.is_valid() ) {
            if( !testing ) {
                debugmsg( "hacksaw terrain: %s invalid terrain", new_ter.str() );
            }
            act.set_to_null();
            return;
        }

        data = static_cast<const activity_data_common *>( &*ter_type->hacksaw );
        here.ter_set( target, new_ter );
    } else {
        if( !testing ) {
            debugmsg( "hacksaw activity finished on invalid terrain" );
        }
        act.set_to_null();
        return;
    }

    for( const activity_byproduct &byproduct : data->byproducts() ) {
        const int amount = byproduct.roll();
        if( byproduct.item->count_by_charges() ) {
            here.add_item_or_charges( target, item::spawn( byproduct.item, calendar::turn, amount ) );
        } else {
            for( int i = 0; i < amount; ++i ) {
                here.add_item_or_charges( target, item::spawn( byproduct.item, calendar::turn ) );
            }
        }
    }

    if( !data->message().empty() ) {
        who.add_msg_if_player( m_info, data->message().translated() );
    }

    act.set_to_null();
}

void hacksaw_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.member( "tool", tool );

    jsout.end_object();
}

std::unique_ptr<activity_actor> hacksaw_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<hacksaw_activity_actor> actor( new hacksaw_activity_actor(
                tripoint_bub_ms::zero(), safe_reference<item>() ) );
    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    data.read( "target", actor->target );
    data.read( "tool", actor->tool );
    return actor;
}

void boltcutting_activity_actor::start( player_activity &act, Character &/*who*/ )
{
    const map &here = get_map();

    if( here.has_furn( target ) ) {
        const furn_id furn_type = here.furn( target );
        if( !furn_type->boltcut->valid() ) {
            if( !testing ) {
                debugmsg( "%s boltcut is invalid", furn_type.id().str() );
            }
            act.set_to_null();
            return;
        }
        progress.emplace( furn_type->name(), to_moves<int>( furn_type->boltcut->duration() ) );
    } else if( !here.ter( target )->is_null() ) {
        const ter_id ter_type = here.ter( target );
        if( !ter_type->boltcut->valid() ) {
            if( !testing ) {
                debugmsg( "%s boltcut is invalid", ter_type.id().str() );
            }
            act.set_to_null();
            return;
        }
        progress.emplace( ter_type->name(), to_moves<int>( ter_type->boltcut->duration() ) );
    } else {
        if( !testing ) {
            debugmsg( "boltcut activity called on invalid terrain" );
        }
        act.set_to_null();
        return;
    }
}

void boltcutting_activity_actor::do_turn( player_activity &/* act */, Character &who )
{
    if( progress.front().complete() ) {
        progress.pop();
        return;
    }
    if( tool->ammo_sufficient( &who ) ) {
        tool->ammo_consume( tool->ammo_required(), tool->bub_pos(), &who );
    } else {
        if( who.is_avatar() ) {
            who.add_msg_if_player( m_bad, _( "Your %1$s ran out of charges." ), tool->tname() );
        } else { // who.is_npc()
            if( get_avatar().sees( who.bub_pos() ) ) {
                add_msg( _( "%1$s %2$s ran out of charges." ), who.disp_name( false,
                         true ), tool->tname() );
            }
        }
        who.cancel_activity();
    }
}

void boltcutting_activity_actor::finish( player_activity &act, Character &who )
{
    map &here = get_map();
    const activity_data_common *data;

    if( here.has_furn( target ) ) {
        const furn_id furn_type = here.furn( target );
        if( !furn_type->boltcut->valid() ) {
            if( !testing ) {
                debugmsg( "%s boltcut is invalid", furn_type.id().str() );
            }
            act.set_to_null();
            return;
        }

        const furn_str_id new_furn = furn_type->boltcut->result();
        if( !new_furn.is_valid() ) {
            if( !testing ) {
                debugmsg( "boltcut furniture: %s invalid furniture", new_furn.str() );
            }
            act.set_to_null();
            return;
        }

        data = static_cast<const activity_data_common *>( &*furn_type->boltcut );
        here.furn_set( target, new_furn );
    } else if( !here.ter( target )->is_null() ) {
        const ter_id ter_type = here.ter( target );
        if( !ter_type->boltcut->valid() ) {
            if( !testing ) {
                debugmsg( "%s boltcut is invalid", ter_type.id().str() );
            }
            act.set_to_null();
            return;
        }

        const ter_str_id new_ter = ter_type->boltcut->result();
        if( !new_ter.is_valid() ) {
            if( !testing ) {
                debugmsg( "boltcut terrain: %s invalid terrain", new_ter.str() );
            }
            act.set_to_null();
            return;
        }

        data = static_cast<const activity_data_common *>( &*ter_type->boltcut );
        here.ter_set( target, new_ter );
    } else {
        if( !testing ) {
            debugmsg( "boltcut activity finished on invalid terrain" );
        }
        act.set_to_null();
        return;
    }
    sound_event se;
    se.origin = target;
    se.volume = 60;
    se.category = sounds::sound_t::combat;
    se.id = "tool";
    se.variant = "boltcutters";
    se.from_player = who.is_player();
    se.from_npc = !se.from_player;
    se.faction = who.get_faction()->id;
    se.monfaction = who.get_faction()->mon_faction;
    if( data->sound().empty() ) {
        se.description = _( "Snick, snick, gachunk!" );
        sounds::sound( se );
    } else {
        se.description = data->sound().translated();
        sounds::sound( se );
    }


    for( const activity_byproduct &byproduct : data->byproducts() ) {
        const int amount = byproduct.roll();
        if( byproduct.item->count_by_charges() ) {
            here.add_item_or_charges( target, item::spawn( byproduct.item, calendar::turn, amount ) );
        } else {
            for( int i = 0; i < amount; ++i ) {
                here.add_item_or_charges( target, item::spawn( byproduct.item, calendar::turn ) );
            }
        }
    }

    if( !data->message().empty() ) {
        who.add_msg_if_player( m_info, data->message().translated() );
    }

    act.set_to_null();
}

void boltcutting_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.member( "tool", tool );

    jsout.end_object();
}

std::unique_ptr<activity_actor> boltcutting_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<boltcutting_activity_actor> actor( new boltcutting_activity_actor(
                tripoint_bub_ms::zero(), safe_reference<item>() ) );

    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    data.read( "target", actor->target );
    data.read( "tool", actor->tool );
    return actor;
}

std::unique_ptr<lockpick_activity_actor> lockpick_activity_actor::use_item(
    int moves_total,
    item &lockpick,
    const tripoint_abs_ms &target
)
{
    return std::unique_ptr<lockpick_activity_actor> ( new lockpick_activity_actor(
                moves_total,
                safe_reference<item>( lockpick ),
                detached_ptr<item>(),
                target
            ) );
}

std::unique_ptr<lockpick_activity_actor> lockpick_activity_actor::use_bionic(
    detached_ptr<item> &&fake_lockpick,
    const tripoint_abs_ms &target
)
{
    return std::unique_ptr<lockpick_activity_actor>( new lockpick_activity_actor(
                to_moves<int>( 5_seconds ),
                safe_reference<item>(),
                std::move( fake_lockpick ),
                target
            ) );
}

void lockpick_activity_actor::start( player_activity &/*act*/, Character & )
{
    const auto target = abs_to_bub( this->target );
    const ter_id ter_type = get_map().ter( target );
    const furn_id furn_type = get_map().furn( target );
    const optional_vpart_position veh = get_map().veh_at( target );
    const auto door_lock = veh.part_with_feature( "DOOR_LOCKING", true );

    if( furn_type != f_null && !furn_type->lockpick_result.is_null() ) {
        progress.emplace( furn_type->name(), moves_total );
    } else if( veh && door_lock ) {
        progress.emplace( veh->vehicle().name, moves_total );
    } else {
        if( ter_type->lockpick_result.is_null() ) {
            debugmsg( "%s lockpick_result is null", ter_type.id().str() );
            return;
        }
        progress.emplace( ter_type->name(), moves_total );
    }
}

void lockpick_activity_actor::do_turn( player_activity &/* act */, Character & )
{
    if( progress.front().complete() ) {
        progress.pop();
        return;
    }
}

void lockpick_activity_actor::finish( player_activity &act, Character &who )
{
    act.set_to_null();

    item *it = nullptr;
    if( lockpick ) {
        it = &*lockpick;
    } else if( fake_lockpick ) {
        it = &*fake_lockpick;
    }

    if( !it ) {
        debugmsg( "Lost ACT_LOCKPICK item" );
        return;
    }

    const auto target = abs_to_bub( this->target );
    const ter_id ter_type = get_map().ter( target );
    const furn_id furn_type = get_map().furn( target );
    const optional_vpart_position veh = get_map().veh_at( target );
    const auto door_lock = veh.part_with_feature( "DOOR_LOCKING", true );

    ter_id new_ter_type = t_null;
    furn_id new_furn_type = f_null;
    std::string open_message = _( "The lock opens…" );

    if( furn_type != f_null ) {
        if( furn_type->lockpick_result.is_null() ) {
            debugmsg( "%s lockpick_result is null", furn_type.id().str() );
            return;
        }

        new_furn_type = furn_type->lockpick_result;
        if( !furn_type->lockpick_message.empty() ) {
            open_message = furn_type->lockpick_message.translated();
        }
    } else if( veh ) {
        if( !door_lock ) {
            debugmsg( "%s has no pickable part", furn_type.id().str() );
            return;
        }
    } else {
        if( ter_type->lockpick_result.is_null() ) {
            debugmsg( "%s lockpick_result is null", ter_type.id().str() );
            return;
        }

        new_ter_type = ter_type->lockpick_result;
        if( !ter_type->lockpick_message.empty() ) {
            open_message = ter_type->lockpick_message.translated();
        }
    }

    bool perfect = it->has_flag( flag_PERFECT_LOCKPICK );
    bool durable = it->has_flag( flag_DURABLE_LOCKPICK );
    bool destroy = false;

    /** @EFFECT_DEX improves chances of successfully picking door lock, reduces chances of bad outcomes */
    /** @EFFECT_MECHANICS improves chances of successfully picking door lock, reduces chances of bad outcomes */
    int pick_roll = 5 *
                    ( std::pow( 1.3, who.get_skill_level( skill_mechanics ) ) +
                      it->get_quality( qual_LOCKPICK ) - it->damage() / 2000.0 ) +
                    who.dex_cur / 4.0;
    int lock_roll = rng( 1, 120 );
    int xp_gain = 0;
    if( perfect || ( pick_roll >= lock_roll ) ) {
        xp_gain += lock_roll;

        if( furn_type != f_null ) {
            get_map().furn_set( target, new_furn_type );
        } else if( door_lock ) {
            door_lock->part().enabled = false;
        } else {
            get_map().ter_set( target, new_ter_type );
        }

        who.add_msg_if_player( m_good, open_message );
    } else if( lock_roll > ( 1.5 * pick_roll ) && !durable ) {
        // damage lockpick on a low result, unless it's durable
        if( it->inc_damage() ) {
            who.add_msg_if_player( m_bad,
                                   _( "The lock stumps your efforts to pick it, and you destroy your tool." ) );
            destroy = true;
        } else {
            who.add_msg_if_player( m_bad,
                                   _( "The lock stumps your efforts to pick it, and you damage your tool." ) );
        }
    } else {
        who.add_msg_if_player( m_bad, _( "The lock stumps your efforts to pick it." ) );
    }

    if( !perfect ) {
        // You don't gain much skill since the item does all the hard work for you
        xp_gain += std::pow( 2, who.get_skill_level( skill_mechanics ) ) + 1;
    }
    who.practice( skill_mechanics, xp_gain );

    if( !perfect
        && ( lock_roll + dice( 1, 30 ) ) > pick_roll ) {

        if( get_map().has_flag( "ALARMED", target ) ) {
            sound_event se;
            se.origin = who.bub_pos();
            se.volume = 90;
            se.category = sounds::sound_t::alarm;
            se.description = _( "an alarm sound!" );
            se.id = "environment";
            se.variant = "alarm";
            sounds::sound( se );
            if( !g->timed_events.queued( TIMED_EVENT_WANTED ) ) {
                g->timed_events.add( TIMED_EVENT_WANTED, calendar::turn + 30_minutes, 0,
                                     who.abs_sm_pos() );
            }
        } else if( veh && veh->vehicle().has_security_working() ) {
            veh->vehicle().is_alarm_on = true;
        }
    }

    if( destroy && lockpick ) {
        lockpick->detach();
    }
}

bool lockpick_activity_actor::is_pickable( const tripoint_bub_ms &p )
{
    const ter_id ter_type = get_map().ter( p );
    const furn_id furn_type = get_map().furn( p );
    const optional_vpart_position veh = get_map().veh_at( p );
    const auto door_lock = veh.part_with_feature( "DOOR_LOCKING", true );

    bool result;
    if( furn_type != f_null ) {
        result = !furn_type->lockpick_result.is_null();
    } else if( door_lock ) {
        result = door_lock.value().part().enabled;
    } else {
        result = !ter_type->lockpick_result.is_null();
    }

    return result;
}

std::optional<tripoint_bub_ms> lockpick_activity_actor::select_location( avatar &you )
{
    if( you.is_mounted() ) {
        you.add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return std::nullopt;
    }

    const std::optional<tripoint_bub_ms> target = choose_adjacent_highlight(
                _( "Use your lockpick where?" ), _( "There is nothing to lockpick nearby." ), is_pickable, false );
    if( !target ) {
        return std::nullopt;
    }

    if( is_pickable( *target ) ) {
        return *target;
    }

    const ter_id terr_type = get_map().ter( *target );
    if( *target == you.bub_pos() ) {
        you.add_msg_if_player( m_info, _( "You pick your nose and your sinuses swing open." ) );
    } else if( g->critter_at<npc>( *target ) ) {
        you.add_msg_if_player( m_info,
                               _( "You can pick your friends, and you can pick your nose, but you can't pick your friend's nose." ) );
    } else if( !terr_type->open.is_null() ) {
        you.add_msg_if_player( m_info, _( "That door isn't locked." ) );
    } else {
        you.add_msg_if_player( m_info, _( "That cannot be picked." ) );
    }
    return std::nullopt;
}

void lockpick_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "moves_total", moves_total );
    jsout.member( "lockpick", lockpick );
    jsout.member( "fake_lockpick", fake_lockpick );
    jsout.member( "target", target );

    jsout.end_object();
}

std::unique_ptr<activity_actor> lockpick_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<lockpick_activity_actor> actor( new lockpick_activity_actor( 0,
            safe_reference<item>(), detached_ptr<item>(), tripoint_abs_ms::zero() ) );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "moves_total", actor->moves_total );
    data.read( "lockpick", actor->lockpick );
    data.read( "fake_lockpick", actor->fake_lockpick );
    data.read( "target", actor->target );

    return actor;
}

void oxytorch_activity_actor::start( player_activity &act, Character &/*who*/ )
{
    const map &here = get_map();

    if( here.has_furn( target ) ) {
        const furn_id furn_type = here.furn( target );
        if( !furn_type->oxytorch->valid() ) {
            if( !testing ) {
                debugmsg( "%s oxytorch is invalid", furn_type.id().str() );
            }
            act.set_to_null();
            return;
        }
        progress.emplace( furn_type->name(), to_moves<int>( furn_type->oxytorch->duration() ) );
    } else if( !here.ter( target )->is_null() ) {
        const ter_id ter_type = here.ter( target );
        if( !ter_type->oxytorch->valid() ) {
            if( !testing ) {
                debugmsg( "%s oxytorch is invalid", ter_type.id().str() );
            }
            act.set_to_null();
            return;
        }
        progress.emplace( ter_type->name(), to_moves<int>( ter_type->oxytorch->duration() ) );
    } else {
        if( !testing ) {
            debugmsg( "oxytorch activity called on invalid terrain" );
        }
        act.set_to_null();
        return;
    }
}

void oxytorch_activity_actor::do_turn( player_activity &/*act*/, Character &who )
{
    // We check available charges when first starting the cut, but this prevents abnormal behavior if torch status changes mid-activity.
    if( tool->ammo_sufficient( &who ) ) {
        tool->ammo_consume( tool->ammo_required(), tool->bub_pos(), &who );
        sfx::play_activity_sound( "tool", "oxytorch", sfx::get_heard_volume( target, 65 ) );
        if( action_time_scale::once_every_this_tick( 2_turns ) ) {
            sound_event se;
            se.origin = target;
            se.volume = 65;
            se.category = sounds::sound_t::destructive_activity;
            se.description = _( "hissssssssss!" );
            se.id = "tool";
            se.variant = "oxytorch";
            se.from_player = who.is_player();
            se.from_npc = !se.from_player;
            se.faction = who.get_faction()->id;
            se.monfaction = who.get_faction()->mon_faction;
            sounds::sound( se );
        }
    } else {
        if( who.is_avatar() ) {
            who.add_msg_if_player( m_bad, _( "Your %1$s ran out of charges." ), tool->tname() );
        } else { // who.is_npc()
            if( get_avatar().sees( who.bub_pos() ) ) {
                add_msg( _( "%1$s %2$s ran out of charges." ), who.disp_name( false,
                         true ), tool->tname() );
            }
        }
        who.cancel_activity();
    }
    if( progress.front().complete() ) {
        progress.pop();
    }
}

void oxytorch_activity_actor::finish( player_activity &act, Character &who )
{
    map &here = get_map();
    const activity_data_common *data;

    if( here.has_furn( target ) ) {
        const furn_id furn_type = here.furn( target );
        if( !furn_type->oxytorch->valid() ) {
            if( !testing ) {
                debugmsg( "%s oxytorch is invalid", furn_type.id().str() );
            }
            act.set_to_null();
            return;
        }

        const furn_str_id new_furn = furn_type->oxytorch->result();
        if( !new_furn.is_valid() ) {
            if( !testing ) {
                debugmsg( "oxytorch furniture: %s invalid furniture", new_furn.str() );
            }
            act.set_to_null();
            return;
        }

        data = static_cast<const activity_data_common *>( &*furn_type->oxytorch );
        here.furn_set( target, new_furn );
    } else if( !here.ter( target )->is_null() ) {
        const ter_id ter_type = here.ter( target );
        if( !ter_type->oxytorch->valid() ) {
            if( !testing ) {
                debugmsg( "%s oxytorch is invalid", ter_type.id().str() );
            }
            act.set_to_null();
            return;
        }

        const ter_str_id new_ter = ter_type->oxytorch->result();
        if( !new_ter.is_valid() ) {
            if( !testing ) {
                debugmsg( "oxytorch terrain: %s invalid terrain", new_ter.str() );
            }
            act.set_to_null();
            return;
        }

        data = static_cast<const activity_data_common *>( &*ter_type->oxytorch );
        here.ter_set( target, new_ter );
    } else {
        if( !testing ) {
            debugmsg( "oxytorch activity finished on invalid terrain" );
        }
        act.set_to_null();
        return;
    }

    for( const activity_byproduct &byproduct : data->byproducts() ) {
        const int amount = byproduct.roll();
        if( byproduct.item->count_by_charges() ) {
            here.add_item_or_charges( target, item::spawn( byproduct.item, calendar::turn, amount ) );
        } else {
            for( int i = 0; i < amount; ++i ) {
                here.add_item_or_charges( target, item::spawn( byproduct.item, calendar::turn ) );
            }
        }
    }

    // 50% chance of starting a fire.
    if( one_in( 2 ) && here.flammable_items_at( target ) ) {
        here.add_field( target, fd_fire, 1, 10_minutes );
    }

    if( !data->message().empty() ) {
        who.add_msg_if_player( m_info, data->message().translated() );
    }

    act.set_to_null();
}

void oxytorch_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.member( "tool", tool );
    jsout.end_object();
}

std::unique_ptr<activity_actor> oxytorch_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<oxytorch_activity_actor> actor( new oxytorch_activity_actor(
                tripoint_bub_ms::zero(), safe_reference<item>() ) );
    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    data.read( "target", actor->target );
    data.read( "tool", actor->tool );
    return actor;
}

void migration_cancel_activity_actor::do_turn( player_activity &act, Character &who )
{
    // Stop the activity
    act.set_to_null();

    // Ensure that neither avatars nor npcs end up in an invalid state
    if( who.is_npc() ) {
        npc &npc_who = dynamic_cast<npc &>( who );
        npc_who.revert_after_activity();
    } else {
        avatar &avatar_who = dynamic_cast<avatar &>( who );
        avatar_who.clear_destination();
        avatar_who.backlog.clear();
    }
}

void migration_cancel_activity_actor::serialize( JsonOut &jsout ) const
{
    // This will probably never be called, but write null to avoid invalid json in
    // the case that it is
    jsout.write_null();
}

std::unique_ptr<activity_actor> migration_cancel_activity_actor::deserialize( JsonIn & )
{
    return std::unique_ptr<migration_cancel_activity_actor>();
}

void toggle_gate_activity_actor::start( player_activity &, Character & )
{
    progress.emplace( "gate", moves_total );
}

void toggle_gate_activity_actor::do_turn( player_activity &, Character & )
{
    if( progress.front().complete() ) {
        progress.pop();
        return;
    }
}

void toggle_gate_activity_actor::finish( player_activity &act, Character & )
{
    gates::toggle_gate( placement );
    act.set_to_null();
}

void toggle_gate_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "moves", moves_total );
    jsout.member( "placement", placement );

    jsout.end_object();
}

std::unique_ptr<activity_actor> toggle_gate_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<toggle_gate_activity_actor> actor( new toggle_gate_activity_actor( 0,
            tripoint_bub_ms::zero() ) );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "moves", actor->moves_total );
    data.read( "placement", actor->placement );

    return actor;
}


stash_activity_actor::stash_activity_actor( Character &ch, const drop_locations &items,
        const tripoint_rel_ms &relpos ) : relpos( relpos )
{
    this->items = pickup::reorder_for_dropping( ch, items );
}

void stash_activity_actor::start( player_activity &, Character & )
{
    // Dummy progress task to indicate ongoing activity
    progress.dummy();
}

void stash_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "items", items );
    jsout.member( "relpos", relpos );

    jsout.end_object();
}

std::unique_ptr<activity_actor> stash_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<stash_activity_actor> actor( new stash_activity_actor() );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "items", actor->items );
    data.read( "relpos", actor->relpos );

    return actor;
}

// ---- insert_item_activity_actor ----
// Transplanted from DDA activity_actor.cpp:8031-8281 (insert_item_activity_actor), adapted to BN
// ownership: item locations are safe_references and the original is detached after the copy went in (T2/T4).

// Cost to take an item from where it is and put it somewhere else (DDA item_move_cost).
static int insert_move_cost( Character &who, item &it, bool bulk_cost )
{
    if( !bulk_cost ) {
        // Cost to take an item from a container or map
        const int obtain_cost = it.obtain_cost( who );
        // Cost to move an item to a container, vehicle or the ground
        const int move_cost = pickup::cost_to_move_item( who, it );
        return obtain_cost + move_cost;
    } else {
        // Pure cost to handling item excluding overhead.
        return std::max( who.item_handling_cost( it, true, 0, -1, true ), 1 );
    }
}

static bool is_bulk_load( const item &lhs, const item &rhs )
{
    if( lhs.where() == rhs.where() && lhs.stacks_with( rhs ) ) {
        switch( lhs.where() ) {
            case item_location_type::container:
                return lhs.parent_item() == rhs.parent_item();
            case item_location_type::map:
            case item_location_type::vehicle:
                return lhs.bub_pos() == rhs.bub_pos();
            default:
                break;
        }
    }
    return false;
}

void insert_item_activity_actor::start( player_activity &act, Character & )
{
    if( items.empty() || !holster ) {
        debugmsg( "ACT_INSERT_ITEM was passed an empty list" );
        act.set_to_null();
        return;
    }

    all_pockets_rigid = holster->all_pockets_rigid();
    // Dummy progress task to indicate ongoing activity
    progress.dummy();
}

static ret_val<void> try_insert( item &holster, item &it, int count, int *charges_added,
                                 Character *carrier, bool allow_fill_charge_item_nested )
{
    ret_val<void> ret = ret_val<void>::make_failure( _( "item can't be stored there" ) );

    if( holster.has_flag( flag_NO_RELOAD ) ) {
        return ret_val<void>::make_failure( _( "destination can't be reloaded" ) );
    }

    if( charges_added == nullptr ) {
        if( it.is_bucket_nonempty() ) {
            debugmsg( "ACT_INSERT_ITEM tried to stash a spillable container without spilling its contents first" );
            return ret_val<void>::make_failure( _( "item would spill" ) );
        }
        ret = holster.can_contain_directly( it );
        if( !ret.success() ) {
            return ret;
        }
        ret = parents_can_contain_recursive( holster, it );
        if( !ret.success() ) {
            if( carrier != nullptr && carrier->is_avatar() && query_yn(
                    _( "The parent container does not have enough space to fit more items.  Would you like to first wield %s to fit more?" ),
                    holster.tname() )
                &&
                // wield the container
                carrier->wield( holster ) ) {
                // and recheck if parent can hold (there should be no parent)
                ret = parents_can_contain_recursive( holster, it );
            }
        }
        if( !ret.success() ) {
            return ret;
        }

        // CDF: NINEFOUR1b, DDA activity_actor.cpp:8122. The put goes first and works on a copy, so a
        // refused put (a gun passes the can_contain_directly gate with its magazine well but has no
        // CONTAINER pocket) leaves the original untouched; do_turn removes the original on success.
        return holster.put_in( it, pocket_type::CONTAINER, /*unseal_pockets=*/true, carrier );
    }

    ret = holster.can_contain_partial_directly( it );
    if( !ret.success() ) {
        return ret;
    }
    ret_val<int> max_parent_charges = max_charges_by_parent_recursive( holster, it );
    if( !max_parent_charges.success() ) {
        return ret_val<void>::make_failure( max_parent_charges.str() );
    } else if( carrier != nullptr && carrier->is_avatar() &&
               max_parent_charges.value() < count ) {
        // if you cannot fit all items because parent container is too small
        if( query_yn(
                _( "The parent container does not have enough space to fit more than %i %s.  Would you like to first wield %s to fit more?" ),
                max_parent_charges.value(), it.tname( max_parent_charges.value() ), holster.tname() )
            &&
            // wield the container
            carrier->wield( holster ) ) {
            // and recalc max_parent_charges
            max_parent_charges = max_charges_by_parent_recursive( holster, it );
        }
    }
    const int charges_to_insert = std::min( count, max_parent_charges.value() );
    *charges_added = holster.fill_with( it, charges_to_insert, /*unseal_pockets=*/true,
                                        /*allow_sealed=*/true, /*ignore_settings*/true, /*into_bottom*/true,
                                        /*allow_nested*/allow_fill_charge_item_nested, carrier );
    if( *charges_added <= 0 ) {
        return ret_val<void>::make_failure( _( "item can't be stored there" ) );
    }

    return ret;
}

void insert_item_activity_actor::do_turn( player_activity &act, Character &who )
{
    bool success = true;
    while( !items.empty() && success && ( who.moves > 0 || who.is_npc() ) ) {
        drop_location holstered_item = items.front();
        items.erase( items.begin() );
        if( !holstered_item.loc || !holster ) {
            // item was lost, so just go to next item
            continue;
        }
        item &it = *holstered_item.loc;
        // Check if next is bulk load before moving item (DDA activity_actor.cpp:8205).
        const bool bulk_load = bulk_next;
        bulk_next = !items.empty() && items.front().loc && is_bulk_load( it, *items.front().loc );
        who.mod_moves( -insert_move_cost( who, it, bulk_load ) );

        Character *carrier = holster->carrying_character();
        ret_val<void> ret = ret_val<void>::make_failure( _( "item can't be stored there" ) );
        if( !it.count_by_charges() ) {
            ret = try_insert( *holster, it, holstered_item.count, nullptr, carrier, false );

            if( ret.success() ) {
                //~ %1$s: item to put in the container, %2$s: container to put item in
                who.add_msg_if_player( string_format( _( "You put your %1$s into the %2$s." ),
                                                      it.display_name(), holster->type->nname( 1 ) ) );
                handler.add_unsealed( *holster );
                handler.unseal_pocket_containing( it );
                // CDF: try_insert already put a copy in (DDA activity_actor.cpp:8183), drop the original.
                it.detach();
            }
        } else {
            int charges_added = 0;
            ret = try_insert( *holster, it, holstered_item.count, &charges_added, carrier,
                              allow_fill_count_by_charge_item_nested );

            if( ret.success() ) {
                detached_ptr<item> copy = item::spawn( it );
                copy->charges = charges_added;
                //~ %1$s: item to put in the container, %2$s: container to put item in
                who.add_msg_if_player( string_format( _( "You put your %1$s into the %2$s." ),
                                                      copy->display_name(), holster->type->nname( 1 ) ) );
                handler.add_unsealed( *holster );
                handler.unseal_pocket_containing( it );
                it.charges -= charges_added;
                if( it.charges == 0 ) {
                    it.detach();
                }
            }
        }

        if( !ret.success() ) {
            success = false;
            std::string error = ret.str().empty() ?
                                //~ %1$s: item we failed to put in the container, %2$s: container to put item in
                                string_format( _( "Could not put %1$s into %2$s." ),
                                               it.tname(), holster->tname() ) :
                                //~ %1$s: item we failed to put in the container, %2$s: container to put item in, %3$s: reason it failed
                                string_format( _( "Could not put %1$s into %2$s, %3$s." ),
                                               it.tname(), holster->tname(), ret.str() );
            who.add_msg_if_player( error );
            items.insert( items.begin(), holstered_item );
        }
    }

    if( items.empty() || !success ) {
        drop_locations items_remain;
        if( who.is_avatar() && reopen_menu && !success ) {
            items_remain = items;
        }
        handler.handle_by( who );
        act.set_to_null();
        if( !items_remain.empty() ) {
            game_menus::inv::insert_items_again( items_remain );
        }
    }
}

void insert_item_activity_actor::canceled( player_activity &, Character &who )
{
    handler.handle_by( who );
}

void insert_item_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "holster", holster );
    jsout.member( "items", items );
    jsout.member( "handler", handler );
    jsout.member( "all_pockets_rigid", all_pockets_rigid );
    jsout.member( "reopen_menu", reopen_menu );
    jsout.member( "allow_fill_charge_item_nested", allow_fill_count_by_charge_item_nested );

    jsout.end_object();
}

std::unique_ptr<activity_actor> insert_item_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<insert_item_activity_actor> actor( new insert_item_activity_actor() );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "holster", actor->holster );
    data.read( "items", actor->items );
    data.read( "handler", actor->handler );
    data.read( "all_pockets_rigid", actor->all_pockets_rigid );
    data.read( "reopen_menu", actor->reopen_menu );
    data.read( "allow_fill_charge_item_nested", actor->allow_fill_count_by_charge_item_nested );

    return actor;
}

// ---- reload_activity_actor ----
// Transplanted from DDA activity_actor.cpp:8283-8533 (reload_activity_actor), adapted to CDF
// ownership: targets are safe_references and the actor's moves live in its progress counter.

reload_activity_actor::reload_activity_actor( item_reload_option &&opt, int extra_moves )
{
    moves_total = opt.moves() + extra_moves;
    quantity = opt.qty();
    pocket_index = opt.pocket_index;
    // CDF: the option holds item *, not item_location (D1).
    target_loc = safe_reference<item>( opt.target );
    ammo_loc = safe_reference<item>( opt.ammo );
    seconds_per_round = opt.qty() ? std::min( 1, moves_total / quantity ) : 0;
    // CDF: itype::id is protected; item::typeId() is the accessor.
    ammo_id = ammo_loc->typeId();
}

bool reload_activity_actor::can_reload() const
{
    if( quantity <= 0 ) {
        debugmsg( "invalid arguments to ACT_RELOAD" );
        return false;
    }

    if( !target_loc ) {
        return false;
    }

    if( !ammo_loc ) {
        return false;
    }

    return true;
}

void reload_activity_actor::start( player_activity &/*act*/, Character &/*who*/ )
{
    // CDF: the actor's progress counter carries the moves (as hacksaw_activity_actor::start does)
    progress.emplace( target_loc ? target_loc->tname() : std::string( "reload" ), moves_total );
}

void reload_activity_actor::reload_msg( Character &who, bool finished )
{
    // Item may have been dropped from inventory between turns (e.g. pocket
    // overflow after the reload increased its size); the location is then
    // stale and dereferencing it would crash.
    if( !target_loc ) {
        return;
    }
    item &reloadable = *target_loc;
    const std::string reloadable_name = reloadable.tname();

    const int qty = finished ? quantity : already_loaded;
    const std::string ammo_name = ammo_id->nname( qty );

    const bool ammo_uses_speedloader = ammo_id->has_flag( flag_SPEEDLOADER );
    const bool ammo_uses_speedloader_clip = ammo_id->has_flag( flag_SPEEDLOADER_CLIP );

    if( reloadable.is_gun() ) {
        if( reloadable.has_flag( flag_RELOAD_ONE ) && !ammo_uses_speedloader &&
            !ammo_uses_speedloader_clip ) {

            add_msg( m_neutral, _( "You insert %dx %s into the %s." ), qty, ammo_name, reloadable_name );
        }
        make_reload_sound( who, reloadable );
    } else if( reloadable.is_watertight_container() ) {
        add_msg( m_neutral, _( "You refill the %s." ), reloadable_name );
    } else {
        add_msg( m_neutral, _( "You reload the %1$s with %2$s." ), reloadable_name, ammo_name );
    }
}

void reload_activity_actor::reload( player_activity &act, Character &who, int load_qty )
{
    if( load_qty == 0 ) {
        return;
    }

    if( !can_reload() ) {
        act.set_to_null();
        return;
    }

    item &reloadable = *target_loc;

    int qty = load_qty ? load_qty : quantity;

    const std::string reloadable_name = reloadable.tname();

    // CDF: is_filthy / flag_FILTHY absent in CDF, filth transfer not ported.
    if( !reloadable.reload( who, *ammo_loc, qty, pocket_index ) ) {
        return;
    }

    if( reloadable.get_var( "dirt", 0 ) > 7800 ) {
        add_msg( m_neutral, _( "You manage to loosen some debris and make your %s somewhat operational." ),
                 reloadable_name );
        reloadable.set_var( "dirt", ( reloadable.get_var( "dirt", 0 ) - rng( 790, 2750 ) ) );
    }

    who.recoil = MAX_RECOIL;

    // Reload may have caused the item to increase in size more than the pocket/location can contain.
    // We want to avoid this because items will be deleted on a save/load.
    // CDF: check_parent_capacity_recursive is absent; "the grown item still fits its parents"
    // is ported as a walk of parent_item() that keeps the outermost ancestor with an overfull
    // pocket. A can_contain( .., parent_it ) walk cannot be used: it rejects every ancestor with
    // "can't put a container into itself" (item_container.cpp:406), also when the pocket has room.
    reloadable.invalidate_pocket_caches_upwards();
    const item *overfull_parent = nullptr;
    for( const item *p = reloadable.parent_item(); p; p = p->parent_item() ) {
        for( const item_pocket *pkt : p->get_pockets( []( const item_pocket & ) {
        return true;
    } ) ) {
            if( pkt->is_overfull() ) {
                overfull_parent = p;
                break;
            }
        }
    }
    if( overfull_parent == nullptr ) {
        return;
    }

    // CDF: CDF owns items through detached_ptr (D1); DDA copies through item_location.
    map &here = get_map();
    const tripoint_bub_ms pos = reloadable.bub_pos();
    detached_ptr<item> d = target_loc->detach();

    // Attempt to put item in another pocket before prompting
    // CDF: try_add leaves d untouched on failure; the item keeps its identity, so target_loc
    // follows it and needs no update (DDA's target_loc = new_loc and loc.remove_item()).
    ret_val<item *> res = who.try_add( std::move( d ), nullptr, nullptr, false );
    if( res.success() ) {
        return;
    }

    // Build prompt
    uilist reload_query;
    const bool wield_check = who.can_wield( *d ).success();
    reload_query.text = string_format( _( "The %s no longer fits in your inventory" ),
                                       reloadable_name );
    if( who.has_wield_conflicts( *d ) ) {
        // CDF: used_weapon() replaces DDA's get_wielded_item().
        reload_query.addentry( 1, wield_check, 'w',
                               _( "Dispose of %s and wield %s" ), who.used_weapon().display_name(),
                               reloadable_name );
    } else {
        reload_query.addentry( 1, wield_check, 'w', _( "Wield %s" ), reloadable_name );
    }
    reload_query.addentry( 2, true, 'd', _( "Drop %s" ), reloadable_name );
    reload_query.query();

    if( reload_query.ret == 1 ) {
        // This case is only reachable if wield_check is true
        // CDF: wield() hands the item back when it was refused, which falls through to the drop.
        d = who.wield( std::move( d ) );
        if( !d ) {
            add_msg( m_neutral, _( "The %s no longer fits in your inventory so you wield it instead." ),
                     reloadable_name );
            return;
        }
    }
    // In player inventory and player is wielding something.
    // CDF: the message goes to who (DDA: loc.carrier()); add_item_or_charges_ret_loc is absent and
    // its location result is unused here; a leftover goes to map::add_item so the item is never lost.
    who.add_msg_if_player( m_neutral,
                           _( "The %s no longer fits in your inventory so you drop it instead." ),
                           reloadable_name );
    d = here.add_item_or_charges( pos, std::move( d ) );
    if( d ) {
        here.add_item( pos, std::move( d ) );
    }
}

void reload_activity_actor::do_turn( player_activity &act, Character &who )
{
    // CDF: progress counter.
    if( seconds_per_round != 0
        && already_loaded < quantity
        && ( progress.get_moves_left() / 100 ) % seconds_per_round == 0 ) {
        reload( act, who );
        already_loaded++;
    }
}

// CDF: P3-6-1b; DDA activity_actor.cpp:8451-8459. DDA calls sounds::ambient_sound, which CDF
// does not have; the sound_event form is the one the old ACT_RELOAD finish handler used.
void reload_activity_actor::make_reload_sound( Character &who, item &reloadable )
{
    if( reloadable.type->gun->reload_noise_volume > 0_dB ) {
        sound_event se;
        se.origin = who.bub_pos();
        se.volume = units::to_decibel( reloadable.type->gun->reload_noise_volume );
        se.category = sounds::sound_t::activity;
        se.description = reloadable.type->gun->reload_noise;
        se.id = "reload";
        se.variant = reloadable.typeId().str();

        sounds::sound( se );
        sfx::play_variant_sound( "reload", reloadable.typeId().str(),
                                 sfx::get_heard_volume( who.bub_pos(), se.volume ) );
    }
}

void reload_activity_actor::finish( player_activity &act, Character &who )
{
    reload( act, who, quantity - already_loaded );
    already_loaded += quantity - already_loaded;
    reload_msg( who, true );
    act.set_to_null();

    // Auto-chain sibling wells so the player is not prompted once per well.
    // Inventory only - map items were not part of the player's original pick
    // and walking them can surface debugmsgs on unrelated nearby items.
    // CDF: P3-6-1b; DDA activity_actor.cpp:8467-8509. act.set_to_null() only nulls the type, and
    // assign_activity pushes the finished activity onto the backlog (never resumed), so this
    // actor is still alive for the reads below.
    if( !target_loc || !target_loc->uses_firing_requirements() ) {
        return;
    }
    item &chain_target = *target_loc;
    // CDF: find_ammo_items_or_mags returns item *, not item_location.
    const std::vector<item *> candidates =
        character_funcs::find_ammo_items_or_mags( who, chain_target, /*empty=*/false, /*radius=*/ -1 );
    if( candidates.empty() ) {
        return;
    }
    int chain_pocket = -1;
    item *chain_ammo = nullptr;
    int idx = 0;
    for( const item_pocket *p : chain_target.get_pockets( []( const item_pocket & ) {
    return true;
} ) ) {
        if( p->is_type( pocket_type::MAGAZINE_WELL ) && p->magazine_current() == nullptr ) {
            item *match = nullptr;
            for( item *cand : candidates ) {
                if( p->can_reload_with( *cand, true ) ) {
                    if( match ) {
                        match = nullptr;
                        break;
                    }
                    match = cand;
                }
            }
            if( match ) {
                chain_pocket = idx;
                chain_ammo = match;
                break;
            }
        }
        ++idx;
    }
    if( !chain_ammo || chain_pocket < 0 ) {
        return;
    }
    const int extra_moves = chain_target.get_var( "dirt", 0 ) > 7800 ? 2500 : 0;
    item_reload_option chain_opt( who.as_player(), &chain_target, &chain_target, *chain_ammo,
                                  chain_pocket );
    who.assign_activity( std::make_unique<player_activity>(
                             std::make_unique<reload_activity_actor>( std::move( chain_opt ), extra_moves ) ),
                         false );
}

void reload_activity_actor::canceled( player_activity &act, Character &who )
{
    who.recoil = MAX_RECOIL;
    act.moves_total = 0;
    act.moves_left = 0;
    reload_msg( who );
}

void reload_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    // CDF: every CDF actor writes its progress counter, as the insert actor does.
    jsout.member( "progress", progress );
    jsout.member( "moves_total", moves_total );
    jsout.member( "qty", quantity );
    jsout.member( "pocket_index", pocket_index );
    jsout.member( "target_loc", target_loc );
    jsout.member( "ammo_loc", ammo_loc );
    jsout.member( "seconds_per_round", seconds_per_round );
    jsout.member( "already_loaded", already_loaded );

    jsout.end_object();
}

// CDF: P3-6-1b; DDA activity_actor.cpp:8535-8556. CDF has no clone() and no JsonValue
// deserializer: the signature is the insert actor's and the actor is built with new.
std::unique_ptr<activity_actor> reload_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<reload_activity_actor> actor( new reload_activity_actor() );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "moves_total", actor->moves_total );
    data.read( "qty", actor->quantity );
    // Saves missing the key route through the first-compatible-well path.
    // TODO(multimag): backwards-compat default; remove after a stable release.
    actor->pocket_index = -1;
    data.read( "pocket_index", actor->pocket_index );
    data.read( "target_loc", actor->target_loc );
    data.read( "ammo_loc", actor->ammo_loc );
    data.read( "seconds_per_round", actor->seconds_per_round );
    data.read( "already_loaded", actor->already_loaded );
    if( actor->ammo_loc ) {
        // CDF: itype::id is protected; item::typeId() is the accessor.
        actor->ammo_id = actor->ammo_loc->typeId();
    }

    return actor;
}

// ---- unload_activity_actor ----
// Transplanted from DDA activity_actor.cpp:6414-6537 (unload_activity_actor), the container branch
// (P3-6-4b) included. CDF: the target is a safe_reference<item> and the actor's moves live in its
// progress counter.

namespace
{
// per DDA activity_actor.cpp:6435-6470: the contents of the unloadable pockets leave a container,
// the pocket types in the order CONTAINER, MAGAZINE_WELL, MAGAZINE, and only the first type that
// changed is emptied by one action
void unload_container( avatar &you, item &target )
{
    contents_change_handler handler;
    bool changed = false;

    for( const pocket_type ptype : {
             pocket_type::CONTAINER,
             pocket_type::MAGAZINE_WELL,
             pocket_type::MAGAZINE
         } ) {
        // the unloadable top items and the pockets they sit in, taken before any of them leaves
        std::vector<std::pair<item *, item_pocket *>> leaving;
        for( item *contained : target.all_items_top( ptype, true ) ) {
            leaving.emplace_back( contained, target.contained_where( *contained ) );
        }
        std::vector<std::pair<item *, item_pocket *>> liquids;
        // CDF: DDA visits the contents and removes them one by one; remove_top_items_with does both
        target.contents.remove_top_items_with( [&]( detached_ptr<item> &&contained ) {
            const auto from = std::ranges::find( leaving, &*contained,
                                                 &std::pair<item *, item_pocket *>::first );
            if( from == leaving.end() ) {
                return std::move( contained );
            }
            if( contained->made_of( LIQUID ) ) {
                // CDF: consume_liquid may open a menu and must not run in the middle of the
                // removal pass: the liquid stays here and is handled after the pass
                liquids.push_back( *from );
                return std::move( contained );
            }
            const int old_charges = contained->charges;
            // CDF: the item that leaves a container must not go back into it (DDA avoid argument)
            contained = avatar_funcs::add_or_drop_with_msg( you, std::move( contained ), true,
                        &target );
            if( !contained || contained->charges != old_charges ) {
                changed = true;
                if( from->second != nullptr ) {
                    from->second->unseal();
                }
                handler.add_unsealed( target );
            }
            return std::move( contained );
        } );

        for( const auto &[liquid, pocket] : liquids ) {
            if( liquid_handler::consume_liquid( *liquid, 1 ) ) {
                changed = true;
                if( pocket != nullptr ) {
                    pocket->unseal();
                }
                handler.add_unsealed( target );
            }
        }

        if( changed ) {
            target.on_contents_changed();
            // CDF: no weight cache to invalidate (DDA invalidate_weight_carried_cache)
            handler.handle_by( you );
            // Warning: handle_by (Character::handle_contents_changed) can invalidate items,
            // `target` among them: neither is used after here
            break;
        }
    }
}
} // namespace

void unload_activity_actor::start( player_activity &, Character & )
{
    // CDF: DDA writes act.moves_left and act.moves_total (activity_actor.cpp:6416-6417)
    progress.emplace( target ? target->tname() : std::string( "unload" ), moves_total );
}

void unload_activity_actor::finish( player_activity &act, Character &who )
{
    // Need to copy `target` here because `::unload` is static and `this` might be invalidated
    // after `act.set_to_null()` has been called (so `this.target` might also be invalidated)
    // (per DDA activity_actor.cpp:6422-6423)
    const safe_reference<item> target_copy = target;
    act.set_to_null();
    // CDF: the target can be destroyed while the activity runs (DDA holds an item_location)
    if( target_copy ) {
        unload( who, *target_copy );
    }
}

void unload_activity_actor::unload( Character &who, item &target )
{
    // CDF: the helper that moves items to the inventory is avatar-only (detached_ptr, avatar_funcs);
    // an NPC never gets this activity
    avatar *const you_ptr = who.as_avatar();
    if( you_ptr == nullptr ) {
        return;
    }
    avatar &you = *you_ptr;

    // CDF: the container test of unload_item (a container, or an item with container pockets)
    if( target.is_container() || !target.get_container_pockets().empty() ) {
        unload_container( you, target );
        return;
    }

    int qty = 0;
    bool actually_unloaded = false;

    // per DDA activity_actor.cpp:6474-6486: the top level items of the CONTAINER, MAGAZINE and
    // MAGAZINE_WELL pockets leave the target, plutonium as cells and not as charges
    std::vector<item *> contents;
    for( const pocket_type ptype : {
             pocket_type::CONTAINER,
             pocket_type::MAGAZINE,
             pocket_type::MAGAZINE_WELL
         } ) {
        for( item *contained : target.all_items_top( ptype ) ) {
            contents.push_back( contained );
        }
    }
    // CDF: DDA visits the contents and removes them afterwards; remove_top_items_with does both
    target.contents.remove_top_items_with( [&]( detached_ptr<item> &&contained ) {
        if( std::ranges::find( contents, &*contained ) == contents.end() ) {
            return std::move( contained );
        }
        if( contained->typeId() == itype_plut_cell_charge ) {
            // CDF: PLUT1. A plutonium pack holds its fuel as one plut_cell_charge stack; the whole
            // stack leaves as charges / 500 plut_cell, as DDA moves the divided stack (the remainder
            // is dropped, as in DDA). Fewer than 500 charges leave nothing.
            const int cells = contained->charges / PLUTONIUM_CHARGES;
            if( cells <= 0 ) {
                return detached_ptr<item>();
            }
            contained = item::spawn( itype_plut_cell, calendar::turn, cells );
        } else if( contained->ammo_type() == ammo_plutonium ) {
            contained->charges /= PLUTONIUM_CHARGES;
        }
        // the item is gone once it has been moved, DDA reads the charges after the move
        const int moved_charges = contained->charges;
        contained = avatar_funcs::add_or_drop_with_msg( you, std::move( contained ), true );
        if( !contained ) {
            qty += moved_charges;
            actually_unloaded = true;
        }
        return std::move( contained );
    } );

    // remove the belt linkage
    if( target.is_ammo_belt() ) {
        if( target.type->magazine && target.type->magazine->linkage ) {
            detached_ptr<item> link = item::spawn( *target.type->magazine->linkage, calendar::turn,
                                                   qty );
            if( !avatar_funcs::add_or_drop_with_msg( you, std::move( link ), true ) ) {
                actually_unloaded = true;
            }
        }
        if( actually_unloaded ) {
            who.add_msg_if_player( _( "You disassemble your %s." ), target.tname() );
        }
    } else if( actually_unloaded ) {
        who.add_msg_if_player( _( "You unload your %s." ), target.tname() );
    }

    if( target.has_flag( flag_MAG_DESTROY ) && target.ammo_remaining() == 0 ) {
        // CDF: detach() for DDA's remove_item(); the target is not used after this
        target.detach();
    }

    who.recoil = MAX_RECOIL;
}

void unload_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    // CDF: every CDF actor writes its progress counter, as the insert and reload actors do
    jsout.member( "progress", progress );
    jsout.member( "moves_total", moves_total );
    jsout.member( "target", target );

    jsout.end_object();
}

std::unique_ptr<activity_actor> unload_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<unload_activity_actor> actor( new unload_activity_actor() );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "moves_total", actor->moves_total );
    data.read( "target", actor->target );

    return actor;
}

void throw_activity_actor::do_turn( player_activity &act, Character &who )
{
    // Make copies of relevant values since the class would
    // not be available after act.set_to_null()
    if( !target ) {
        debugmsg( "Lost weapon while throwing" );
        act.set_to_null();
        return;
    }

    item *it = &*target;
    std::optional<tripoint_bub_ms> blind_throw_pos = blind_throw_from_pos;

    // Stop the activity. Whether we will or will not throw doesn't matter.
    act.set_to_null();
    if( !who.is_avatar() ) {
        // Sanity check
        debugmsg( "ACT_THROW is not applicable for NPCs." );
        return;
    }

    // Shift our position to our peeking position so the target UI can see from there.
    const auto original_player_position = who.abs_pos();
    if( blind_throw_pos ) {
        who.setpos( bub_to_abs( *blind_throw_pos ) );
    }

    target_handler::trajectory trajectory = target_handler::mode_throw( *who.as_avatar(), *it,
                                            blind_throw_pos.has_value() );

    // If we previously shifted our position, put ourselves back now that we've picked our target.
    if( blind_throw_pos ) {
        who.setpos( original_player_position );
    }

    if( trajectory.empty() ) {
        return;
    }

    if( it != &who.primary_weapon() ) {
        // This is to represent "implicit offhand wielding"
        int extra_cost = who.item_handling_cost( *it, true, INVENTORY_HANDLING_PENALTY / 2 );
        who.mod_moves( -extra_cost );
    }
    detached_ptr<item> det = target->split( 1 );
    ranged::throw_item( who, trajectory.back(), std::move( det ), blind_throw_pos );
}

void throw_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "target_loc", target );
    jsout.member( "blind_throw_from_pos", blind_throw_from_pos );

    jsout.end_object();
}

std::unique_ptr<activity_actor> throw_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<throw_activity_actor> actor( new throw_activity_actor() );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "target_loc", actor->target );
    data.read( "blind_throw_from_pos", actor->blind_throw_from_pos );

    return actor;
}


// ---- craft_activity_actor ----

craft_activity_actor::craft_activity_actor(
    const recipe *rec,
    int batch_size,
    int craft_counter,
    const tripoint_abs_ms &location,
    std::vector<comp_selection<item_comp>> item_selections,
    std::vector<comp_selection<tool_comp>> tool_selections,
    bool tools_prepaid,
    bool is_long
) : rec( rec ), batch_size( batch_size ), craft_counter( craft_counter ),
    location( location ),
    item_selections( std::move( item_selections ) ),
    tool_selections( std::move( tool_selections ) ),
    tools_prepaid( tools_prepaid ),
    is_long( is_long ),
    is_valid( rec != nullptr )
{}

auto craft_activity_actor::find_in_progress_craft( const player_activity &act,
        Character &who ) const -> item * // *NOPAD*
{
    if( !act.targets.empty() && act.targets.front() && act.targets.front()->is_craft() &&
        &act.targets.front()->get_making() == rec ) {
        return &*act.targets.front();
    }

    item *result = nullptr;
    who.visit_items( [&]( item * it ) {
        if( it->is_craft() && &it->get_making() == rec ) {
            result = it;
            return VisitResponse::ABORT;
        }
        return VisitResponse::NEXT;
    } );
    if( result ) {
        return result;
    }
    // If not in inventory, check the map at the crafter's feet — set_item_inventory
    // may have placed it there if the NPC was over their carry capacity.
    map_selector sel( who.bub_pos(), 0 );
    sel.visit_items( [&]( item * it ) {
        if( it->is_craft() && &it->get_making() == rec ) {
            result = it;
            return VisitResponse::ABORT;
        }
        return VisitResponse::NEXT;
    } );
    return result;
}

void craft_activity_actor::calc_all_moves( player_activity &act, Character &who )
{
    if( !rec || !is_valid ) {
        act.set_to_null();
        return;
    }

    const int current_turn = to_turn<int>( calendar::turn );

    // Catch-up: apply time elapsed while NPC was outside the reality bubble.
    // last_turn_nr >= 0 means start() already ran in a previous session.
    if( last_turn_nr >= 0 && current_turn > last_turn_nr ) {
        item *craft_item = find_in_progress_craft( act, who );
        if( craft_item ) {
            const int elapsed_turns = current_turn - last_turn_nr;
            const double base_total_moves = std::max( 1, rec->batch_time( batch_size, 1.0f, 0 ) );
            // No live crafting modifiers are applied while outside the reality bubble.
            const auto moves_elapsed = action_time_scale::activity_progress_for_turns( elapsed_turns );
            const int old_counter = craft_item->get_counter();
            const int new_counter = static_cast<int>( std::min(
                                        old_counter + moves_elapsed / base_total_moves * 10'000'000.0,
                                        10'000'000.0 ) );
            craft_item->set_counter( new_counter );
            craft_counter = new_counter;

            const int five_percent_steps = new_counter / 500'000 - old_counter / 500'000;
            if( five_percent_steps > 0 ) {
                who.craft_skill_gain( *craft_item, five_percent_steps );
            }

            // Re-build progress counter to match updated craft state
            const int remaining = std::max( 0, static_cast<int>(
                                                base_total_moves * ( 1.0 - new_counter / 10'000'000.0 ) ) );
            if( !activity_actor::progress.empty() ) {
                activity_actor::progress.mod_moves_left(
                    remaining - activity_actor::progress.get_moves_left() );
            } else {
                activity_actor::progress.emplace( craft_item->tname(),
                                                  static_cast<int>( base_total_moves ), remaining );
            }

            if( new_counter >= 10'000'000 ) {
                // Drain so complete() fires on the next do_turn check
                activity_actor::progress.mod_moves_left( -activity_actor::progress.get_moves_left() );
            }
        }
    }

    last_turn_nr = current_turn;

    // Re-build progress counter after deserialization if catch-up didn't already do it
    if( activity_actor::progress.empty() ) {
        item *craft_item = find_in_progress_craft( act, who );
        const std::string name = craft_item ? craft_item->tname() : rec->result_name();
        const int base_total = std::max( 1, rec->batch_time( batch_size, 1.0f, 0 ) );
        const int remaining = std::max( 1, static_cast<int>(
                                            base_total * ( 1.0 - craft_counter / 10'000'000.0 ) ) );
        activity_actor::progress.emplace( name, base_total, remaining );
    }

    item *craft_item = find_in_progress_craft( act, who );
    if( craft_item ) {
        refresh_speed( act, who, *craft_item );
    }
}

void craft_activity_actor::refresh_speed( player_activity &act, const Character &who,
        const item &craft_item, std::optional<bench_location> bench ) const
{
    const bench_location resolved_bench = bench ? *bench : find_best_bench( who, craft_item );
    const recipe &making = *rec;
    const float tools_mult = cached_tools_mult != 0.0f ? cached_tools_mult
                             : crafting_tools_speed_multiplier( who, making );
    act.speed.light        = lighting_crafting_speed_multiplier( who, making );
    act.speed.bench_factor = workbench_crafting_speed_multiplier( craft_item, resolved_bench );
    act.speed.morale       = morale_crafting_speed_multiplier( who, making );
    act.speed.tools        = tools_mult;
    act.speed.player_speed = who.get_speed() / 100.0f;
    const int assistants   = who.available_assistant_count( making );
    if( assistants > 0 ) {
        const double base_no_assist   = std::max( 1, making.batch_time( batch_size, 1.0f, 0 ) );
        const double base_with_assist = std::max( 1, making.batch_time( batch_size, 1.0f, assistants ) );
        act.speed.assist = static_cast<float>( base_no_assist / base_with_assist );
    } else {
        act.speed.assist = 1.0f;
    }
    // Mutation and game-option multipliers have no dedicated speed field; fold them
    // into skills so act.speed.total() matches the actual crafting rate.
    const float mutation_mult = who.mutation_value( "crafting_speed_modifier" );
    const float game_opt_mult = get_option<int>( "CRAFTING_SPEED_MULT" ) == 0
                                ? 9999.0f
                                : 100.0f / static_cast<float>( get_option<int>( "CRAFTING_SPEED_MULT" ) );
    act.speed.skills = mutation_mult * game_opt_mult;
}

void craft_activity_actor::start( player_activity &act, Character &who )
{
    if( !rec || !is_valid ) {
        act.set_to_null();
        return;
    }

    item *craft_item = find_in_progress_craft( act, who );
    if( !craft_item ) {
        who.add_msg_player_or_npc(
            _( "You lost your in progress %s and had to stop crafting." ),
            _( "<npcname> lost the in progress %s and had to stop crafting." ),
            rec->result_name() );
        act.set_to_null();
        return;
    }

    cached_tools_mult = crafting_tools_speed_multiplier( who, *rec );
    craft_counter = craft_item->get_counter();
    last_turn_nr = to_turn<int>( calendar::turn );  // mark fresh start so calc_all_moves skips catch-up
    const int base_total = std::max( 1, rec->batch_time( batch_size, 1.0f, 0 ) );
    const int remaining = craft_counter == 0
                          ? base_total
                          : std::max( 1, static_cast<int>( base_total * ( 1.0 - craft_counter / 10'000'000.0 ) ) );
    activity_actor::progress.emplace( craft_item->tname(), base_total, remaining );
}

void craft_activity_actor::do_turn( player_activity &act, Character &who )
{
    if( !rec || !is_valid ) {
        act.set_to_null();
        return;
    }

    item *craft_item = find_in_progress_craft( act, who );
    if( !craft_item ) {
        who.add_msg_player_or_npc(
            _( "You no longer have the in progress craft in your possession.  "
               "You stop crafting.  "
               "Reactivate the in progress craft to continue crafting." ),
            _( "<npcname> no longer has the in progress craft in their possession.  "
               "<npcname> stops crafting." ) );
        act.set_to_null();
        return;
    }

    const recipe &making = *rec;
    if( cached_tools_mult == 0.0f ) {
        cached_tools_mult = crafting_tools_speed_multiplier( who, making );
    }
    const bench_location bench = find_best_bench( who, *craft_item );
    refresh_speed( act, who, *craft_item, bench );
    const float crafting_speed = crafting_speed_multiplier( who, *craft_item, bench, act.speed.tools );
    const int assistants = who.available_assistant_count( making );

    if( crafting_speed <= 0.0f ) {
        who.add_msg_player_or_npc( m_bad,
                                   _( "You cannot continue crafting." ),
                                   _( "<npcname> cannot continue crafting." ) );
        act.set_to_null();
        return;
    }

    const int old_counter = craft_item->get_counter();
    const double base_total_moves = std::max( 1, making.batch_time( batch_size, 1.0f, 0 ) );
    const double cur_total_moves = std::max( 1, making.batch_time( batch_size, crafting_speed,
                                   assistants ) );
    const auto scaled_moves = action_time_scale::activity_progress_from_actor_moves( who );
    const auto delta_progress = scaled_moves * base_total_moves / cur_total_moves;
    const double current_progress = old_counter * base_total_moves / 10'000'000.0 + delta_progress;
    const int new_counter = static_cast<int>( std::min(
                                std::round( current_progress / base_total_moves * 10'000'000.0 ),
                                10'000'000.0 ) );
    const int five_percent_steps = new_counter / 500'000 - old_counter / 500'000;
    craft_item->set_counter( new_counter );
    craft_counter = new_counter;

    who.set_moves( 0 );

    if( five_percent_steps > 0 ) {
        who.craft_skill_gain( *craft_item, five_percent_steps );

        if( !tools_prepaid && !who.craft_consume_tools( *craft_item, five_percent_steps, false ) ) {
            act.set_to_null();
            return;
        }
    }

    // Keep the progress_counter in sync so the UI shows correct values
    if( !activity_actor::progress.empty() ) {
        const int new_moves_left = static_cast<int>(
                                       base_total_moves * ( 1.0 - static_cast<double>( new_counter ) / 10'000'000.0 ) );
        const int delta = new_moves_left - activity_actor::progress.get_moves_left();
        if( delta != 0 ) {
            activity_actor::progress.mod_moves_left( delta );
        }
    }

    last_turn_nr = to_turn<int>( calendar::turn );

    if( new_counter >= 10'000'000 ) {
        // Signal completion so player_activity::do_turn calls finish()
        if( !activity_actor::progress.empty() ) {
            activity_actor::progress.mod_moves_left( -activity_actor::progress.get_moves_left() );
        }
    } else if( new_counter >= craft_item->get_next_failure_point() ) {
        const bool destroy = craft_item->handle_craft_failure( who );
        if( destroy ) {
            who.add_msg_player_or_npc(
                _( "There is nothing left of the %s to craft from." ),
                _( "There is nothing left of the %s <npcname> was crafting." ),
                craft_item->tname() );
            craft_item->detach();
            act.set_to_null();
        }
        // If !destroy, handle_craft_failure may have called cancel_activity already
    }
}

void craft_activity_actor::finish( player_activity &act, Character &who )
{
    act.set_to_null();
    do_complete_craft( act, who );
}

void craft_activity_actor::do_complete_craft( player_activity &act, Character &who )
{
    item *craft_item = find_in_progress_craft( act, who );
    if( !craft_item ) {
        debugmsg( "craft_activity_actor::do_complete_craft: no craft item found for %s",
                  rec ? rec->result_name() : "unknown" );
        return;
    }
    ::complete_craft( who, *craft_item );
    craft_item->detach();
    if( is_long && rec ) {
        if( who.making_would_work( rec->ident(), batch_size ) ) {
            who.last_craft->execute( abs_to_bub( location ) );
        }
    }
}

act_progress_message craft_activity_actor::get_progress_message(
    const player_activity &act, const Character &who ) const
{
    if( !rec || !is_valid ) {
        return act_progress_message::make_empty();
    }

    const int assistants = who.available_assistant_count( *rec );
    const double base_total_moves = std::max( 1, rec->batch_time( batch_size, 1.0f, 0 ) );
    const double remaining_pct = 1.0 - craft_counter / 10'000'000.0;
    const auto total_mult = act.speed.total();
    const auto remaining_moves = static_cast<int>( std::ceil( remaining_pct * base_total_moves ) );
    const auto remaining_turns = action_time_scale::turns_for_progress( remaining_moves,
                                 act.speed.calendar_moves_per_turn() );

    const std::string time_desc = string_format( _( "Time left: %s" ),
                                  to_string( time_duration::from_turns( remaining_turns ) ) );

    const auto fmt_spd = [&]( float level, const std::string & name ) -> std::string {
        const int pct = static_cast<int>( level * 100 );
        if( pct == 100 )
        {
            return "";
        }
        nc_color col = pct > 100 ? c_green : c_red;
        return string_format( " - %s: %s\n", name,
                              colorize( std::to_string( pct ) + '%', col ) );
    };

    std::string mults_desc = _( "Crafting speed multipliers:\n" );
    const int total_pct = static_cast<int>( total_mult * 100 );
    nc_color total_col = total_pct > 100 ? c_green : c_red;
    mults_desc += string_format( " - %s: %s\n", _( "Total" ),
                                 colorize( std::to_string( total_pct ) + '%', total_col ) );
    mults_desc += fmt_spd( act.speed.player_speed, _( "Speed" ) );
    mults_desc += fmt_spd( act.speed.light, pgettext( "crafting speed multiplier", "Light" ) );
    mults_desc += fmt_spd( act.speed.bench_factor, _( "Workbench" ) );
    mults_desc += fmt_spd( act.speed.morale, _( "Morale" ) );
    mults_desc += fmt_spd( act.speed.tools, _( "Tools" ) );
    if( assistants > 0 ) {
        mults_desc += fmt_spd( act.speed.assist, _( "Assistants" ) );
    }

    return act_progress_message::make_full(
               string_format( _( "%s: %s\n\n%s\n\n%s" ),
                              act.get_verb().translated(), rec->result_name(),
                              time_desc, mults_desc ) );
}

void craft_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", activity_actor::progress );
    jsout.member( "recipe", rec ? rec->ident().str() : std::string() );
    jsout.member( "batch_size", batch_size );
    jsout.member( "craft_counter", craft_counter );
    jsout.member( "location", location );
    jsout.member( "item_selections", item_selections );
    jsout.member( "tool_selections", tool_selections );
    jsout.member( "tools_prepaid", tools_prepaid );
    jsout.member( "is_long", is_long );
    jsout.member( "last_turn_nr", last_turn_nr );
    jsout.end_object();
}

std::unique_ptr<activity_actor> craft_activity_actor::deserialize( JsonIn &jsin )
{
    auto actor = std::make_unique<craft_activity_actor>();
    JsonObject data = jsin.get_object();

    data.read( "progress", actor->activity_actor::progress );
    std::string recipe_str;
    data.read( "recipe", recipe_str );
    if( !recipe_str.empty() ) {
        const recipe_id rid( recipe_str );
        if( rid.is_valid() ) {
            actor->rec = &*rid;
            actor->is_valid = true;
        }
    }
    data.read( "batch_size", actor->batch_size );
    data.read( "craft_counter", actor->craft_counter );
    data.read( "location", actor->location );
    data.read( "item_selections", actor->item_selections );
    data.read( "tool_selections", actor->tool_selections );
    data.read( "tools_prepaid", actor->tools_prepaid );
    data.read( "is_long", actor->is_long );
    data.read( "last_turn_nr", actor->last_turn_nr );

    return actor;
}

inline void construction_activity_actor::calc_all_moves( player_activity &act, Character &who )
{
    // Check if pc was lost for some reason, but actually still exists on map, e.g. save/load
    if( !pc ) {
        map &here = get_map();
        auto local = abs_to_bub( target );
        pc = here.partial_con_at( tripoint_bub_ms( local ) );
    }
    //if something goes terribly wrong we don't CTD
    if( !pc ) {
        act.set_to_null();
        return;
    }
    auto reqs = activity_reqs_adapter( *pc->id );
    act.speed.calc_all_moves( who, reqs );
}

void construction_activity_actor::start( player_activity &/*act*/, Character &/*who*/ )
{
    map &here = get_map();
    auto local = abs_to_bub( target );
    pc = here.partial_con_at( tripoint_bub_ms( local ) );
    auto &built = *pc->id;

    std::string name;

    if( pc->id == deconstruct || pc->id == deconstruct_simple ||
        built.group == advanced_object_deconstruction ) {
        if( here.has_furn( local ) ) {
            const furn_id furn_type = here.furn( local );
            name = furn_type->name();
        } else if( !here.ter( local )->is_null() ) {
            const ter_id ter_type = here.ter( local );
            name = ter_type->name();
        }
    } else {
        name = built.post_furniture.is_empty()
               ? ""
               : built.post_furniture->name();
        name = built.post_terrain.is_empty()
               ? name
               : built.post_terrain->name();
    }

    int total_time = std::max( 1, built.adjusted_time() );
    int left = pc->counter == 0
               ? total_time
               : total_time - pc->counter / 10'000'000.0 * total_time;

    progress.emplace( name, total_time, left );
}

void construction_activity_actor::do_turn( player_activity &act, Character &who )
{
    // Check if pc was lost for some reason, but actually still exists on map, e.g. save/load
    if( !pc ) {
        map &here = get_map();
        auto local = abs_to_bub( target );
        pc = here.partial_con_at( tripoint_bub_ms( local ) );
    }

    // Maybe the player and the NPC are working on the same construction at the same time or toubles during load
    if( !pc ) {
        act.set_to_null();
        add_msg( m_info, _( "%s did not find an unfinished construction at the activity spot." ),
                 who.disp_name() );
        return;
    }

    pc->counter = progress.front().to_counter();

    if( progress.front().complete() ) {
        progress.pop();
        return;
    } else {
        auto &built = *pc->id;
        if( !who.has_trait( trait_DEBUG_HS ) && !who.meets_skill_requirements( built ) ) {
            add_msg( m_info, _( "%s can't work on this construction anymore." ), who.disp_name() );
            act.set_to_null();
            return;
        }
    }
}

void construction_activity_actor::finish( player_activity &act, Character &who )
{
    complete_construction( who, target );
    act.set_to_null();
}

void construction_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.end_object();
}

std::unique_ptr<activity_actor> construction_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<construction_activity_actor> actor( new construction_activity_actor(
                tripoint_abs_ms( tripoint_zero ) ) );
    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    data.read( "target", actor->target );
    return actor;
}

void assist_activity_actor::start( player_activity &/*act*/, Character &/*who*/ )
{
    progress.dummy();
}

void assist_activity_actor::serialize( JsonOut &jsout ) const
{
    // Activity is not being saved but still provide some valid json if called.
    jsout.write_null();
}

std::unique_ptr<activity_actor> assist_activity_actor::deserialize( JsonIn & )
{
    return std::make_unique<assist_activity_actor>();
}

std::unique_ptr<activity_actor> salvage_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<salvage_activity_actor> actor( new salvage_activity_actor() );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "targets", actor->targets );
    data.read( "pos", actor->pos );
    data.read( "mute_prompts", actor->mute_prompts );

    return actor;
}

// ---- enchant_activity_actor ----

void enchant_activity_actor::start( player_activity &act, Character & )
{
    if( !target ) {
        debugmsg( "Lost object being enchanted" );
        act.set_to_null();
        return;
    }
    const std::string name = string_format( "Enchant %s", target->display_name() );
    progress.emplace( name, moves_total );
}

void enchant_activity_actor::do_turn( player_activity &act, Character & )
{
    if( !target ) {
        debugmsg( "Lost object being enchanted" );
        act.set_to_null();
        return;
    }

    if( progress.front().complete() ) {
        progress.pop();
        return;
    }
}

void enchant_activity_actor::finish( player_activity &act, Character &who )
{
    if( !target ) {
        debugmsg( "Lost object being enchanted" );
        act.set_to_null();
        return;
    }
    if( !furn.is_valid() ) {
        debugmsg( "The furniture that was used is invalid" );
        act.set_to_null();
        return;
    }
    enchant_info info;
    bool found_info = false;
    for( const enchant_info &ench_info : furn->enchanter ) {
        if( ench_info.id == enchanter_id ) {
            info = ench_info;
            found_info = true;
            break;
        }
    }
    if( !found_info ) {
        debugmsg( "The enchantment could not be found in furniture definition." );
        act.set_to_null();
        return;
    }

    auto total_reqs =
        enchanter::total_requirements( info )
        * std::max(
            1, ( info.volume_batch_effect ? int( target->base_volume() / info.volume_per_batch ) : 1 ) );
    for( const auto &comp : total_reqs.get_components() ) { who.consume_items( comp ); }
    for( const auto &comp : total_reqs.get_tools() ) { who.consume_tools( comp ); }
    target->add_enchantment( info.to_enchant_with );
    target->set_var( info.count_var, target->get_var<int>( info.count_var, 0 ) + 1 );
    if( info.applied_flag_id.is_valid() ) { target->set_flag( info.applied_flag_id ); }
    act.set_to_null();
}

void enchant_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "target_obj", target );
    jsout.member( "furn", furn );
    jsout.member( "enchanter_id", enchanter_id );

    jsout.end_object();
}

std::unique_ptr<activity_actor> enchant_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<enchant_activity_actor> actor( new enchant_activity_actor() );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "target_obj", actor->target );
    data.read( "furn", actor->furn );
    data.read( "enchanter_id", actor->enchanter_id );

    return actor;
}

// CDF: E1-1 declared the e-file actors in activity_actor_definitions.h, E1-5a filled the bookbinder
// (DDA activity_actor.cpp:1130-1200) and ebooksave (:3611-3770) actors, E1-5b the efile actor
// (:3828-4436), E1-5c the port compatibility, the transfer rate, the external transfer storage and
// the recipe catalog and photo gallery merge. The iuse functions that create the actors (E1-7)
// are stubs until then.

namespace io
{
template<>
std::string enum_to_string<efile_action>( efile_action data )
{
    // *INDENT-OFF*
    switch( data ) {
        case efile_action::EF_BROWSE: return "EF_BROWSE";
        case efile_action::EF_READ: return "EF_READ";
        case efile_action::EF_MOVE_FROM_THIS: return "EF_MOVE_FROM_THIS";
        case efile_action::EF_MOVE_ONTO_THIS: return "EF_MOVE_ONTO_THIS";
        case efile_action::EF_COPY_FROM_THIS: return "EF_COPY_FROM_THIS";
        case efile_action::EF_COPY_ONTO_THIS: return "EF_COPY_ONTO_THIS";
        case efile_action::EF_WIPE: return "EF_WIPE";
        case efile_action::EF_INVALID: return "EF_INVALID";
        case efile_action::EF_ACTION_COUNT: break;
    }
    // *INDENT-ON*
    debugmsg( "Invalid efile_action" );
    abort();
}

template<>
std::string enum_to_string<efile_combo>( efile_combo data )
{
    // *INDENT-OFF*
    switch( data ) {
        case efile_combo::COMBO_MOVE_ONTO_BROWSE: return "COMBO_MOVE_ONTO_BROWSE";
        case efile_combo::COMBO_NONE: return "COMBO_NONE";
        case efile_combo::EFILE_COMBO_COUNT: break;
    }
    // *INDENT-ON*
    debugmsg( "Invalid efile_combo" );
    abort();
}
} // namespace io

int bookbinder_copy_activity_actor::pages_for_recipe( const recipe &r )
{
    return 1 + r.difficulty / 2;
}

bool bookbinder_copy_activity_actor::can_resume_with_internal( const activity_actor &other,
        const Character & ) const
{
    const bookbinder_copy_activity_actor &act = static_cast<const bookbinder_copy_activity_actor &>( other );
    return rec_id == act.rec_id && book_binder == act.book_binder;
}

void bookbinder_copy_activity_actor::start( player_activity &act, Character & )
{
    // CDF: item_location in DDA, safe_reference here
    if( !book_binder || !rec_id.is_valid() ) {
        act.set_to_null();
        return;
    }
    pages = bookbinder_copy_activity_actor::pages_for_recipe( *rec_id );
    // CDF: DDA sets act.moves_total and act.moves_left; CDF actors keep time in progress.
    progress.emplace( rec_id->result_name(), to_moves<int>( pages * 10_minutes ) );
}

void bookbinder_copy_activity_actor::do_turn( player_activity &, Character &p )
{
    // CDF: item_location in DDA, safe_reference here
    if( !book_binder ) {
        p.cancel_activity();
        return;
    }
    if( character_funcs::fine_detail_vision_mod( p ) > 4.0f ) {
        p.cancel_activity();
        p.add_msg_if_player( m_info, _( "It's too dark to write!" ) );
        return;
    }
}

void bookbinder_copy_activity_actor::finish( player_activity &act, Character &p )
{
    // CDF: item_location in DDA, safe_reference here
    if( !book_binder ) {
        act.set_to_null();
        return;
    }

    detached_ptr<item> paper_sheets = item::spawn( itype_paper, calendar::turn, pages );
    if( !book_binder->can_contain( *paper_sheets ).success() ) {
        debugmsg( "Book binder can not contain '%s' recipe when it should.", rec_id.str() );
        act.set_to_null();
        return;
    }

    std::set<recipe_id> recipes = book_binder->get_saved_recipes();

    if( recipes.emplace( rec_id ).second ) { // if .second is true then recipe was inserted
        book_binder->set_saved_recipes( recipes );
        p.add_msg_if_player( m_good, _( "You copy the recipe for %1$s into your recipe book." ),
                             rec_id->result_name() );

        p.use_charges( itype_paper, pages );

        // CDF: items_with returns std::vector<item *>, DDA returns const item *.
        const std::vector<item *> writing_tools_filter =
            p.crafting_inventory().items_with( [&]( const item & it ) {
            // Match the local-only crafting drain so the filter doesn't pick
            // a multimag tool that ammo_sufficient counts via UPS/bionic.
            const bool enough = it.uses_firing_requirements()
                                ? ( !it.needs_charges_to_use() || it.tool_uses_remaining_local() > 0 )
                                : it.ammo_sufficient( &p );
            return it.has_flag( flag_WRITE_MESSAGE ) && enough;
        } );

        std::vector<tool_comp> writing_tools;
        writing_tools.reserve( writing_tools_filter.size() );
        for( const item *tool : writing_tools_filter ) {
            writing_tools.emplace_back( tool->typeId(), 1 );
        }

        p.consume_tools( writing_tools, pages );
        // CDF: item::spawn with the detached_ptr put_in overload.
        book_binder->put_in( item::spawn( itype_paper, calendar::turn, pages ),
                             pocket_type::MAGAZINE );
    } else {
        debugmsg( "Recipe book already has '%s' recipe when it should not.", rec_id.str() );
    }

    act.set_to_null();
}

void bookbinder_copy_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "book_binder", book_binder );
    jsout.member( "rec_id", rec_id );
    jsout.member( "pages", pages );
    jsout.end_object();
}

// CDF: DDA's deserialize (activity_actor.cpp:1198-1215) reads lockpick fields and forgets rec_id
// and pages; read the members that serialize writes.
std::unique_ptr<activity_actor> bookbinder_copy_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<bookbinder_copy_activity_actor> actor =
        std::make_unique<bookbinder_copy_activity_actor>();

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "book_binder", actor->book_binder );
    data.read( "rec_id", actor->rec_id );
    data.read( "pages", actor->pages );

    return actor;
}

ebooksave_activity_actor::ebooksave_activity_actor( const std::vector<item *> &books_to_scan,
        item *reader ) : ereader( reader )
{
    for( item *book : books_to_scan ) {
        books.emplace_back( book );
    }
}

namespace
{
// CDF: the members are safe_references, the DDA statics take items; only valid books count
auto live_books( const std::vector<safe_reference<item>> &refs ) -> std::vector<item *>
{
    std::vector<item *> result;
    for( const safe_reference<item> &ref : refs ) {
        if( ref ) {
            result.push_back( ref.get() );
        }
    }
    return result;
}
} // namespace

int ebooksave_activity_actor::total_pages( const std::vector<item *> &books )
{
    int pages = 0;
    for( const item *book : books ) {
        pages += item::pages_in_book( *book->type );
    }
    return pages;
}

time_duration ebooksave_activity_actor::required_time( const std::vector<item *> &books )
{
    return total_pages( books ) * time_per_page;
}

// TODO(multimag): UI estimate uses ammo_required for legacy compatibility;
// for multimag conversions of e-readers this would need a uses-based formula.
int ebooksave_activity_actor::required_charges( const std::vector<item *> &books,
        const item *ereader )
{
    const int charges = total_pages( books ) / pages_per_charge;
    return charges * ereader->ammo_required();
}

void ebooksave_activity_actor::start( player_activity &act, Character & )
{
    // CDF: item_location in DDA, safe_reference here
    if( !ereader ) {
        act.set_to_null();
        return;
    }
    const time_duration scanning_time = required_time( live_books( books ) );
    // CDF: DDA sets act.moves_total; CDF actors keep time in progress.
    progress.emplace( ereader->type_name(), to_moves<int>( scanning_time ) );
    start_scanning_next_book( act );
}

void ebooksave_activity_actor::do_turn( player_activity &act, Character &who )
{
    // CDF: item_location in DDA, safe_reference here
    if( !ereader ) {
        who.cancel_activity();
        return;
    }
    if( books.empty() ) {
        // CDF: nothing left to scan (the list was empty or every book vanished): the progress is
        // zero, so finish() ends the activity. DDA calls books.back() on the empty list here.
        return;
    }
    // only consume charges every pages_per_charge pages
    if( action_time_scale::once_every_this_tick( pages_per_charge * time_per_page ) ) {
        if( !ereader->ammo_sufficient( &who ) ) {
            // CDF: no add_msg_if_player_sees
            who.add_msg_player_or_npc( _( "Your %s ran out of batteries." ),
                                       _( "<npcname>'s %s ran out of batteries." ),
                                       item::nname( ereader->typeId() ) );
            who.cancel_activity();
            return;
        }

        ereader->consume_tool_uses( 1, get_map(), who.bub_pos(), &who );
    }

    turns_left_on_current_book--;
    if( turns_left_on_current_book > 0 ) {
        return; // not fully scanned yet
    }

    // We have now spent enough time to fully scan current book
    completed_scanning_current_book( act, who );
}

void ebooksave_activity_actor::finish( player_activity &act, Character &who )
{
    if( !ereader ) {
        // CDF: the reader vanished (item_location in DDA, safe_reference here): the books cannot
        // be stored any more, so drop them instead of walking the list
        books.clear();
    }
    while( !books.empty() ) {
        // We can end up here with books left if the player has speed>100
        completed_scanning_current_book( act, who );
    }
    // CDF: no add_msg_if_player_sees free function, and vgettext instead of DDA's n_gettext
    who.add_msg_player_or_npc( m_info, _( "You scan %d %s into your device." ),
                               _( "<npcname> scans %d %s into their device." ), handled_books,
                               vgettext( "book", "books", handled_books ) );
    act.set_to_null();
}

void ebooksave_activity_actor::canceled( player_activity &act, Character &who )
{
    // CDF: no add_msg_if_player_sees free function
    who.add_msg_player_or_npc( m_info, _( "You stop scanning the remaining books." ),
                               _( "<npcname> stops scanning books." ) );
    act.set_to_null();
}

void ebooksave_activity_actor::start_scanning_next_book( player_activity & )
{
    while( !books.empty() && !books.back() ) {
        books.pop_back();
    }
    if( books.empty() ) {
        progress.mod_moves_left( -progress.get_moves_left() );
        return;
    }
    const item &current_book = *books.back();
    const int pages_in_current_book = item::pages_in_book( *current_book.type );
    turns_left_on_current_book = to_turns<int>( pages_in_current_book * time_per_page );
    progress.mod_moves_left( to_moves<int>( required_time( live_books( books ) ) )
                             - progress.get_moves_left() );
}

void ebooksave_activity_actor::completed_scanning_current_book( player_activity &act,
        Character &who )
{
    safe_reference<item> scanned_book = books.back();
    books.pop_back();
    if( scanned_book && ereader ) {
        // CDF: put_in( const item &, pocket_type ) stores a COPY (the DDA spelling), so the
        // original stays. A refusal (the pocket is full) is not counted as scanned; quiet = true:
        // no debugmsg.
        const ret_val<void> stored =
            ereader->put_in( *scanned_book, pocket_type::E_FILE_STORAGE, false, &who, true );
        if( stored.success() ) {
            // DDA activity_actor.cpp:3697-3706: the avatar identifies the scanned book (CDF:
            // is_book() stands for DDA's is_identifiable()).
            if( avatar *player_avatar = who.as_avatar() ) {
                const itype_id &book_id = scanned_book->typeId();
                if( scanned_book->is_book() && !player_avatar->has_identified( book_id ) ) {
                    player_avatar->identify( *scanned_book );
                }
            }
            handled_books++;
        }
    }
    start_scanning_next_book( act );
}

void ebooksave_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "books", books );
    jsout.member( "ereader", ereader );
    jsout.member( "handled_books", handled_books );
    jsout.member( "turns_left_on_current_book", turns_left_on_current_book );
    jsout.end_object();
}

std::unique_ptr<activity_actor> ebooksave_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<ebooksave_activity_actor> actor =
        std::make_unique<ebooksave_activity_actor>();

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "books", actor->books );
    data.read( "ereader", actor->ereader );
    data.read( "handled_books", actor->handled_books );
    data.read( "turns_left_on_current_book", actor->turns_left_on_current_book );

    return actor;
}

namespace
{
// CDF: a dropped item keeps a valid safe_reference, while DDA's item_location of a carried item
// goes invalid when the item is dropped. A device can be used when it is carried (any depth) or
// lies on the map or in a vehicle within PICKUP_RANGE on the same level (the menus offer such
// devices).
auto edevice_in_reach( const safe_reference<item> &edevice, const Character &who ) -> bool
{
    if( !edevice ) {
        return false;
    }
    const item &device = *edevice;
    if( who.has_item( device ) ) {
        return true;
    }
    if( !device.has_position() ) {
        return false;
    }
    const tripoint_bub_ms device_pos = device.bub_pos();
    const tripoint_bub_ms who_pos = who.bub_pos();
    return device_pos.z() == who_pos.z() && rl_dist( device_pos, who_pos ) <= PICKUP_RANGE;
}

// CDF: DDA's item_location of a file goes invalid when the file leaves its device (or the device
// goes); a safe_reference follows the file wherever it moves. So "the file exists" is "the file is
// valid and sits in the device the action expects it in".
auto efile_in_place( const safe_reference<item> &efile,
                     const safe_reference<item> &expected_parent ) -> bool
{
    return efile && expected_parent && efile->parent_item() == expected_parent.get();
}
} // namespace

efile_activity_actor::efile_activity_actor( item *used, const std::vector<item *> &targets,
        const std::vector<item *> &files, efile_action action, efile_combo combo ) :
    used_edevice( used ), action_type( action ), combo_type( combo )
{
    for( item *target : targets ) {
        target_edevices.emplace_back( target );
    }
    for( item *file : files ) {
        selected_efiles.emplace_back( file );
    }
}

void efile_activity_actor::start( player_activity &, Character &who )
{
    if( combo_type == COMBO_MOVE_ONTO_BROWSE ) {
        target_edevices_copy = target_edevices;
    }
    //handle combo move e-device (browsing may have included used e-device)
    if( action_type == EF_MOVE_ONTO_THIS ) {
        auto i = target_edevices.begin();
        while( i != target_edevices.end() ) {
            if( i->get() == used_edevice.get() ) {
                i = target_edevices.erase( i );
                break;
            } else {
                i++;
            }
        }
    }
    for( safe_reference<item> &i : target_edevices ) {
        // CDF: a destroyed device leaves an invalid reference; DDA never has one
        if( i && !i->has_pocket_type( pocket_type::E_FILE_STORAGE ) ) {
            debugmsg( "invalid item %s provided to efile activity; must have "
                      "\"E_FILE_STORAGE\" pocket", i->tname() );
        }
    }
    //only skip if loaded through deserialization
    if( !started_processing ) {
        started_processing = true;
        computer_low_skill = who.get_skill_level( skill_computer ) < 1;
    }

    time_duration total_time = 0_seconds;
    if( !processed_edevices_remain() ) {
        // CDF: the members are safe_references, the statics take item pointers
        total_time = total_processing_time( used_edevice ? used_edevice.get() : nullptr,
                                            live_books( target_edevices ),
                                            live_books( selected_efiles ), action_type, who,
                                            computer_low_skill );
        target_edevices_count = target_edevices.size();
    } else {
        done_processing = true;
    }
    // CDF: the time is kept in progress, always as one task (also for 0 moves)
    progress.emplace( used_edevice ? used_edevice->tname() : std::string(),
                      to_moves<int>( total_time ) );
}

void efile_activity_actor::do_turn( player_activity &act, Character &who )
{
    map &here = get_map();

    auto edevice_reduce_charge = [&]( const safe_reference<item> &edevice ) {
        if( action_time_scale::once_every_this_tick( charge_time( action_type ) ) ) {
            if( !edevice->ammo_sufficient( &who ) ) {
                // CDF: no add_msg_if_player_sees; DDA shows the same message to player and npcs
                who.add_msg_player_or_npc( m_warning, _( "The %s ran out of batteries!" ),
                                           _( "The %s ran out of batteries!" ),
                                           edevice->display_name() );
                return false;
            }
            edevice->consume_tool_uses( 1, here, edevice->bub_pos(), &who );
        }
        return true;
    };

    //check for zero devices selected, for combo call
    if( progress.get_moves_left() > 0 ) {
        //if an e-device was booted, make sure it still exists
        if( turns_left_on_current_edevice ) {
            if( !edevice_in_reach( used_edevice, who ) || !edevice_reduce_charge( used_edevice ) ) {
                //if the used device runs out of power or is missing, fail all remaining devices
                do {
                    failed_processing_current_edevice();
                } while( !done_processing );
            } else {
                const safe_reference<item> &next_edevice = get_currently_processed_edevice();
                if( !edevice_in_reach( next_edevice, who ) ||
                    !edevice_reduce_charge( next_edevice ) ) {
                    failed_processing_current_edevice();
                }
            }
        }
    }
    //done check (handles return)
    if( done_processing ) {
        progress.mod_moves_left( -progress.get_moves_left() );
        return;
    }
    //computer practice
    if( one_in( 3 ) && computer_low_skill ) {
        // CDF: practice returns void, re-read the skill level
        who.practice( skill_computer, 1 );
        computer_low_skill = who.get_skill_level( skill_computer ) < 1;
    }

    if( !next_edevice_booted ) {
        if( !turns_left_on_current_edevice ) {
            // CDF: the device is missing before it was booted: DDA dereferences an empty
            // optional here
            if( !edevice_in_reach( used_edevice, who ) ||
                !edevice_in_reach( get_currently_processed_edevice(), who ) ) {
                failed_processing_current_edevice();
                if( done_processing ) {
                    progress.mod_moves_left( -progress.get_moves_left() );
                }
                return;
            }
            start_processing_next_edevice(); //only sets if device exists
            if( !turns_left_on_current_edevice ) {
                failed_processing_current_edevice();
                if( done_processing ) {
                    progress.mod_moves_left( -progress.get_moves_left() );
                }
                return;
            }
        }
        ( *turns_left_on_current_edevice )--;
        if( turns_left_on_current_edevice == 0 ) {
            next_edevice_booted = true;
            start_processing_next_efile( act, who ); //sets turns_left_file if file exists
        }
    }
    if( next_edevice_booted ) {
        // should not be an "else" because files start processing in same turn
        // CDF: the file must exist AND sit where the action expects it (a from-action works on the
        // used device's files, the others on the current device's files)
        const safe_reference<item> &expected_parent = efile_action_is_from( action_type ) ?
                used_edevice : get_currently_processed_edevice();
        if( !efile_in_place( get_currently_processed_efile(), expected_parent ) ) {
            failed_processing_current_efile( act, who );
            return; // the next file, or the end of this device, is handled on the next turn
        }
        if( turns_left_on_current_efile > 0 ) {
            turns_left_on_current_efile--;
        }
        if( turns_left_on_current_efile == 0 ) {
            completed_processing_current_efile( act, who );
            start_processing_next_efile( act, who );
        }
    }
}

void efile_activity_actor::finish( player_activity &act, Character &who )
{
    act.set_to_null();
    const std::string action_name = efile_action_name( action_type, true, true );
    const int total_edevices = target_edevices_count;
    if( total_edevices == 0 && processed_edevices == 0 ) {
        who.add_msg_player_or_npc( m_info, _( "No devices needed to be processed." ),
                                   _( "<npcname> had no devices to process." ) );
    } else if( processed_edevices == 0 ) {
        who.add_msg_player_or_npc( m_warning, _( "You failed to %s %d device(s)." ),
                                   _( "<npcname> failed to %s %d device(s)." ),
                                   efile_action_name( action_type, false, true ), total_edevices );
    } else {
        who.add_msg_player_or_npc( m_good, _( "You successfully %s %d/%d device(s)." ),
                                   _( "<npcname> successfully %s %d/%d device(s)." ),
                                   action_name, processed_edevices, total_edevices );
    }
    combo_next_activity( who );
}

void efile_activity_actor::canceled( player_activity &act, Character &who )
{
    who.add_msg_player_or_npc( m_info, _( "You stop processing the remaining devices." ),
                               _( "<npcname> stops processing devices." ) );
    act.set_to_null();
}

void efile_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "used_edevice", used_edevice );
    jsout.member( "target_edevices", target_edevices );
    jsout.member( "target_edevices_copy", target_edevices_copy );
    jsout.member( "action_type", action_type );
    jsout.member( "combo_type", combo_type );
    jsout.member( "selected_efiles", selected_efiles );
    jsout.member( "currently_processed_efiles", currently_processed_efiles );
    jsout.member( "target_edevices_count", target_edevices_count );
    jsout.member( "processed_edevices", processed_edevices );
    jsout.member( "failed_edevices", failed_edevices );
    jsout.member( "processed_efiles", processed_efiles );
    jsout.member( "failed_efiles", failed_efiles );
    jsout.member( "started_processing", started_processing );
    jsout.member( "done_processing", done_processing );
    jsout.member( "next_edevice_booted", next_edevice_booted );
    jsout.member( "computer_low_skill", computer_low_skill );
    jsout.member( "turns_left_on_current_edevice", turns_left_on_current_edevice );
    jsout.member( "turns_left_on_current_efile", turns_left_on_current_efile );
    jsout.end_object();
}

std::unique_ptr<activity_actor> efile_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<efile_activity_actor> actor = std::make_unique<efile_activity_actor>();

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "used_edevice", actor->used_edevice );
    data.read( "target_edevices", actor->target_edevices );
    data.read( "target_edevices_copy", actor->target_edevices_copy );
    data.read( "action_type", actor->action_type );
    data.read( "combo_type", actor->combo_type );
    data.read( "selected_efiles", actor->selected_efiles );
    data.read( "currently_processed_efiles", actor->currently_processed_efiles );
    data.read( "target_edevices_count", actor->target_edevices_count );
    data.read( "processed_edevices", actor->processed_edevices );
    data.read( "failed_edevices", actor->failed_edevices );
    data.read( "processed_efiles", actor->processed_efiles );
    data.read( "failed_efiles", actor->failed_efiles );
    data.read( "started_processing", actor->started_processing );
    data.read( "done_processing", actor->done_processing );
    data.read( "next_edevice_booted", actor->next_edevice_booted );
    data.read( "computer_low_skill", actor->computer_low_skill );
    data.read( "turns_left_on_current_edevice", actor->turns_left_on_current_edevice );
    data.read( "turns_left_on_current_efile", actor->turns_left_on_current_efile );

    return actor;
}

std::string efile_activity_actor::efile_action_name( efile_action action_type, bool past_tense,
        bool extended )
{
    std::vector<std::string> base_names = { _( "browse" ), _( "read" ), _( "move" ), _( "move" ),
                                            _( "copy" ), _( "copy" ), _( "wipe" )
                                          };
    std::vector<std::string> past_names = { _( "browsed" ), _( "read" ), _( "moved" ), _( "moved" ),
                                            _( "copied" ), _( "copied" ), _( "wiped" )
                                          };
    std::vector<std::string> extensions = { "", "", _( " files onto" ), _( " files off of" ),
                                            _( " files onto" ), _( " files off of" ), ""
                                          };
    const std::string name_output = ( past_tense ? past_names : base_names )[action_type];
    return extended ? name_output + extensions[action_type] : name_output;
}

bool efile_activity_actor::efile_action_exclude_used( efile_action action_type )
{
    return action_type == EF_MOVE_FROM_THIS || action_type == EF_COPY_FROM_THIS ||
           action_type == EF_MOVE_ONTO_THIS || action_type == EF_COPY_ONTO_THIS;
}

bool efile_activity_actor::efile_action_is_from( efile_action action_type )
{
    return action_type == EF_MOVE_FROM_THIS || action_type == EF_COPY_FROM_THIS;
}

bool efile_activity_actor::efile_skip_copy( const efile_transfer &transfer, const item &efile )
{
    const auto check_for_file = [&efile]( const item * edevice ) {
        // CDF: a vanished device cannot already hold the file
        if( edevice == nullptr ) {
            return false;
        }
        for( const item *i : edevice->efiles() ) {
            if( i->typeId() == efile.typeId() ) {
                return true;
            }
        }
        return false;
    };
    if( efile.is_ecopiable() && !efile.has_flag( flag_E_FILE_COLLECTION ) ) {
        return check_for_file( transfer.used_edevice ) && check_for_file( transfer.target_edevice );
    }
    return false;
}

time_duration efile_activity_actor::efile_processing_time( item *efile,
        const efile_transfer &transfer, efile_action action_type, Character &who,
        bool computer_low_skill )
{
    time_duration processing_time;
    if( action_type == EF_BROWSE ) {
        processing_time = time_per_browsed_efile;
    } else if( action_type == EF_WIPE ) {
        processing_time = 1_seconds + ( efile->ememory_size() / local_etransfer_rate ) * 1_seconds;
    } else if( efile_skip_copy( transfer, *efile ) ) {
        return 1_seconds;
    } else {
        //otherwise, this is a basic file transfer
        processing_time = 1_seconds + ( efile->ememory_size() / current_etransfer_rate(
                                            who, transfer, efile ) ) * 1_seconds;
    }
    if( computer_low_skill ) {
        processing_time *= 3;
    }
    return processing_time;
}

time_duration efile_activity_actor::total_processing_time( item *used_edevice,
        const std::vector<item *> &currently_processed_edevices,
        const std::vector<item *> &selected_efiles,
        efile_action action_type, Character &who, bool computer_low_skill )
{
    time_duration total_time = device_bootup_time * currently_processed_edevices.size();
    for( item *edevice : currently_processed_edevices ) {
        for( item *efile : selected_efiles ) {
            efile_transfer transfer( used_edevice, edevice );
            total_time += efile_processing_time( efile, transfer, action_type, who,
                                                 computer_low_skill );
        }
    }

    return total_time;
}

units::ememory efile_activity_actor::current_etransfer_rate( Character &who,
        const efile_transfer &transfer, item *efile )
{
    if( transfer.used_edevice == nullptr || transfer.target_edevice == nullptr ) {
        // a vanished device: the activity fails on its first turn, the time does not matter
        return local_etransfer_rate;
    }
    item *target_edevice = transfer.target_edevice;
    item *used_edevice = transfer.used_edevice;
    item *external_transfer_edevice = find_external_transfer_estorage( who, efile, target_edevice,
                                       used_edevice );

    const auto get_etransfer_rate = []( const item * edevice ) {
        units::ememory rate = edevice->type->tool ? edevice->type->tool->etransfer_rate : 0_KB;
        if( rate == 0_KB ) {
            debugmsg( "%s is candidate for transferring e-files but has no etransfer_rate",
                      edevice->tname() );
            rate = 1_KB;
        }
        return rate;
    };

    units::ememory used_rate = get_etransfer_rate( used_edevice );
    units::ememory target_rate = get_etransfer_rate( target_edevice );
    units::ememory external_transfer_rate = external_transfer_edevice ?
                                            get_etransfer_rate( external_transfer_edevice ) / 2 : 0_KB;
    units::ememory slow_transfer_rate = 1_MB; //bluetooth

    units::ememory final_rate = std::min( used_rate, target_rate );

    edevice_compatible compat = edevices_compatible( used_edevice, target_edevice );
    if( compat == ECOMPAT_FAST ) {
        //do nothing
    } else if( compat == ECOMPAT_SLOW ) {
        if( external_transfer_rate > slow_transfer_rate ) {
            final_rate = external_transfer_rate;
        } else {
            final_rate = slow_transfer_rate; //bluetooth
        }
    } else {
        debugmsg( "incompatible devices %s, %s provided to efile_activity_actor",
                  used_edevice->display_name(), target_edevice->display_name() );
    }
    return final_rate;
}

std::vector<item *> efile_activity_actor::filter_edevice_efiles( item *edevice,
        std::vector<item *> &filter_files )
{
    std::vector<item *> filtered_efiles;
    std::vector<item *>::iterator it = filter_files.begin();
    while( it != filter_files.end() ) {
        if( ( *it )->parent_item() == edevice ) {
            filtered_efiles.emplace_back( *it );
            it = filter_files.erase( it );
        } else {
            ++it;
        }
    }
    return filtered_efiles;
}

item *efile_activity_actor::find_external_transfer_estorage( Character &p, item *efile, item *ed1,
        item *ed2 )
{
    // CDF: a vanished device or file
    if( efile == nullptr || ed1 == nullptr || ed2 == nullptr ) {
        return nullptr;
    }
    item *fastest_edevice = nullptr;
    if( edevices_compatible( ed1, ed2 ) == ECOMPAT_FAST ) {
        return fastest_edevice;
    }
    units::ememory largest_efile_size = efile->ememory_size();
    //search for fastest, large-enough, browsed, non-tool, estorage device in radius or on person
    units::ememory fastest_rate = 0_KB;
    const std::function<bool( item *, item * )> func = [&]( item * it, item * ) {
        return it->is_browsed() &&
               !edevice_has_use( it ) && //is not a usable e-device (e.g. a USB drive)
               it->is_estorage() &&
               it->remaining_ememory() >= largest_efile_size &&
               it->is_tool();
    };
    std::vector<item *> nearby_etds = p.nearby( func, PICKUP_RANGE );
    for( item *candidate : nearby_etds ) {
        if( candidate->type->tool->etransfer_rate > fastest_rate ) {
            fastest_edevice = candidate;
        }
    }
    return fastest_edevice;
}

bool efile_activity_actor::edevice_has_use( const item *edevice )
{
    return edevice != nullptr && edevice->type->can_use( "E_FILE_DEVICE" );
}

efile_activity_actor::edevice_compatible efile_activity_actor::edevices_compatible( item *ed1,
        item *ed2 )
{
    if( ed1 == nullptr || ed2 == nullptr ) {
        // CDF: a vanished device has no port
        return ECOMPAT_NONE;
    }
    return edevices_compatible( *ed1, *ed2 );
}

efile_activity_actor::edevice_compatible efile_activity_actor::edevices_compatible(
    const item &ed1, const item &ed2 )
{
    if( !ed1.is_tool() || !ed2.is_tool() ) {
        return efile_activity_actor::ECOMPAT_NONE;
    }
    const std::string &port1 = ed1.type->tool->e_port;
    const std::string &port2 = ed2.type->tool->e_port;
    if( port1 == port2 ) {
        return efile_activity_actor::ECOMPAT_SLOW;
    }
    const std::vector<std::string> &banned1 = ed1.type->tool->e_ports_banned;
    const std::vector<std::string> &banned2 = ed2.type->tool->e_ports_banned;
    for( const std::string &ban : banned1 ) {
        if( ban == port2 ) {
            return efile_activity_actor::ECOMPAT_NONE;
        }
    }
    for( const std::string &ban : banned2 ) {
        if( ban == port1 ) {
            return efile_activity_actor::ECOMPAT_NONE;
        }
    }
    return efile_activity_actor::ECOMPAT_FAST;
}

void efile_activity_actor::start_processing_next_edevice()
{
    const safe_reference<item> &current_edevice = get_currently_processed_edevice();
    const time_duration process_time = device_bootup_time;
    bool ignore_filter = efile_action_is_from( action_type );
    if( current_edevice ) {
        turns_left_on_current_edevice = to_turns<int>( process_time );
        if( ignore_filter ) {
            currently_processed_efiles = selected_efiles;
        } else {
            // CDF: the static works on item pointers, the members are safe_references
            std::vector<item *> remaining = live_books( selected_efiles );
            const std::vector<item *> filtered = filter_edevice_efiles( current_edevice.get(),
                                                 remaining );
            currently_processed_efiles.clear();
            for( item *efile : filtered ) {
                currently_processed_efiles.emplace_back( efile );
            }
            selected_efiles.clear();
            for( item *efile : remaining ) {
                selected_efiles.emplace_back( efile );
            }
        }
    }
}

void efile_activity_actor::start_processing_next_efile( player_activity &, Character &who )
{
    // CDF: files that vanished or left their device are dropped here (DDA dereferences an invalid
    // location)
    const safe_reference<item> &expected_parent = efile_action_is_from( action_type ) ?
            used_edevice : get_currently_processed_edevice();
    while( !processed_efiles_remain() &&
           !efile_in_place( get_currently_processed_efile(), expected_parent ) ) {
        currently_processed_efiles.pop_back();
        failed_efiles++;
    }
    if( !processed_efiles_remain() ) {
        //separate for easy debugging
        item *current_efile = get_currently_processed_efile().get();
        const efile_transfer transfer( used_edevice ? used_edevice.get() : nullptr,
                                       get_currently_processed_edevice().get() );
        const time_duration transfer_time = efile_processing_time( current_efile, transfer,
                                            action_type, who, computer_low_skill );
        turns_left_on_current_efile = to_turns<int>( transfer_time );
    } else {
        completed_processing_current_edevice();
    }
}

void efile_activity_actor::failed_processing_current_edevice()
{
    failed_edevices++;

    turns_left_on_current_edevice.reset();
    next_edevice_booted = false;
    // CDF: the list can be empty here
    if( !target_edevices.empty() ) {
        target_edevices.pop_back();
    }
    if( processed_edevices_remain() ) {
        done_processing = true;
    }
}

void efile_activity_actor::completed_processing_current_edevice()
{
    if( action_type == EF_BROWSE ) {
        // CDF: the list can be empty here
        if( get_currently_processed_edevice() ) {
            get_currently_processed_edevice()->set_browsed( true );
        }
    }
    processed_edevices++;

    next_edevice_booted = false;
    turns_left_on_current_edevice.reset();
    // CDF: the list can be empty here
    if( !target_edevices.empty() ) {
        target_edevices.pop_back();
    }
    if( processed_edevices_remain() ) {
        done_processing = true;
    }
}

void efile_activity_actor::failed_processing_current_efile( player_activity &act, Character &who )
{
    // CDF: DDA does not remove the failed file from the list
    if( !currently_processed_efiles.empty() ) {
        currently_processed_efiles.pop_back();
    }
    failed_efiles++;
    start_processing_next_efile( act, who );
}

void efile_activity_actor::completed_processing_current_efile( player_activity &, Character &who )
{
    const safe_reference<item> current_edevice = get_currently_processed_edevice();
    const safe_reference<item> current_efile = get_currently_processed_efile();
    if( !current_efile || !current_edevice || !used_edevice ) {
        // CDF: a vanished file or device (the callers check, this is the guard)
        failed_efiles++;
        if( !currently_processed_efiles.empty() ) {
            currently_processed_efiles.pop_back();
        }
        return;
    }
    const efile_transfer current_transfer( used_edevice.get(), current_edevice.get() );

    // CDF: a file that leaves a device is detached from it and dies with its detached_ptr
    const auto remove_efile = []( item & efile_remove ) {
        detached_ptr<item> removed = efile_remove.detach();
    };
    // DDA activity_actor.cpp:4035-4059: instead of storing the file, combine it with the
    // recipe catalog / photo gallery the target device already has. Returns true when the
    // file was merged.
    const auto merge_into_target = []( item & edevice, const item & efile ) {
        if( efile.type->memory_card_data ) {
            item *edevice_recipe_catalog = edevice.get_recipe_catalog();
            if( edevice_recipe_catalog != nullptr ) {
                std::set<recipe_id> merged_catalogs = edevice_recipe_catalog->get_saved_recipes();
                for( const recipe_id &recipe : efile.get_saved_recipes() ) {
                    merged_catalogs.emplace( recipe );
                }
                edevice_recipe_catalog->set_saved_recipes( merged_catalogs );
                return true;
            }
        } else if( efile.typeId() == itype_efile_photos ) {
            // CDF: get_item_with skips the e-file pocket
            item *edevice_photos = edevice.get_photo_gallery();
            if( edevice_photos != nullptr ) {
                std::vector<item::extended_photo_def> extended_photos;
                edevice_photos->read_extended_photos( extended_photos, "CAMERA_EXTENDED_PHOTOS",
                                                     false );
                efile.read_extended_photos( extended_photos, "CAMERA_EXTENDED_PHOTOS", true );
                edevice_photos->write_extended_photos( extended_photos,
                                                       "CAMERA_EXTENDED_PHOTOS" );
                return true;
            }
        }
        return false;
    };
    // CDF, D-B: a move keeps the file's identity (detach from the source, attach to the target;
    // DDA copies and removes), a copy stores a copy (put_in( const item & )). A recipe catalog or
    // photo gallery is merged into the target's own instead of stored (merge_into_target). A
    // skipped file (the target already holds it, efile_skip_copy) is not stored; a moved skipped
    // file is still removed from the source, as in DDA.
    const auto add_efile = [&]( const safe_reference<item> &edevice, item &efile, bool copy ) {
        const bool skip = efile_skip_copy( current_transfer, efile );
        if( copy ) {
            if( !skip && !merge_into_target( *edevice, efile ) ) {
                edevice->put_in( efile, pocket_type::E_FILE_STORAGE, false, &who, true );
            }
            return;
        }
        item *source = efile.parent_item();
        detached_ptr<item> detached = efile.detach();
        if( skip || merge_into_target( *edevice, *detached ) ) {
            // nothing to store: the moved file dies here (a skipped file is still removed from
            // the source, as in DDA)
            return;
        }
        const ret_val<void> stored = edevice->put_in( std::move( detached ),
                                     pocket_type::E_FILE_STORAGE, false, &who, true );
        if( !stored.success() && source != nullptr ) {
            // the target refused the file: it goes back where it came from
            source->put_in( std::move( detached ), pocket_type::E_FILE_STORAGE, false, &who, true );
        }
    };

    who.add_msg_player_or_npc( m_info, _( "You %1$s %2$s." ), _( "<npcname> %1$s %2$s." ),
                               efile_action_name( action_type, true, false ),
                               current_efile->display_name() );
    switch( action_type ) {
        case EF_BROWSE:
            if( current_efile->typeId() == itype_efile_junk ) {
                remove_efile( *current_efile );
                break;
            }
            if( current_efile->get_recipe_catalog() != nullptr ) {
                if( current_efile->get_saved_recipes().empty() ) {
                    current_efile->generate_recipes();
                }
            }
            current_efile->set_browsed( true );
            break;
        case EF_MOVE_ONTO_THIS:
            add_efile( used_edevice, *current_efile, false );
            break;
        case EF_MOVE_FROM_THIS:
            add_efile( current_edevice, *current_efile, false );
            break;
        case EF_COPY_ONTO_THIS:
            add_efile( used_edevice, *current_efile, true );
            break;
        case EF_COPY_FROM_THIS:
            add_efile( current_edevice, *current_efile, true );
            break;
        case EF_WIPE:
            remove_efile( *current_efile );
            break;
        default:
            break;
    }
    processed_efiles++;
    if( !currently_processed_efiles.empty() ) {
        currently_processed_efiles.pop_back();
    }
}

safe_reference<item> &efile_activity_actor::get_currently_processed_edevice()
{
    // CDF: DDA calls back() unguarded; an empty list gives an invalid reference
    static safe_reference<item> none;
    return target_edevices.empty() ? none : target_edevices.back();
}

safe_reference<item> &efile_activity_actor::get_currently_processed_efile()
{
    // CDF: DDA calls back() unguarded; an empty list gives an invalid reference
    static safe_reference<item> none;
    return currently_processed_efiles.empty() ? none : currently_processed_efiles.back();
}

bool efile_activity_actor::processed_edevices_remain() const
{
    // DDA's odd naming: true when NONE remain
    return target_edevices.empty();
}

bool efile_activity_actor::processed_efiles_remain() const
{
    // DDA's odd naming: true when NONE remain
    return currently_processed_efiles.empty();
}

void efile_activity_actor::combo_next_activity( Character &who )
{
    efile_combo new_combo = COMBO_NONE;
    efile_action new_action = EF_INVALID;
    std::vector<item *> all_updated_files;
    // CDF: the devices that are still there (a vanished one is skipped)
    const std::vector<item *> combo_devices = live_books( target_edevices_copy );

    if( combo_type == COMBO_MOVE_ONTO_BROWSE && used_edevice ) {
        for( item *edevice : combo_devices ) {
            for( item *efile : edevice->efiles() ) {
                all_updated_files.push_back( efile );
            }
        }

        units::ememory total_ememory;
        for( item *edevice : combo_devices ) {
            if( edevice->is_browsed() ) {
                for( item *efile : edevice->efiles() ) {
                    total_ememory += efile->ememory_size();
                }
            }
        }
        if( total_ememory <= used_edevice->remaining_ememory() ) {
            new_action = EF_MOVE_ONTO_THIS;
        } else {
            who.add_msg_player_or_npc( m_warning,
                    _( "File size exceeds available memory; canceling file move." ),
                    _( "<npcname> cancels the file move: the files are too big." ) );
        }
    }
    if( new_action != EF_INVALID ) {
        // CDF: assign_activity( ..., false ) as reload_activity_actor::finish does: the finished
        // activity is not resumed
        std::unique_ptr<efile_activity_actor> combo_actor = std::make_unique<efile_activity_actor>(
                    used_edevice.get(), combo_devices, all_updated_files, new_action, new_combo );
        who.assign_activity( std::make_unique<player_activity>( std::move( combo_actor ) ), false );
    }
}

time_duration efile_activity_actor::charge_time( efile_action action )
{
    if( action == EF_BROWSE ) {
        return charge_per_browse_time;
    }
    return charge_per_transfer_time;
}

namespace activity_actors
{

// Please keep this alphabetically sorted
const std::unordered_map<activity_id, std::unique_ptr<activity_actor>( * )( JsonIn & )>
deserialize_functions = {
    { activity_id( "ACT_AIM" ), &aim_activity_actor::deserialize },
    { activity_id( "ACT_AUTODRIVE" ), &autodrive_activity_actor::deserialize },
    { activity_id( "ACT_BINDER_COPY_RECIPE" ), &bookbinder_copy_activity_actor::deserialize },
    { activity_id( "ACT_BOLTCUTTING" ), &boltcutting_activity_actor::deserialize },
    { activity_id( "ACT_BUILD" ), &construction_activity_actor::deserialize },
    { activity_id( "ACT_CRAFT" ), &craft_activity_actor::deserialize },
    { activity_id( "ACT_DIG" ), &dig_activity_actor::deserialize },
    { activity_id( "ACT_DIG_CHANNEL" ), &dig_channel_activity_actor::deserialize },
    { activity_id( "ACT_DISASSEMBLE" ), &disassemble_activity_actor::deserialize },
    { activity_id( "ACT_DROP" ), &drop_activity_actor::deserialize },
    { activity_id( "ACT_EBOOKSAVE" ), &ebooksave_activity_actor::deserialize },
    { activity_id( "ACT_E_FILE" ), &efile_activity_actor::deserialize },
    { activity_id( "ACT_HACKING" ), &hacking_activity_actor::deserialize },
    { activity_id( "ACT_HACKSAW" ), &hacksaw_activity_actor::deserialize },
    { activity_id( "ACT_LOCKPICK" ), &lockpick_activity_actor::deserialize },
    { activity_id( "ACT_MIGRATION_CANCEL" ), &migration_cancel_activity_actor::deserialize },
    { activity_id( "ACT_MOVE_ITEMS" ), &move_items_activity_actor::deserialize },
    { activity_id( "ACT_TOGGLE_GATE" ), &toggle_gate_activity_actor::deserialize },
    { activity_id( "ACT_OXYTORCH" ), &oxytorch_activity_actor::deserialize },
    { activity_id( "ACT_PICKUP" ), &pickup_activity_actor::deserialize },
    { activity_id( "ACT_INSERT_ITEM" ), &insert_item_activity_actor::deserialize },
    { activity_id( "ACT_RELOAD" ), &reload_activity_actor::deserialize },
    { activity_id( "ACT_STASH" ), &stash_activity_actor::deserialize },
    { activity_id( "ACT_UNLOAD" ), &unload_activity_actor::deserialize },
    { activity_id( "ACT_THROW" ), &throw_activity_actor::deserialize },
    { activity_id( "ACT_ASSIST" ), &assist_activity_actor::deserialize },
    { activity_id( "ACT_LONGSALVAGE" ), &salvage_activity_actor::deserialize },
    { activity_id( "ACT_ENCHANT" ), &enchant_activity_actor::deserialize }
};
} // namespace activity_actors

void serialize( const std::unique_ptr<activity_actor> &actor, JsonOut &jsout )
{
    if( !actor ) {
        jsout.write_null();
    } else {
        jsout.start_object();

        jsout.member( "actor_type", actor->get_type() );
        jsout.member( "actor_data", *actor );

        jsout.end_object();
    }
}

void deserialize( std::unique_ptr<activity_actor> &actor, JsonIn &jsin )
{
    if( jsin.test_null() ) {
        actor = nullptr;
    } else {
        JsonObject data = jsin.get_object();
        if( data.has_member( "actor_data" ) ) {
            activity_id actor_type;
            data.read( "actor_type", actor_type );
            auto deserializer = activity_actors::deserialize_functions.find( actor_type );
            if( deserializer != activity_actors::deserialize_functions.end() ) {
                actor = deserializer->second( *data.get_raw( "actor_data" ) );
            } else {
                debugmsg( "Failed to find activity actor deserializer for type \"%s\"", actor_type.c_str() );
                actor = nullptr;
            }
        } else {
            debugmsg( "Failed to load activity actor" );
            actor = nullptr;
        }
    }
}
