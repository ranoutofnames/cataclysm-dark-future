#pragma once

#include "activity_actor.h"
#include "coordinates.h"
#include "contents_change_handler.h"
#include "enum_conversions.h"
#include "craft_command.h"
#include "crafting.h"
#include "item_handling_util.h"
#include "location_ptr.h"
#include "locations.h"
#include "memory_fast.h"
#include "pickup_token.h"
#include "point.h"
#include "safe_reference.h"
#include "type_id.h"
#include "units.h"
#include "units_energy.h"

#include <optional>

class Creature;
class vehicle;
class item_reload_option;
class recipe;
struct partial_con;

class aim_activity_actor : public activity_actor
{
    private:
        safe_reference<item> weapon;
        location_ptr<item> fake_weapon;
        units::energy bp_cost_per_shot = 0_J;
        int stamina_cost_per_shot = 0;
        std::vector<tripoint_bub_ms> fin_trajectory;

    public:
        std::string action;
        int aif_duration = 0; // Counts aim-and-fire duration
        bool aiming_at_critter = false; // Whether aiming at critter or a tile
        bool snap_to_target = false;
        bool shifting_view = false;
        tripoint_rel_ms initial_view_offset;
        /** Target UI requested to abort aiming */
        bool aborted = false;
        /** RELOAD_AND_SHOOT weapon is unloaded when the aim ends without firing (DDA should_unload_RAS) */
        bool should_unload_RAS = false;
        /** Item location for RAS weapon reload */
        safe_reference<item> reload_loc;
        /** if true abort if no targets are available when re-entering aiming ui after shooting */
        bool abort_if_no_targets = false;
        /**
         * Target UI requested to abort aiming and reload weapon
         * Implies aborted = true
         */
        bool reload_requested = false;
        /**
         * A friendly creature may enter line of fire during aim-and-shoot,
         * and that generates a warning to proceed/abort. If player decides to
         * proceed, that creature is saved in this vector to prevent the same warning
         * from popping up on the following turn(s).
         */
        std::vector<weak_ptr_fast<Creature>> acceptable_losses;

        aim_activity_actor();

        /** Aiming wielded gun */
        static std::unique_ptr<aim_activity_actor> use_wielded();

        /** Aiming fake gun provided by a bionic */
        static std::unique_ptr<aim_activity_actor> use_bionic( detached_ptr<item> &&fake_gun,
                const units::energy &cost_per_shot );

        /** Aiming gun provided by gear */
        static std::unique_ptr<aim_activity_actor> use_gear( item *gun );

        /** Aiming fake gun provided by a mutation */
        static std::unique_ptr<aim_activity_actor> use_mutation( detached_ptr<item> &&fake_gun );

        activity_id get_type() const override {
            return activity_id( "ACT_AIM" );
        }

        void start( player_activity &act, Character &who ) override;
        void do_turn( player_activity &act, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;
        void canceled( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

        item *get_weapon();
        void restore_view();
        // Load/unload a RELOAD_AND_SHOOT weapon
        bool load_RAS_weapon();
        void unload_RAS_weapon();
};

class autodrive_activity_actor : public activity_actor
{
    private:
        vehicle *player_vehicle = nullptr;

    public:
        autodrive_activity_actor() = default;

        activity_id get_type() const override {
            return activity_id( "ACT_AUTODRIVE" );
        }

        void start( player_activity &act, Character & ) override;
        void do_turn( player_activity &, Character & ) override;
        void canceled( player_activity &, Character & ) override;
        void finish( player_activity &act, Character & ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

class craft_activity_actor final : public activity_actor
{
    protected:
        const recipe *rec = nullptr;
        int batch_size = 1;
        int craft_counter = 0;  // 0 to 10,000,000 — mirrors item's counter field
        tripoint_abs_ms location;

        std::vector<comp_selection<item_comp>> item_selections;
        std::vector<comp_selection<tool_comp>> tool_selections;

        bool tools_prepaid = false;
        bool is_long = false;
        bool is_valid = false;
        int last_turn_nr = -1;  // turn# when last do_turn ran; -1 = never set
        float cached_tools_mult = 0.0f;   // 0 = not yet computed; set once in start()

        bool can_resume_with_internal( const activity_actor &other, const Character & ) const override {
            const auto &c_actor = static_cast<const craft_activity_actor &>( other );
            return equivalent_activity( c_actor );
        }

        bool equivalent_activity( const craft_activity_actor &other ) const {
            return location == other.location &&
                   rec == other.rec &&
                   batch_size == other.batch_size;
        }

    public:
        craft_activity_actor() = default;
        explicit craft_activity_actor(
            const recipe *rec,
            int batch_size = 1,
            int craft_counter = 0,
            const tripoint_abs_ms &location = tripoint_abs_ms::zero(),
            std::vector<comp_selection<item_comp>> item_selections = {},
            std::vector<comp_selection<tool_comp>> tool_selections = {},
            bool tools_prepaid = false,
            bool is_long = false
        );

        activity_id get_type() const override {
            return activity_id( "ACT_CRAFT" );
        }

        void calc_all_moves( player_activity &act, Character &who ) override;
        void start( player_activity &act, Character &who ) override;
        void do_turn( player_activity &act, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;
        void canceled( player_activity &/*act*/, Character &/*who*/ ) override {}

        const recipe *get_recipe() const { return rec; }
        int get_batch_size() const { return batch_size; }
        int get_craft_counter() const { return craft_counter; }
        const tripoint_abs_ms &get_location() const { return location; }
        bool are_tools_prepaid() const { return tools_prepaid; }

        act_progress_message get_progress_message( const player_activity &act,
                const Character &who ) const override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

    private:
        auto find_in_progress_craft( const player_activity &act,
                                     Character &who ) const -> item *; // *NOPAD*
        void do_complete_craft( player_activity &act, Character &who );
        void refresh_speed( player_activity &act, const Character &who, const item &craft_item,
                            std::optional<bench_location> bench = std::nullopt ) const;

};

class dig_activity_actor : public activity_actor
{
    private:
        int moves_total;
        /** location of the dig **/
        tripoint_bub_ms location;
        std::string result_terrain;
        tripoint_bub_ms byproducts_location;
        std::string byproducts_item_group;

        /**
         * Returns true if @p other and `this` are "equivalent" in the sense that
         *  `this` can be resumed instead of starting @p other.
         */
        bool equivalent_activity( const dig_activity_actor &other ) const {
            return  location == other.location &&
                    result_terrain == other.result_terrain &&
                    byproducts_location == other.byproducts_location &&
                    byproducts_item_group == other.byproducts_item_group;
        }

        /**
         * @pre @p other is a `dig_activity_actor`
         */
        bool can_resume_with_internal( const activity_actor &other, const Character & ) const override {
            const dig_activity_actor &d_actor = static_cast<const dig_activity_actor &>( other );
            return equivalent_activity( d_actor );
        }

    public:
        dig_activity_actor(
            int dig_moves, const tripoint_bub_ms &dig_loc,
            const std::string &resulting_ter, const tripoint_bub_ms &dump_loc,
            const std::string &dump_item_group
        ):
            moves_total( dig_moves ), location( dig_loc ),
            result_terrain( resulting_ter ),
            byproducts_location( dump_loc ),
            byproducts_item_group( dump_item_group ) {}

        activity_id get_type() const override {
            return activity_id( "ACT_DIG" );
        }

        void start( player_activity &act, Character & ) override;
        void do_turn( player_activity &, Character & ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

class dig_channel_activity_actor : public activity_actor
{
    private:
        int moves_total;
        /** location of the dig **/
        tripoint_bub_ms location;
        std::string result_terrain;
        tripoint_bub_ms byproducts_location;
        std::string byproducts_item_group;

        /**
         * Returns true if @p other and `this` are "equivalent" in the sense that
         *  `this` can be resumed instead of starting @p other.
         */
        bool equivalent_activity( const dig_channel_activity_actor &other ) const {
            return  location == other.location &&
                    result_terrain == other.result_terrain &&
                    byproducts_location == other.byproducts_location &&
                    byproducts_item_group == other.byproducts_item_group;
        }

        /**
         * @pre @p other is a `dig_activity_actor`
         */
        bool can_resume_with_internal( const activity_actor &other, const Character & ) const override {
            const dig_channel_activity_actor &dc_actor = static_cast<const dig_channel_activity_actor &>
                    ( other );
            return equivalent_activity( dc_actor );
        }

    public:
        dig_channel_activity_actor(
            int dig_moves, const tripoint_bub_ms &dig_loc,
            const std::string &resulting_ter, const tripoint_bub_ms &dump_loc,
            const std::string &dump_item_group
        ):
            moves_total( dig_moves ), location( dig_loc ),
            result_terrain( resulting_ter ),
            byproducts_location( dump_loc ),
            byproducts_item_group( dump_item_group ) {}

        activity_id get_type() const override {
            return activity_id( "ACT_DIG_CHANNEL" );
        }

        void start( player_activity &act, Character & ) override;
        void do_turn( player_activity &, Character & ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

class disassemble_activity_actor : public activity_actor
{
    private:
        std::vector<iuse_location> targets;
        tripoint_abs_ms pos;
        bool recursive = false;

    public:
        disassemble_activity_actor() = default;
        disassemble_activity_actor(
            std::vector<iuse_location> &&targets,
            tripoint_abs_ms pos,
            bool recursive
        ) : targets( std::move( targets ) ), pos( pos ), recursive( recursive ) {}
        ~disassemble_activity_actor() = default;

        activity_id get_type() const override {
            return activity_id( "ACT_DISASSEMBLE" );
        }
        void calc_all_moves( player_activity &act, Character &who ) override;
        void start( player_activity &act, Character &who ) override;
        void do_turn( player_activity &, Character & ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

        bool try_start_single( player_activity &act, Character &who );
        void process_target( player_activity &, iuse_location &target );
};

class drop_activity_actor : public activity_actor
{
    private:
        std::list<pickup::act_item> items;
        bool force_ground = false;
        tripoint_rel_ms relpos;

    public:
        drop_activity_actor() = default;
        drop_activity_actor( Character &ch, const drop_locations &items,
                             bool force_ground, const tripoint_rel_ms &relpos );

        activity_id get_type() const override {
            return activity_id( "ACT_DROP" );
        }

        void start( player_activity &, Character & ) override;
        void do_turn( player_activity &, Character &who ) override;
        void finish( player_activity &, Character & ) override {};

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

class hacking_activity_actor : public activity_actor
{
    private:
        bool using_bionic = false;

    public:
        struct use_bionic {};

        hacking_activity_actor() = default;
        hacking_activity_actor( use_bionic );

        activity_id get_type() const override {
            return activity_id( "ACT_HACKING" );
        }

        void start( player_activity &act, Character &who ) override;
        void do_turn( player_activity &, Character & ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

class hacksaw_activity_actor : public activity_actor
{
    public:
        explicit hacksaw_activity_actor( const tripoint_bub_ms &target,
                                         const safe_reference<item> &tool ) : target( target ), tool( tool ) {};

        activity_id get_type() const override {
            return activity_id( "ACT_HACKSAW" );
        }

        void start( player_activity &act, Character &who ) override;
        void do_turn( player_activity &/*act*/, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

        // debugmsg causes a backtrace when fired during cata_test
        bool testing = false;  // NOLINT(cata-serialize)
    private:
        tripoint_bub_ms target;
        safe_reference<item> tool;

        bool can_resume_with_internal( const activity_actor &other,
                                       const Character &/*who*/ ) const override {
            const hacksaw_activity_actor &actor = static_cast<const hacksaw_activity_actor &>
                                                  ( other );
            return actor.target == target;
        }
};

class boltcutting_activity_actor : public activity_actor
{
    public:
        explicit boltcutting_activity_actor( const tripoint_bub_ms &target,
                                             const safe_reference<item> tool ) : target( target ), tool( tool ) {};

        activity_id get_type() const override {
            return activity_id( "ACT_BOLTCUTTING" );
        }

        void start( player_activity &act, Character &/*who*/ ) override;
        void do_turn( player_activity &/*act*/, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

        // debugmsg causes a backtrace when fired during cata_test
        bool testing = false;

    private:
        tripoint_bub_ms target;
        safe_reference<item> tool;

        bool can_resume_with_internal( const activity_actor &other,
                                       const Character &/*who*/ ) const override {
            const boltcutting_activity_actor &actor = static_cast<const boltcutting_activity_actor &>
                    ( other );
            return actor.target == target && actor.tool == tool;
        }
};

class lockpick_activity_actor : public activity_actor
{
    private:
        int moves_total;
        safe_reference<item> lockpick;
        location_ptr<item> fake_lockpick;
        tripoint_abs_ms target;

        lockpick_activity_actor(
            int moves_total,
            safe_reference<item> lockpick,
            detached_ptr<item> &&fake_lockpick,
            const tripoint_abs_ms &target
        ) : moves_total( moves_total ), lockpick( lockpick ), fake_lockpick( new fake_item_location() ),
            target( target ) {
            this->fake_lockpick = std::move( fake_lockpick );
        };

    public:
        /** Use regular lockpick. 'target' is in global coords */
        static std::unique_ptr<lockpick_activity_actor> use_item(
            int moves_total,
            item &lockpick,
            const tripoint_abs_ms &target
        );

        /** Use bionic lockpick. 'target' is in global coords */
        static std::unique_ptr<lockpick_activity_actor> use_bionic(
            detached_ptr<item> &&fake_lockpick,
            const tripoint_abs_ms &target
        );

        activity_id get_type() const override {
            return activity_id( "ACT_LOCKPICK" );
        }

        void start( player_activity &act, Character & ) override;
        void do_turn( player_activity &, Character & ) override;
        void finish( player_activity &act, Character &who ) override;

        static bool is_pickable( const tripoint_bub_ms &p );
        static std::optional<tripoint_bub_ms> select_location( avatar &you );

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

class migration_cancel_activity_actor : public activity_actor
{
    public:
        migration_cancel_activity_actor() = default;

        activity_id get_type() const override {
            return activity_id( "ACT_MIGRATION_CANCEL" );
        }

        void start( player_activity &, Character & ) override {};
        void do_turn( player_activity &act, Character &who ) override;
        void finish( player_activity &, Character & ) override {};

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

class move_items_activity_actor : public activity_actor
{
    private:
        std::vector<safe_reference<item>> target_items;
        std::vector<int> quantities;
        bool to_vehicle;
        tripoint_rel_ms relative_destination;

    public:
        move_items_activity_actor( std::vector<item *> items, std::vector<int> quantities,
                                   bool to_vehicle, tripoint_rel_ms relative_destination ) :
            quantities( quantities ), to_vehicle( to_vehicle ),
            relative_destination( relative_destination ) {

            for( item *&it : items ) {
                target_items.emplace_back( it );
            }
        }

        activity_id get_type() const override {
            return activity_id( "ACT_MOVE_ITEMS" );
        }

        void start( player_activity &, Character & ) override {};
        void do_turn( player_activity &act, Character &who ) override;
        void finish( player_activity &, Character & ) override {};


        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

class toggle_gate_activity_actor : public activity_actor
{
    private:
        int moves_total;
        tripoint_bub_ms placement;

        /**
         * @pre @p other is a toggle_gate_activity_actor
         */
        bool can_resume_with_internal( const activity_actor &other, const Character & ) const override {
            const toggle_gate_activity_actor &og_actor = static_cast<const toggle_gate_activity_actor &>
                    ( other );
            return placement == og_actor.placement;
        }

    public:
        toggle_gate_activity_actor( int gate_moves, const tripoint_bub_ms &gate_placement ) :
            moves_total( gate_moves ), placement( gate_placement ) {}

        activity_id get_type() const override {
            return activity_id( "ACT_TOGGLE_GATE" );
        }

        void start( player_activity &act, Character & ) override;
        void do_turn( player_activity &, Character & ) override;
        void finish( player_activity &act, Character & ) override;


        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

class pickup_activity_actor : public activity_actor
{
    private:
        /** Target items and the quantities thereof */
        std::vector<pickup::pick_drop_selection> target_items;

        /**
         * Position of the character when the activity is started. This is
         * stored so that we can cancel the activity if the player moves
         * (e.g. if the player is in a moving vehicle). This should be null
         * if not grabbing from the ground.
         */
        std::optional<tripoint_bub_ms> starting_pos;

    public:
        pickup_activity_actor( const std::vector<pickup::pick_drop_selection> &target_items,
                               const std::optional<tripoint_bub_ms> &starting_pos )
            : target_items( target_items )
            , starting_pos( starting_pos ) {}

        activity_id get_type() const override {
            return activity_id( "ACT_PICKUP" );
        }

        void start( player_activity &, Character & ) override {};
        void do_turn( player_activity &act, Character &who ) override;
        void finish( player_activity &, Character & ) override {};


        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

class stash_activity_actor : public activity_actor
{
    private:
        std::list<pickup::act_item> items;
        tripoint_rel_ms relpos;

    public:
        stash_activity_actor() = default;
        stash_activity_actor( Character &ch, const drop_locations &items, const tripoint_rel_ms &relpos );

        activity_id get_type() const override {
            return activity_id( "ACT_STASH" );
        }

        void start( player_activity &, Character & ) override;
        void do_turn( player_activity &, Character &who ) override;
        void finish( player_activity &, Character & ) override {};

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

/**
 * Puts items into a container, one by one, paying the obtain and handling cost of each.
 * Transplanted from DDA activity_actor_definitions.h:2539 (insert_item_activity_actor); CDF:
 * items are safe_references, and like drop/stash the activity pays its moves per item in do_turn.
 */
class insert_item_activity_actor : public activity_actor
{
    private:
        safe_reference<item> holster;
        drop_locations items;
        contents_change_handler handler;
        bool all_pockets_rigid = false;
        bool reopen_menu = false;
        // allow put charge items into holster's nested pocket
        bool allow_fill_count_by_charge_item_nested = true;
        // the next item stacks with the one being moved, so it costs only its handling
        bool bulk_next = false;

    public:
        insert_item_activity_actor() = default;
        insert_item_activity_actor( item &holster, const drop_locations &holstered_list,
                                    bool reopen_menu = false,
                                    bool allow_fill_count_by_charge_item_nested = true )
            : holster( &holster ), items( holstered_list ), reopen_menu( reopen_menu ),
              allow_fill_count_by_charge_item_nested( allow_fill_count_by_charge_item_nested ) {}

        activity_id get_type() const override {
            return activity_id( "ACT_INSERT_ITEM" );
        }

        void start( player_activity &act, Character &who ) override;
        void do_turn( player_activity &act, Character &who ) override;
        void finish( player_activity &, Character & ) override {};
        void canceled( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

// CDF: P3-6-1a-1 (DDA activity_actor_definitions.h:2332-2371); targets are
// safe_reference<item> (D1, insert-actor pattern), no clone() (the CDF base has none).
class reload_activity_actor : public activity_actor
{
    public:
        explicit reload_activity_actor( item_reload_option &&opt, int extra_moves = 0 );

        activity_id get_type() const override {
            return activity_id( "ACT_RELOAD" );
        }

        void start( player_activity &act, Character &who ) override;
        void do_turn( player_activity &act, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;
        void canceled( player_activity &act, Character &who ) override;

        void reload_msg( Character &who, bool finished = false );
        void reload( player_activity &act, Character &who, int load_qty = 1 );

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

        // CDF: P3-6-1b. Read-only seam for the tests (and 6-2a): the actor's targets
        // replace the old player_activity::targets/index assertions.
        item *target() const {
            return target_loc ? target_loc.get() : nullptr;
        }
        item *ammo() const {
            return ammo_loc ? ammo_loc.get() : nullptr;
        }
        int qty() const {
            return quantity;
        }

    private:
        explicit reload_activity_actor() = default;
        int moves_total = 0;
        int quantity = 0;
        int seconds_per_round = 0;
        int already_loaded = 0;
        // MAGAZINE_WELL pocket index in target_loc->contents. Negative =
        // first-compatible-well fallback.
        int pocket_index = -1;
        // cache ammo because reloading deletes the location
        itype_id ammo_id; // NOLINT(cata-serialize)
        safe_reference<item> target_loc;
        safe_reference<item> ammo_loc;

        bool can_reload() const;
        static void make_reload_sound( Character &who, item &reloadable );
};

// CDF: P3-6-4a (DDA activity_actor_definitions.h:2035-2067). Unloads a magazine or an ammo belt over
// time; the target is a safe_reference<item> (D1) and there is no clone() (the CDF base has none).
// Containers are 6-4b; guns and tools stay in avatar_funcs::unload_item (6-4d).
class unload_activity_actor : public activity_actor
{
    public:
        unload_activity_actor( int moves_total, item &target )
            : moves_total( moves_total ), target( &target ) {}

        activity_id get_type() const override {
            return activity_id( "ACT_UNLOAD" );
        }

        void start( player_activity &act, Character &who ) override;
        void do_turn( player_activity &, Character & ) override {}
        void finish( player_activity &act, Character &who ) override;

        /** Unloads the magazine instantly. Can be called without an activity. May destroy the item. */
        static void unload( Character &who, item &target );

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

    private:
        unload_activity_actor() = default;
        int moves_total = 0;
        safe_reference<item> target;

        bool can_resume_with_internal( const activity_actor &other, const Character & ) const override {
            const unload_activity_actor &act = static_cast<const unload_activity_actor &>( other );
            return target == act.target;
        }
};

class throw_activity_actor : public activity_actor
{
    private:

        safe_reference<item> target;
        std::optional<tripoint_bub_ms> blind_throw_from_pos;

    public:
        throw_activity_actor() = default;
        throw_activity_actor(
            item &target,
            std::optional<tripoint_bub_ms> blind_throw_from_pos
        ) : target( &target ),
            blind_throw_from_pos( blind_throw_from_pos ) {}
        ~throw_activity_actor() = default;

        activity_id get_type() const override {
            return activity_id( "ACT_THROW" );
        }

        void start( player_activity &, Character & ) override {};
        void do_turn( player_activity &act, Character &who ) override;
        void finish( player_activity &, Character & ) override {};

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};


class oxytorch_activity_actor : public activity_actor
{
    public:
        explicit oxytorch_activity_actor( const tripoint_bub_ms &target,
                                          const safe_reference<item> &tool ) : target( target ), tool( tool ) {};

        activity_id get_type() const override {
            return activity_id( "ACT_OXYTORCH" );
        }

        void start( player_activity &act, Character &who ) override;
        void do_turn( player_activity &/*act*/, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

        // debugmsg causes a backtrace when fired during cata_test
        bool testing = false;  // NOLINT(cata-serialize)
    private:
        tripoint_bub_ms target;
        safe_reference<item> tool;

        bool can_resume_with_internal( const activity_actor &other,
                                       const Character &/*who*/ ) const override {
            const oxytorch_activity_actor &actor = static_cast<const oxytorch_activity_actor &>
                                                   ( other );
            return actor.target == target;
        }
};

class construction_activity_actor : public activity_actor
{
    private:
        tripoint_abs_ms target;
        partial_con *pc;
    public:
        explicit construction_activity_actor( const tripoint_abs_ms &target ) : target( target ) {
        };

        activity_id get_type() const override {
            return activity_id( "ACT_BUILD" );
        }

        void calc_all_moves( player_activity &act, Character &who ) override;

        void start( player_activity &act, Character &who ) override;
        void do_turn( player_activity &act, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

class assist_activity_actor : public activity_actor
{
    public:
        explicit assist_activity_actor() {
        };

        activity_id get_type() const override {
            return activity_id( "ACT_ASSIST" );
        }

        void calc_all_moves( player_activity & /*act*/, Character &/*who*/ ) override {};

        void start( player_activity &act, Character &who ) override;
        void do_turn( player_activity &/*act*/, Character &/*who*/ ) override {};
        void finish( player_activity &/*act*/, Character &/*who*/ ) override {};

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

};

class salvage_activity_actor : public activity_actor
{
    private:
        iuse_locations targets;
        tripoint_abs_ms pos;
        bool mute_prompts = false;
    public:
        salvage_activity_actor() = default;
        salvage_activity_actor(
            iuse_locations &&targets,
            tripoint_abs_ms pos
        ) : targets( std::move( targets ) ), pos( pos ) {}

        ~salvage_activity_actor() = default;

        activity_id get_type() const override {
            return activity_id( "ACT_LONGSALVAGE" );
        }

        void calc_all_moves( player_activity & /*act*/, Character &/*who*/ ) override;

        void start( player_activity &act, Character &who ) override;
        void do_turn( player_activity &/*act*/, Character &/*who*/ ) override;
        void finish( player_activity &/*act*/, Character &/*who*/ ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

class enchant_activity_actor : public activity_actor
{
    private:
        safe_reference<item> target;
        furn_str_id furn;
        std::string enchanter_id;
        int moves_total;

    public:
        enchant_activity_actor() = default;
        enchant_activity_actor(
            item &target,
            furn_str_id furn,
            std::string enchanter_id,
            int moves
        ) : target( &target ),
            furn( furn ),
            enchanter_id( enchanter_id ),
            moves_total( moves ) {}
        ~enchant_activity_actor() = default;

        activity_id get_type() const override {
            return activity_id( "ACT_ENCHANT" );
        }

        void start( player_activity &, Character & ) override;
        void do_turn( player_activity &act, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );
};

// CDF: E1-1, the DDA e-file actors (DDA activity_actor_definitions.h:776-816, 1355-1610, b4b62254ae2).
// All three are declared here and compile as stubs (activity_actor.cpp, marked "E1-5:") so no later
// step edits this header. DEVIATION D-A: DDA's item_location is item * for arguments and results, and
// safe_reference<item> for the members, so the targets survive saving and moving items (the insert and
// reload actors do the same). DEVIATION D-B: items move between devices by detach/attach.

/**
 * Copies a recipe into a book binder over time (DDA bookbinder_copy_activity_actor).
 * Needs paper and a writing tool; the recipe is stored with get_saved_recipes/set_saved_recipes.
 */
class bookbinder_copy_activity_actor : public activity_actor
{
    private:
        safe_reference<item> book_binder;
        recipe_id rec_id;
        int pages = 0;

    public:
        bookbinder_copy_activity_actor() = default;
        bookbinder_copy_activity_actor( item &book_binder, const recipe_id &rec_id )
            : book_binder( &book_binder ), rec_id( rec_id ) {}

        activity_id get_type() const override {
            return activity_id( "ACT_BINDER_COPY_RECIPE" );
        }

        static int pages_for_recipe( const recipe &r );

        void start( player_activity &act, Character &who ) override;
        void do_turn( player_activity &act, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

    protected:
        bool can_resume_with_internal( const activity_actor &other, const Character & ) const override;
};

/**
 * Scans books onto an e-device (the "Store books" use, DDA ebooksave_activity_actor).
 * 5 seconds per page, one battery use per 120 pages; the original books stay.
 */
class ebooksave_activity_actor : public activity_actor
{
    public:
        ebooksave_activity_actor() = default;
        ebooksave_activity_actor( const std::vector<item *> &books, item *ereader );

        activity_id get_type() const override {
            return activity_id( "ACT_EBOOKSAVE" );
        }

        static int total_pages( const std::vector<item *> &books );
        static time_duration required_time( const std::vector<item *> &books );
        static int required_charges( const std::vector<item *> &books, const item *ereader );

        void start( player_activity &act, Character &who ) override;
        void do_turn( player_activity &act, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;
        void canceled( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

        static constexpr time_duration time_per_page = 5_seconds;
        // Every 120 pages requires one charge of the ereader (equivalent of 1500 mWh, so roughly 6 charges per hour)
        static constexpr int pages_per_charge = 120;

    private:
        /**
         * All remaining books left to scan.
         * After a book is fully scanned to the ereader, it is removed from this list.
         */
        std::vector<safe_reference<item>> books;
        safe_reference<item> ereader;
        /** How many books this activity has scanned to ereader so far. */
        int handled_books = 0;
        /** How much time is left on this book until it is fully scanned and we can move on to the next book. */
        int turns_left_on_current_book = 0;

        void start_scanning_next_book( player_activity &act );
        void completed_scanning_current_book( player_activity &act, Character &who );
};

/**
 * Handles any e-file processing on an e-device (DDA efile_action and efile_combo).
 */
enum efile_action : int {
    EF_BROWSE,
    EF_READ,
    EF_MOVE_FROM_THIS,
    EF_MOVE_ONTO_THIS,
    EF_COPY_FROM_THIS,
    EF_COPY_ONTO_THIS,
    EF_WIPE,
    EF_INVALID,
    EF_ACTION_COUNT
};
template<>
struct enum_traits<efile_action> {
    static constexpr efile_action last = efile_action::EF_ACTION_COUNT;
};
enum efile_combo : int {
    COMBO_MOVE_ONTO_BROWSE,
    COMBO_NONE,
    EFILE_COMBO_COUNT
};
template<>
struct enum_traits<efile_combo> {
    static constexpr efile_combo last = efile_combo::EFILE_COMBO_COUNT;
};

namespace io
{
template<>
std::string enum_to_string<efile_action>( efile_action data );

template<>
std::string enum_to_string<efile_combo>( efile_combo data );
} // namespace io

/** Holds an e-file transfer's devices. */
struct efile_transfer {
    //the e-device initiating the action -- always usable
    item *used_edevice = nullptr;
    //the e-device receiving the action -- optionally usable
    item *target_edevice = nullptr;

    efile_transfer( item *used_edevice, item *target_edevice ) :
        used_edevice( used_edevice ),
        target_edevice( target_edevice ) {}
};

/**
 * Handles electronic file browsing/transferring for electronic devices.
 * On a per-turn basis: e-devices are "booted" until success or failure; for each e-device,
 * e-files are processed until success or failure.
 * See related functions: iuse::efiledevice, game_menus::inv::edevice_select/efile_select
 */
class efile_activity_actor : public activity_actor
{
    public:
        explicit efile_activity_actor() = default;
        /**
         * @param used_edevice the edevice used from iuse::efiledevice
         * @param target_edevices all e-devices to perform e-file operation with
         * @param selected_efiles if provided, action only uses the given files
         * @param action_type which sub-activity to do
         * @param combo_type which sub-activity to do immediately after action_type
         */
        efile_activity_actor( item *used_edevice, const std::vector<item *> &target_edevices,
                              const std::vector<item *> &selected_efiles, efile_action action_type,
                              efile_combo combo_type );

        activity_id get_type() const override {
            return activity_id( "ACT_E_FILE" );
        }

        void start( player_activity &act, Character &who ) override;
        void do_turn( player_activity &act, Character &who ) override;
        void finish( player_activity &act, Character &who ) override;
        void canceled( player_activity &act, Character &who ) override;

        void serialize( JsonOut &jsout ) const override;
        static std::unique_ptr<activity_actor> deserialize( JsonIn &jsin );

        /** Abstract flat time to look at a file's filename and extension, determine whether it's
         * junk or not, and open it to verify its contents */
        static constexpr time_duration time_per_browsed_efile = 15_seconds;
        /** One charge consumed per 4 minutes */
        static constexpr time_duration charge_per_transfer_time = 4_minutes;
        /** One charge consumed per 32 files (8 minutes) */
        static constexpr time_duration charge_per_browse_time = time_per_browsed_efile * 32;
        /** Time required to boot an e-device */
        static constexpr time_duration device_bootup_time = 10_seconds;
        /** Generic local file transfer speed */
        static constexpr units::ememory local_etransfer_rate = 200_MB;

        /** Converts `efile_action` enum to human-readable string
         * @param extended for move/copy actions, e.g. "move files onto" */
        static std::string efile_action_name( efile_action action_type, bool past_tense = false,
                                              bool extended = false );
        /** Returns if the action automatically excludes the used e-device with its target e-devices */
        static bool efile_action_exclude_used( efile_action action_type );
        /** Returns if the action is moving files from an e-device */
        static bool efile_action_is_from( efile_action action_type );
        /** Returns if we can skip transferring this file because a copy of it already exists */
        static bool efile_skip_copy( const efile_transfer &transfer, const item &efile );
        /** Returns time needed to process the given file with the given transfer */
        static time_duration efile_processing_time( item *efile, const efile_transfer &transfer,
                efile_action action_type, Character &who, bool computer_low_skill );
        /** Returns time needed to process all given e-devices with the given transfer */
        static time_duration total_processing_time( item *used_edevice,
                const std::vector<item *> &currently_processed_edevices,
                const std::vector<item *> &selected_efiles,
                efile_action action_type, Character &who, bool computer_low_skill );
        /** Returns the effective electronic transfer rate with `external_transfer_rate` factored in */
        static units::ememory current_etransfer_rate( Character &who, const efile_transfer &transfer,
                item *efile );
        /** Returns all e-files on this device that are in the filter_files list,
         * and removes matching efiles from filter_files list
         * @param filter_files list of e-files to use, usually selected_files */
        static std::vector<item *> filter_edevice_efiles( item *edevice,
                std::vector<item *> &filter_files );
        /** Returns the most optimal estorage (if needed) for improving transfer speed given the two edevices provided */
        static item *find_external_transfer_estorage( Character &p, item *efile, item *ed1,
                item *ed2 );
        /** Returns whether the given edevice can process files (expand later if needed) */
        static bool edevice_has_use( const item *edevice );
        enum edevice_compatible {
            ECOMPAT_NONE,
            ECOMPAT_SLOW,
            ECOMPAT_FAST
        };
        /** Returns the given edevices' compatibility type based on their itypes and e_port_banned */
        static edevice_compatible edevices_compatible( item *ed1, item *ed2 );
        static edevice_compatible edevices_compatible( const item &ed1, const item &ed2 );

    private:
        safe_reference<item> used_edevice;
        std::vector<safe_reference<item>> target_edevices;
        /** Held for combo activity */
        std::vector<safe_reference<item>> target_edevices_copy;
        /** e-files that have yet to be processed, across all devices */
        std::vector<safe_reference<item>> selected_efiles;
        efile_action action_type = EF_INVALID;
        efile_combo combo_type = COMBO_NONE;
        /** e-files currently processed on current e-device */
        std::vector<safe_reference<item>> currently_processed_efiles;
        /** How many e-devices this activity started with. */
        int target_edevices_count = 0;
        /** How many e-devices this activity has successfully processed so far. */
        int processed_edevices = 0;
        /** How many e-devices this activity has failed to process so far. */
        int failed_edevices = 0;
        /** How many e-files this activity has successfully processed so far. */
        int processed_efiles = 0;
        /** How many e-files this activity has failed to process so far. */
        int failed_efiles = 0;
        /** Have we started processing e-devices? */
        bool started_processing = false;
        /** Have we finished processing e-devices? */
        bool done_processing = false;
        /** Have we finished booting the current e-device? */
        bool next_edevice_booted = false;
        /** actor computer skill < 1 */
        bool computer_low_skill = false;
        /** How many turns left on this e-device until it is fully processed.
         * If empty, no e-device is currently being processed */
        std::optional<int> turns_left_on_current_edevice;
        /** How many turns left on this file until it is fully processed. */
        int turns_left_on_current_efile = 0;

        void start_processing_next_edevice();
        void start_processing_next_efile( player_activity &act, Character &who );
        void failed_processing_current_edevice();
        void completed_processing_current_edevice();
        void failed_processing_current_efile( player_activity &act, Character &who );
        /** Action upon completing processing an e-file (depends on action_type) */
        void completed_processing_current_efile( player_activity &act, Character &who );

        safe_reference<item> &get_currently_processed_edevice();
        safe_reference<item> &get_currently_processed_efile();
        bool processed_edevices_remain() const;
        bool processed_efiles_remain() const;

        /** Segue to the next player activity for a combo type */
        void combo_next_activity( Character &who );
        time_duration charge_time( efile_action action_type );
};
