#pragma once

#include "bodypart.h" // body_part::num_bp
#include "calendar.h"
#include "catalua_type_operators.h"
#include "color.h" // nc_color
#include "coordinates.h"
#include "damage.h"
#include "data_vars.h"
#include "enums.h" // point
#include "explosion.h"
#include "game_constants.h"
#include "hsv_color.h"
#include "pocket_data.h"
#include "iuse.h" // use_function
#include "map/mapdata.h"
#include "pldata.h" // add_type
#include "shape.h"
#include "stomach.h"
#include "translations.h"
#include "type_id.h"
#include "units.h"
#include "value_ptr.h"

#include <array>
#include <iosfwd>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

class Item_factory;
class item;
class JsonObject;
class JsonIn;
class lua_iwieldable_actor;
class lua_iwearable_actor;
class lua_iequippable_actor;
class lua_istate_actor;
class lua_iconsume_actor;
class lua_imelee_actor;
class lua_iranged_actor;
class lua_itrap_actor;
class player;
class relic;
template <typename E> struct enum_traits;

enum art_effect_active : int;
enum art_charge : int;
enum art_charge_req : int;
enum art_effect_passive : int;

class gun_modifier_data
{
    private:
        std::string name_;
        int qty_;
        std::set<std::string> flags_;

    public:
        /**
         * @param n A string that can be translated via @ref _ (must have been extracted for translation).
         */
        gun_modifier_data( const std::string &n, const int q, const std::set<std::string> &f ) : name_( n ),
            qty_( q ), flags_( f ) { }
        std::string name() const {
            return name_;
        }
        /// @returns The translated name of the gun mode.
        std::string tname() const {
            return _( name_ );
        }
        int qty() const {
            return qty_;
        }
        const std::set<std::string> &flags() const {
            return flags_;
        }
};

class gunmod_location
{
    private:
        std::string _id;

    public:
        gunmod_location() = default;
        gunmod_location( const std::string &id ) : _id( id ) { }

        /// Returns the translated name.
        std::string name() const;
        /// Returns the location id.
        std::string str() const {
            return _id;
        }

        bool operator==( const gunmod_location &rhs ) const {
            return _id == rhs._id;
        }
        bool operator<( const gunmod_location &rhs ) const {
            return _id < rhs._id;
        }
};

struct pocket_consumption_entry {
    std::string pocket;
    int qty = 0;

    bool was_loaded = false;
    void deserialize( const JsonObject &jo );
};

// A mod's adjustment to one host pocket's per-use consumption, targeted by id.
// `set` overrides the resolved qty; `multiply` scales it.
struct pocket_consumption_mod {
    std::string pocket;
    std::optional<int> set;
    float multiply = 1.0f;

    bool was_loaded = false;
    void deserialize( const JsonObject &jo );
};

// A mod's scaling of one host pocket's ammo capacity, targeted by id. Applies to
// integral MAGAZINE pockets only; multiple mods on one pocket combine by product.
struct pocket_capacity_mod {
    std::string pocket;
    float multiply = 1.0f;

    bool was_loaded = false;
    void deserialize( const JsonObject &jo );
};

// Tools lower consumption_per_use into per_mode[DEFAULT]; guns use the
// firing_requirements map directly.
struct firing_requirement_set {
    std::map<gun_mode_id, std::vector<pocket_consumption_entry>> per_mode;

    bool empty() const {
        return per_mode.empty();
    }
    const std::vector<pocket_consumption_entry> *for_mode( const gun_mode_id &m ) const {
        const auto it = per_mode.find( m );
        return it == per_mode.end() ? nullptr : &it->second;
    }

    bool was_loaded = false;
    void deserialize_firing_requirements( const JsonObject &jo, std::string_view member );
    void deserialize_consumption_per_use( const JsonObject &jo, std::string_view member );
};

struct islot_tool;

struct islot_comestible;

struct islot_brewable;

struct islot_container;

/** DDA part_material: a material of one armor portion and the percentage it covers of it. */
struct part_material {
    material_id id = material_id::NULL_ID();
    // CDF: DDA default of covered_by_mat
    int cover = 100;
};

// DDA `encumbrance_modifier`: how a head item's weight-derived encumbrance is adjusted
enum class encumbrance_modifier : int {
    IMBALANCED,
    RESTRICTS_NECK,
    WELL_SUPPORTED,
    NONE,
    last
};

template<>
struct enum_traits<encumbrance_modifier> {
    static constexpr encumbrance_modifier last = encumbrance_modifier::last;
};

struct armor_portion_data {
    /**
     * How much this item encumbers the player.
     */
    int encumber = 0;
    /**
    * When storage is full, how much it encumbers the player. -1 (not given): DDA works it out
    * from the non-rigid pocket volume of the item when the item type is finalized.
    */
    int max_encumber = -1;
    /**
     * Percentage of the body part area that this item covers.
     * This determines how likely it is to hit the item instead of the player.
     */
    int coverage = 0;

    // Where does this cover if any
    body_part_set covers;

    /**
     * Sub-locations this portion covers (JSON `specifically_covers`, DDA #52468). Only
     * set on the portions in islot_armor::sub_data; `coverage` is then the coverage of each
     * of these sub-locations. Empty on the per-body-part portions in islot_armor::data.
     */
    std::set<sub_bodypart_str_id> sub_coverage;

    // DDA armor portion fields (J5)
    /** -1 (not given) until finalized: then the plain coverage, as DDA's loader defaults them. */
    int cover_melee = -1;
    int cover_ranged = -1;
    int cover_vitals = 0;
    bool rigid = false;
    bool rigid_layer_only = false;
    bool comfortable = false;

    /** DDA: how much the volume of the pockets of this item adds to its encumbrance. */
    float volume_encumber_modifier = 1;
    /** Materials of this portion (DDA); filled from the item materials when not given. */
    std::vector<part_material> materials;
    /** DDA: volume of non-rigid pocket capacity that adds one point of encumbrance. */
    static constexpr units::volume volume_per_encumbrance = 250_ml;

    /**
     * CDF: DDA `encumbrance_modifiers`. When not empty, the encumbrance of the item is derived
     * from its weight (calc_encumbrance, run when the item type is finalized) instead of being read.
     */
    std::vector<encumbrance_modifier> encumber_modifiers;

    /**
     * CDF: DDA portion-level `layers`. Stored only: CDF's layer is still one value per item
     * (itype::layer), nothing reads this yet.
     */
    std::set<layer_level> layers;

    /** DDA itype.cpp: encumbrance of an item of `weight` on `bp` from bp->encumbrance_per_weight. */
    int calc_encumbrance( units::mass weight, bodypart_id bp ) const;
};

struct islot_armor;

struct islot_pet_armor;

/**
 * What recipes can be learned from this book.
 */
struct book_recipe {
    /**
     * The recipe that can be learned (never null).
     */
    const class recipe *recipe;
    /**
     * The skill level required to learn the recipe.
     */
    int skill_level;
    /**
     * The name for the recipe as it appears in the book.
     */
    translation name;
    /**
     * Hidden means it does not show up in the description of the book.
     */
    bool hidden;
    bool is_hidden() const {
        return hidden;
    }

    LUA_TYPE_OPS( book_recipe, recipe );
};

struct islot_book;

struct islot_mod;

/**
 * Common data for ranged things: guns, gunmods and ammo.
 * The values of the gun itself, its mods and its current ammo (optional) are usually summed
 * up in the item class and the sum is used.
 */
struct common_ranged_data {
    /**
     * Damage, armor piercing and multipliers for each.
     * If multipliers are set on both gun and ammo, values will be normalized
     * as in @ref damage_instance::add_damage
     */
    damage_instance damage;
    /**
     * Range bonus from gun.
     */
    int range = 0;
    /**
     * Dispersion "bonus" from gun.
     */
    int dispersion = 0;
    /**
    * Bonus to the maximum potential multiplier for aimed critical damage.
    * Additive to the aimed critical damage mult maximum. A bonus of 1 is a potential +100% damage.
    * Does not add damage directly, skills stats or aimed crit bonus is required to take advantage.
    */
    double aimedcritmaxbonus = 0.0;
    /**
    * Bonus to the aimed critical damage multiplier.
    * Will apply to any "goodhit" or better hit (missed_by of 0.5 or less)
    * Will not raise the aimed critical damage mult above the max potential mult.
    * A bonus of 0.25 is +25% damage, up to the crit mult max.
    */
    double aimedcritbonus = 0.0;
};

struct islot_engine;

struct islot_wheel;

struct fuel_explosion {
    int explosion_chance_hot = 0;
    int explosion_chance_cold = 0;
    float explosion_factor = 0.0f;
    bool fiery_explosion = false;
    float fuel_size_factor = 0.0f;
};

struct islot_fuel;

struct islot_gun;

struct islot_gunmod;

struct islot_magazine;

struct islot_battery;

struct islot_ammo;

struct islot_bionic;

struct islot_seed;

struct islot_artifact;

enum class condition_type {
    FLAG,
    VITAMIN,
    COMPONENT_ID,
    num_condition_types
};

template<>
struct enum_traits<condition_type> {
    static constexpr auto last = condition_type::num_condition_types;
};

// A name that is applied under certain conditions.
struct conditional_name {
    // Context type  (i.e. "FLAG"          or "COMPONENT_ID")
    condition_type type;
    // Context name  (i.e. "CANNIBALISM"   or "mutant")
    std::string condition;
    // Name to apply (i.e. "Luigi lasagne" or "smoked mutant"). Can use %s which will
    // be replaced by the item's normal name and/or preceding conditional names.
    translation name;
};

class islot_milling;
struct memory_card_info;

struct attack_statblock {
    int to_hit = 0;
    damage_instance damage;
};

struct itype {
        friend class Item_factory;

        using FlagsSetType = std::set<flag_id>;

        std::vector<std::pair<itype_id, mod_id>> src;

        LUA_TYPE_OPS( itype, id );

        /**
         * Slots for various item type properties. Each slot may contain a valid pointer or null, check
         * this before using it.
         */
        /*@{*/
        cata::value_ptr<islot_container> container;
        cata::value_ptr<islot_tool> tool;
        cata::value_ptr<islot_comestible> comestible;
        cata::value_ptr<islot_brewable> brewable;
        cata::value_ptr<islot_armor> armor;
        cata::value_ptr<islot_pet_armor> pet_armor;
        cata::value_ptr<islot_book> book;
        cata::value_ptr<islot_mod> mod;
        cata::value_ptr<islot_engine> engine;
        cata::value_ptr<islot_wheel> wheel;
        cata::value_ptr<islot_fuel> fuel;
        cata::value_ptr<islot_gun> gun;
        cata::value_ptr<islot_gunmod> gunmod;
        cata::value_ptr<islot_magazine> magazine;
        cata::value_ptr<islot_battery> battery;
        cata::value_ptr<islot_bionic> bionic;
        cata::value_ptr<islot_ammo> ammo;
        cata::value_ptr<islot_seed> seed;
        cata::value_ptr<islot_artifact> artifact;
        cata::value_ptr<relic> relic_data;
        cata::value_ptr<islot_milling> milling_data;
        // CDF: E1-1, DDA itype.h:1564: memory card (and recipe catalog) generation data, key `memory_card`
        cata::value_ptr<memory_card_info> memory_card_data;
        /*@}*/

        /**
         * Pockets of this item type (CDF P1a, DDA #39406). Either loaded from
         * "pocket_data" or synthesised from legacy JSON in Item_factory::finalize.
         */
        std::vector<pocket_data> pockets;
        /** True if "pocket_data" was given in JSON, which disables synthesis. */
        bool explicit_pockets = false;

        // Empty for legacy items. Tools use DEFAULT mode; guns use per-mode
        // entries from the JSON map.
        firing_requirement_set firing_requirements;

        /** Raw charges per use for legacy callers of firing-requirement tools; must be >= 1. */
        int legacy_charges_per_use_factor = 1;

    private:
        /** Can item be combined with other identical items? */
        bool stackable_ = false;

        /** Minimum and maximum amount of damage to an item (state of maximum repair). */
        // TODO: create and use a MinMax class or similar to put both values into one object.
        /// @{
        int damage_min_ = -1000;
        int damage_max_ = +4000;
        /// @}

    protected:
        itype_id id = itype_id::NULL_ID(); /** unique string identifier for this type */

        // private because is should only be accessed through itype::nname!
        // nname() is used for display purposes
        translation name = no_translation( "none" );

    public:
        itype();
        virtual ~itype();

        int damage_min() const;
        int damage_max() const;

        // used for generic_factory for copy-from
        bool was_loaded = false;

        // a hint for tilesets: if it doesn't have a tile, what does it look like?
        itype_id looks_like;

        // What item this item repairs like if it doesn't have a recipe
        itype_id repairs_like;

        std::set<weapon_category_id> weapon_category;

        std::string snippet_category;
        translation description; // Flavor text
        ascii_art_id picture_id;

        // The container it comes in
        std::optional<itype_id> default_container;
        // DDA itype.h:1469, :1672: variant of the default container, and whether it is sealed
        // when the item spawns in it. BN items have no variants, so the variant is only loaded.
        std::optional<std::string> default_container_variant;
        bool default_container_sealed = true;

        std::map<quality_id, int> qualities; //Tool quality indicators
        std::map<std::string, std::string> properties;
        float crafting_speed_modifier = 1.0f;

        // A list of conditional names, in order of ascending priority.
        std::vector<conditional_name> conditional_names;

        // What we're made of (material names). .size() == made of nothing.
        // MATERIALS WORK IN PROGRESS.
        std::vector<material_id> materials;

        /** Actions an instance can perform (if any) indexed by action type */
        std::map<std::string, use_function> use_methods;

        /** The factor of ammo consumption indexed by action type*/
        std::map<std::string, int> ammo_scale;

        /** Action to take BEFORE the item is placed on map. If it returns non-zero, item won't be placed. */
        use_function drop_action;

        /** Field to spread when a liquid is spilled onto ordinary ground. */
        field_type_id spill_field;

        /** Lua callback actors (non-owning, owned by Item_factory) */
        const lua_iwieldable_actor *iwieldable_callbacks = nullptr;
        const lua_iwearable_actor *iwearable_callbacks = nullptr;
        const lua_iequippable_actor *iequippable_callbacks = nullptr;
        const lua_istate_actor *istate_callbacks = nullptr;
        const lua_iconsume_actor *iconsume_callbacks = nullptr;
        const lua_imelee_actor *imelee_callbacks = nullptr;
        const lua_iranged_actor *iranged_callbacks = nullptr;
        const lua_itrap_actor *itrap_callbacks = nullptr;

        /** Fields to emit when item is in active state */
        std::set<emit_id> emits;

        std::set<matec_id> techniques;

        // Minimum stat(s) or skill(s) to use the item
        std::map<skill_id, int> min_skills;
        int min_str = 0;
        int min_dex = 0;
        int min_int = 0;
        int min_per = 0;

        phase_id phase      = SOLID; // e.g. solid, liquid, gas

        // How should the item explode
        explosion_data explosion;
        // Should the item explode when lit on fire
        bool explode_in_fire = false;

        /** Is item destroyed after the countdown action is run? */
        bool countdown_destroy = false;

        /** Default countdown interval (if any) for item */
        int countdown_interval = 0;

        /** Action to take when countdown expires */
        use_function countdown_action;

        /**
         * @name Non-negative properties
         * After loading from JSON these properties guaranteed to be zero or positive
         */
        /**@{*/

        /** Weight of item ( or each stack member ) */
        units::mass weight = 0_gram;
        /** Weight difference with the part it replaces for mods */
        units::mass integral_weight = -1_gram;
        /** CDF: E1-1, DDA itype.h:1417: electronic memory size of this item as a file (`ememory_size`). */
        units::ememory ememory_size = 0_KB;

        /**
         * Space occupied by items of this type
         * CAUTION: value given is for a default-sized stack. Avoid using where @ref count_by_charges items may be encountered; see @ref item::volume instead.
         * To determine how many of an item can fit in a given space, use @ref charges_per_volume.
         */
        units::volume volume = 0_ml;
        /**
         * Space consumed when integrated as part of another item (defaults to volume)
         * CAUTION: value given is for a default-sized stack. Avoid using this. In general, see @ref item::volume instead.
         */
        units::volume integral_volume = -1_ml;

        /** Number of items per above volume for @ref count_by_charges items */
        int stack_size = 0;

        /**
         * Length of the longest side (JSON `longest_side`, as DDA). -1 until resolved in
         * Item_factory::finalize_post (explicit JSON, item_overlay, category floor, volume-derived).
         */
        units::length longest_side = -1_mm;
        /** DDA `integral_longest_side`: how far a gun mod sticks out of its gun (item::length). */
        units::length integral_longest_side = -1_mm;

        /** Value before cataclysm. Price given is for a default-sized stack. */
        units::money price = 0_cent;
        /** Value after cataclysm, dependent upon practical usages. Price given is for a default-sized stack. */
        units::money price_post = -1_cent;

        /**@}*/
        // If non-rigid volume (and if worn encumbrance) increases proportional to contents
        bool rigid = true;

        // Default item vars for the resulting item
        data_vars::data_set item_vars;

        /** Damage output in melee for zero or more damage types */
        std::array<int, NUM_DT> melee;
        /**
         * Attacks possible to execute with this weapon.
         * Keys are attack ids (not to be translated).
         * WIP feature, intended to replace @ref melee and @ref to_hit.
         */
        std::map<std::string, attack_statblock> attacks;
        /** Base damage output when thrown */
        damage_instance thrown_damage;

        int m_to_hit  = 0;  // To-hit bonus for melee combat; -5 to 5 is reasonable

        unsigned light_emission = 0;   // Exactly the same as item_tags LIGHT_*, this is for lightmap.
        std::optional<RGBColor> light_color;

        /** If set via JSON forces item category to this (preventing automatic assignment) */
        item_category_id category_force;

        std::string sym;
        nc_color color = c_white; // Color on the map (color.h)

        static constexpr int damage_scale = 1000; /** Damage scale compared to the old float damage value */

        /** What items can be used to repair this item? @see Item_factory::finalize */
        std::set<itype_id> repair;

        /** What recipes can make this item */
        std::vector<recipe_id> recipes;

        /** What faults (if any) can occur */
        std::set<fault_id> faults;

        /** Magazine types (if any) for each ammo type that can be used to reload this item */
        std::map< ammotype, std::set<itype_id> > magazines;

        /** Default magazine for each ammo type that can be used to reload this item */
        std::map< ammotype, itype_id > magazine_default;

        /** Volume above which the magazine starts to protrude from the item and add extra volume */
        units::volume magazine_well = 0_ml;

        layer_level layer = layer_level::MAX_CLOTHING_LAYER;

        /**
         * Efficiency of solar energy conversion for solarpacks.
         */
        float solar_efficiency = 0;

        // Max amount of this type that can be worn (DDA itype.h:1661, item::max_worn()).
        int max_worn = MAX_WORN_PER_TYPE;

        // Sets repair difficulty for items
        // Overrides recipe difficulty
        int repair_difficulty = -1;

        FlagsSetType item_tags;

        std::string get_item_type_string() const;

        // Returns the name of the item type in the correct language and with respect to its grammatical number,
        // based on quantity (example: item type “anvil”, nname(4) would return “anvils” (as in “4 anvils”).
        std::string nname( unsigned int quantity ) const;

        // Allow direct access to the type id for the few cases that need it.
        const itype_id &get_id() const;

        bool count_by_charges() const;
        // Separate check for stackable generics, for functions that are allowed to treat them separately from ammo or comestibles.
        bool is_stackable() const;

        int charges_default() const;

        int charges_to_use() const;

        // for tools that sub another tool, but use a different ratio of charges
        int charge_factor() const;

        int maximum_charges() const;
        bool can_have_charges() const;

        /**
         * Number of (charges of) this type of item that fit into the given volume.
         * May return 0 if not even one charge fits into the volume.
         */
        int charges_per_volume( const units::volume &vol ) const;

        bool has_use() const;

        bool has_flag( const flag_id &flag ) const;

        // returns read-only set of all item tags/flags
        const FlagsSetType &get_flags() const;

        bool can_use( const std::string &iuse_name ) const;
        const use_function *get_use( const std::string &iuse_name ) const;

        // Here "invoke" means "actively use". "Tick" means "active item working"
        int invoke( player &p, item &it,
                    const tripoint_bub_ms &pos ) const; // Picks first method or returns 0
        int invoke( player &p, item &it, const tripoint_bub_ms &pos, const std::string &iuse_name ) const;
        void tick( player &p, item &it, const tripoint_bub_ms &pos ) const;

        bool is_fuel() const;
        bool is_seed() const;
};
