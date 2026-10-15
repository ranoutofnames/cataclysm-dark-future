#include "test_data.h"

#include "json.h"
#include "pocket_type.h"  // IWYU pragma: keep // need full type to read from json

// Transplanted from DDA src/test_data.cpp b4b62254ae2
// CDF: static members limited to the carried sections.
std::map<spawn_type, std::vector<container_spawn_test_data>> test_data::container_spawn_data;
std::map<std::string, pocket_mod_test_data> test_data::pocket_mod_data;

void container_spawn_test_data::deserialize( const JsonObject &jo )
{
    jo.read( "given", given );
    jo.read( "expected", expected_amount );

    if( jo.has_member( "group" ) ) {
        jo.read( "group", group );
    }
    if( jo.has_member( "recipe" ) ) {
        jo.read( "recipe", recipe );
    }
    if( jo.has_member( "vehicle" ) ) {
        jo.read( "vehicle", vehicle );
    }
    if( jo.has_member( "profession" ) ) {
        jo.read( "profession", profession );
    }
    if( jo.has_member( "item" ) ) {
        jo.read( "item", item );
    }
    if( jo.has_member( "charges" ) ) {
        jo.read( "charges", charges );
    }
}

void pocket_mod_test_data::deserialize( const JsonObject &jo )
{
    jo.read( "base_item", base_item );
    jo.read( "mod_item", mod_item );
    jo.read( "expected_pockets", expected_pockets );
}

void test_data::load( const JsonObject &jo )
{
    if( jo.has_object( "spawn_data" ) )  {
        JsonObject spawn_jo = jo.get_object( "spawn_data" );
        if( spawn_jo.has_array( "group" ) ) {
            std::vector<container_spawn_test_data> test_groups;
            spawn_jo.read( "group", test_groups );
            container_spawn_data[spawn_type::item_group].insert(
                container_spawn_data[spawn_type::item_group].end(), test_groups.begin(), test_groups.end() );
        }
        if( spawn_jo.has_array( "recipe" ) ) {
            std::vector<container_spawn_test_data> test_recipes;
            spawn_jo.read( "recipe", test_recipes );
            container_spawn_data[spawn_type::recipe].insert( container_spawn_data[spawn_type::recipe].end(),
                    test_recipes.begin(), test_recipes.end() );
        }
        if( spawn_jo.has_array( "vehicle" ) ) {
            std::vector<container_spawn_test_data> test_vehicles;
            spawn_jo.read( "vehicle", test_vehicles );
            container_spawn_data[spawn_type::vehicle].insert( container_spawn_data[spawn_type::vehicle].end(),
                    test_vehicles.begin(), test_vehicles.end() );
        }
        if( spawn_jo.has_array( "profession" ) ) {
            std::vector<container_spawn_test_data> test_professions;
            spawn_jo.read( "profession", test_professions );
            container_spawn_data[spawn_type::profession].insert(
                container_spawn_data[spawn_type::profession].end(), test_professions.begin(),
                test_professions.end() );
        }
        if( spawn_jo.has_array( "map" ) ) {
            std::vector<container_spawn_test_data> test_map;
            spawn_jo.read( "map", test_map );
            container_spawn_data[spawn_type::map].insert( container_spawn_data[spawn_type::map].end(),
                    test_map.begin(), test_map.end() );
        }
    }

    if( jo.has_object( "pocket_mod_data" ) ) {
        std::map<std::string, pocket_mod_test_data> new_pocket_mod_data;
        jo.read( "pocket_mod_data", new_pocket_mod_data );
        pocket_mod_data.insert( new_pocket_mod_data.begin(), new_pocket_mod_data.end() );
    }
}
