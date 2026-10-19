#pragma once

#include <functional>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <vector>

#include "coordinates.h"
#include "enums.h"
#include "item_stack.h"

class JsonIn;
class JsonOut;
class item;
struct itype;

namespace auto_pickup
{
/**
 * The currently-active set of auto-pickup rules, in a form that allows quick
 * lookup. When this is filled (by @ref auto_pickup::create_rule()), every
 * item existing in the game that matches a rule (either white- or blacklist)
 * is added as the key, with RULE_WHITELISTED or RULE_BLACKLISTED as the values.
 */
class cache : public std::unordered_map<std::string, rule_state>
{
    public:
        /// Defines whether this cache has been filled.
        bool ready = false;

        /// Temporary data used while filling the cache.
        std::unordered_map<std::string, const itype *> temp_items;
};

/**
 * A single entry in the list of auto pickup entries @ref rule_list.
 * The data contained can be edited by the player and determines what to pick/ignore.
 */
class rule
{
    public:
        std::string sRule = "";
        bool bActive = false;
        bool bExclude = false;

        rule() = default;

        rule( const std::string &r, const bool a, const bool e ) : sRule( r ), bActive( a ), bExclude( e ) {
        }

        void serialize( JsonOut &jsout ) const;
        void deserialize( JsonIn &jsin );

        void test_pattern() const;
};

/**
 * A list of rules. This is primarily a container with a few convenient functions (like saving/loading).
 */
class rule_list : public std::vector<rule>
{
    public:
        void serialize( JsonOut &jsout ) const;
        void deserialize( JsonIn &jsin );

        void refresh_map_items( cache &map_items ) const;

        void create_rule( cache &map_items, const std::string &to_match );
        void create_rule( cache &map_items, const item &it );
};

class user_interface
{
    public:
        class tab
        {
            public:
                std::string title;
                rule_list new_rules;
                std::reference_wrapper<rule_list> rules;

                tab( const std::string &t, rule_list &r ) : title( t ), new_rules( r ), rules( r ) { }
        };

        std::string title;
        std::vector<tab> tabs;
        bool is_autopickup = false;

        void show();

        bool bStuffChanged = false;
};

class base_settings
{
    protected:
        mutable cache map_items;

        void invalidate();

    private:
        virtual void refresh_map_items( cache &map_items ) const = 0;

        void recreate() const;

    public:
        virtual ~base_settings() = default;
        rule_state check_item( const std::string &sItemName ) const;
};

class player_settings : public base_settings
{
    private:
        void load( bool bCharacter );
        bool save( bool bCharacter );

        rule_list global_rules;
        rule_list character_rules;

        void refresh_map_items( cache &map_items ) const override;

    public:
        ~player_settings() override = default;
        using base_settings::check_item;
        /**
         * DDA auto_pickup.cpp:794: rule state of the item's name; builds the cache entry
         * for the item on first sight (matches filter and wildcard rules against the item).
         */
        rule_state check_item( const item &it );
        void create_rule( const item *it );
        bool has_rule( const item *it );
        /** @param include true adds a whitelist rule, false a blacklist rule (DDA add_rule). */
        void add_rule( const item *it, bool include = true );
        void remove_rule( const item *it );

        void clear_character_rules();

        void show();
        bool save_character();
        bool save_global();
        void load_character();
        void load_global();

        bool empty() const;
};

class npc_settings : public base_settings
{
    private:
        rule_list rules;

        void refresh_map_items( cache &map_items ) const override;

    public:
        ~npc_settings() override = default;
        void create_rule( const std::string &to_match );

        void show( const std::string &name );

        void serialize( JsonOut &jsout ) const;
        void deserialize( JsonIn &jsin );

        bool empty() const;
};

bool test_pattern_function( const itype &type, std::string filter );
bool autopickup_item_function( const item &object, std::string filter );

/**
 * Select which items on the map tile should be auto-picked up (DDA auto_pickup.cpp:246).
 * The return value holds the selected items; nested items inside a container on the tile are
 * returned when only part of a container is picked.
 * CDF: DDA returns drop_locations (item_location + count); post-IV CDF returns item pointers,
 * the count is always the whole item (DDA passes 0 there as well).
 *
 * @param from stack of item entries on a map tile (or vehicle cargo tile).
 * @param location where the stack of items is located on the map.
 */
std::vector<item *> select_items( const std::vector<item_stack::iterator> &from,
                                  const tripoint_bub_ms &location );

} // namespace auto_pickup

auto_pickup::player_settings &get_auto_pickup();


