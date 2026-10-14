#include "catalua_serde.h"

#include "catalua_bindings_coords_common.h"
#include "catalua_impl.h"
#include "catalua_sol.h"
#include "debug.h"
#include "json.h"
#include "string_formatter.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cata
{
// Builds the path of a value in a saved table for messages. It is only called when something is
// reported, so no work is spent on it for the entries that are saved without problems.
using lua_path_fn = std::function<std::string()>;

void serialize_lua_table_internal( const sol::table &t, JsonOut &jsout,
                                   std::vector<sol::table> &stack, const lua_path_fn &path );

namespace
{

constexpr auto point_coord_kind = std::string_view { "PointCoord" };
constexpr auto tripoint_coord_kind = std::string_view { "TripointCoord" };

auto serialize_lua_coord_kind( const std::string_view kind, JsonOut &jsout ) -> void
{
    jsout.member_as_string( "type", "userdata" );
    jsout.member_as_string( "kind", std::string( kind ) );
    jsout.member( "data" );
}

auto serialize_lua_point_coord_data( const detail::lua_coords::lua_point_coord &coord,
                                     JsonOut &jsout ) -> void
{
    jsout.start_object();
    jsout.member_as_string( "origin",
                            std::string( detail::lua_coords::origin_lua_name( coord.origin ) ) );
    jsout.member_as_string( "scale", std::string( detail::lua_coords::scale_lua_name( coord.scale ) ) );
    jsout.member( "raw" );
    coord.raw.serialize( jsout );
    jsout.end_object();
}

auto serialize_lua_tripoint_coord_data( const detail::lua_coords::lua_tripoint_coord &coord,
                                        JsonOut &jsout ) -> void
{
    jsout.start_object();
    jsout.member_as_string( "origin",
                            std::string( detail::lua_coords::origin_lua_name( coord.origin ) ) );
    jsout.member_as_string( "scale", std::string( detail::lua_coords::scale_lua_name( coord.scale ) ) );
    jsout.member( "raw" );
    coord.raw.serialize( jsout );
    jsout.end_object();
}

auto try_serialize_lua_coord( const sol::object &val, JsonOut &jsout ) -> bool
{
    if( val.is<detail::lua_coords::lua_point_coord>() ) {
        serialize_lua_coord_kind( point_coord_kind, jsout );
        serialize_lua_point_coord_data( val.as<detail::lua_coords::lua_point_coord>(), jsout );
        return true;
    }
    if( val.is<detail::lua_coords::lua_tripoint_coord>() ) {
        serialize_lua_coord_kind( tripoint_coord_kind, jsout );
        serialize_lua_tripoint_coord_data( val.as<detail::lua_coords::lua_tripoint_coord>(), jsout );
        return true;
    }
    return false;
}

auto deserialize_lua_point_coord_data( const JsonObject &data ) ->
std::optional<detail::lua_coords::lua_point_coord>
{
    const auto origin = detail::lua_coords::parse_origin( data.get_string( "origin" ) );
    const auto scale = detail::lua_coords::parse_scale( data.get_string( "scale" ) );
    if( !origin || !scale ) {
        debugmsg( "Failed to deserialize PointCoord: invalid coordinate kind." );
        return std::nullopt;
    }

    auto raw = point{};
    if( !data.read( "raw", raw ) ) {
        debugmsg( "Failed to deserialize PointCoord: missing raw coordinate." );
        return std::nullopt;
    }

    try {
        return detail::lua_coords::make_point_coord( *origin, *scale, raw );
    } catch( const std::exception &err ) {
        debugmsg( "Failed to deserialize PointCoord: %s", err.what() );
        return std::nullopt;
    }
}

auto deserialize_lua_tripoint_coord_data( const JsonObject &data ) ->
std::optional<detail::lua_coords::lua_tripoint_coord>
{
    const auto origin = detail::lua_coords::parse_origin( data.get_string( "origin" ) );
    const auto scale = detail::lua_coords::parse_scale( data.get_string( "scale" ) );
    if( !origin || !scale ) {
        debugmsg( "Failed to deserialize TripointCoord: invalid coordinate kind." );
        return std::nullopt;
    }

    auto raw = tripoint{};
    if( !data.read( "raw", raw ) ) {
        debugmsg( "Failed to deserialize TripointCoord: missing raw coordinate." );
        return std::nullopt;
    }

    try {
        return detail::lua_coords::make_tripoint_coord( *origin, *scale, raw );
    } catch( const std::exception &err ) {
        debugmsg( "Failed to deserialize TripointCoord: %s", err.what() );
        return std::nullopt;
    }
}

auto deserialize_lua_coord( sol::state_view lua, const JsonObject &jo,
                            const std::string &kind ) -> sol::object
{
    const auto data = jo.get_object( "data" );
    if( kind == point_coord_kind ) {
        const auto coord = deserialize_lua_point_coord_data( data );
        if( coord ) {
            return sol::make_object( lua, *coord );
        }
    } else if( kind == tripoint_coord_kind ) {
        const auto coord = deserialize_lua_tripoint_coord_data( data );
        if( coord ) {
            return sol::make_object( lua, *coord );
        }
    }
    return sol::nil;
}

} // namespace

// Serializes one Lua value as a JSON object. Returns false if the value could not be
// serialized (after reporting it); the output is then incomplete and has to be discarded.
static bool serialize_lua_object( const sol::object &val, JsonOut &jsout,
                                  std::vector<sol::table> &stack, const lua_path_fn &path )
{
    sol::state_view lua( val.lua_state() );
    auto ok = true;

    jsout.start_object();
    switch( val.get_type() ) {
        case sol::type::boolean: {
            jsout.member_as_string( "type", "bool" );
            jsout.member( "data" );
            jsout.write( val.as<bool>() );
            break;
        }
        case sol::type::number: {
            // There's no clear difference in JSON between int and float,
            // so we have to be explicit to avoid subtle errors down the line
            if( is_number_integer( lua, val ) ) {
                jsout.member_as_string( "type", "int" );
                jsout.member( "data" );
                jsout.write( val.as<int64_t>() );
            } else {
                jsout.member_as_string( "type", "float" );
                jsout.member( "data" );
                jsout.write( val.as<double>() );
            }
            break;
        }
        case sol::type::string: {
            jsout.member_as_string( "type", "string" );
            jsout.member( "data" );
            jsout.write( val.as<std::string>() );
            break;
        }
        case sol::type::table: {
            jsout.member_as_string( "type", "lua_table" );
            jsout.member( "data" );
            serialize_lua_table_internal( val.as<sol::table>(), jsout, stack, path );
            break;
        }
        case sol::type::userdata: {
            std::optional<std::string> luna_type = get_luna_type( val );
            if( !luna_type ) {
                debugmsg( "Tried to serialize usertype that was not registered with luna. "
                          "Not saving the entry at '%s'.", path() );
                ok = false;
                break;
            }
            if( try_serialize_lua_coord( val, jsout ) ) {
                break;
            }
            sol::table table_val = val.as<sol::table>();
            sol::protected_function serialize_func = table_val["serialize"];
            if( !serialize_func ) {
                debugmsg( "Tried to serialize usertype that does not allow serialization. "
                          "Not saving the entry at '%s' (usertype '%s').", path(), *luna_type );
                ok = false;
                break;
            }
            jsout.member_as_string( "type", "userdata" );
            jsout.member_as_string( "kind", *luna_type );
            jsout.member( "data" );
            sol::protected_function_result res = serialize_func( val, jsout );
            if( res.status() != sol::call_status::ok ) {
                sol::error err = res;
                debugmsg( "Not saving Lua value at '%s': failed to serialize type '%s': %s", path(),
                          *luna_type, err.what() );
                ok = false;
            }
            break;
        }
        default: {
            debugmsg( "Not saving Lua value at '%s': unsupported type '%s'.", path(),
                      sol::type_name( lua.lua_state(), val.get_type() ) );
            ok = false;
            break;
        }
    }
    jsout.end_object();
    return ok;
}

namespace
{

// A table key or value that has been checked for being saveable before anything is written, so
// that an entry that cannot be saved is left out as a whole instead of leaving a broken one.
struct prepared_lua_value {
    bool ok = false;
    // Userdata can only fail while it is being written, so it is written to a string first.
    std::optional<std::string> raw;
};

// Only used for messages, so it is called when something is reported. Numbers are formatted
// like Lua's tostring does, without calling into Lua (a mod could have replaced tostring).
auto lua_key_name( const sol::object &key ) -> std::string
{
    sol::state_view lua( key.lua_state() );
    switch( key.get_type() ) {
        case sol::type::string:
            return key.as<std::string>();
        case sol::type::number: {
            if( is_number_integer( lua, key ) ) {
                return std::to_string( key.as<int64_t>() );
            }
            auto name = string_format( "%.14g", key.as<double>() );
            if( name.find_first_not_of( "-0123456789" ) == std::string::npos ) {
                name += ".0";
            }
            return name;
        }
        default:
            return string_format( "<%s>", sol::type_name( lua.lua_state(), key.get_type() ) );
    }
}

auto prepare_lua_value( const sol::object &val, std::vector<sol::table> &stack,
                        const lua_path_fn &path ) -> prepared_lua_value
{
    switch( val.get_type() ) {
        case sol::type::boolean:
        case sol::type::number:
        case sol::type::string:
            return { true, std::nullopt };
        case sol::type::table: {
            const sol::table t = val.as<sol::table>();
            const auto recursive = std::ranges::any_of( stack, [&t]( const sol::table & it ) {
                return it == t;
            } );
            if( recursive ) {
                debugmsg( "Tried to serialize recursive table structure. "
                          "Not saving the entry at '%s'.", path() );
                return {};
            }
            return { true, std::nullopt };
        }
        case sol::type::userdata: {
            auto buffer = std::ostringstream{};
            auto out = JsonOut( buffer );
            if( !serialize_lua_object( val, out, stack, path ) ) {
                return {};
            }
            return { true, buffer.str() };
        }
        default: {
            sol::state_view lua( val.lua_state() );
            debugmsg( "Not saving Lua value at '%s': unsupported type '%s'.", path(),
                      sol::type_name( lua.lua_state(), val.get_type() ) );
            return {};
        }
    }
}

void write_prepared_lua_value( const sol::object &val, const prepared_lua_value &prepared,
                               JsonOut &jsout, std::vector<sol::table> &stack,
                               const lua_path_fn &path )
{
    if( prepared.raw ) {
        jsout.write_separator();
        *jsout.get_stream() << *prepared.raw;
        jsout.set_need_separator();
    } else {
        serialize_lua_object( val, jsout, stack, path );
    }
}

} // namespace

void serialize_lua_table_internal( const sol::table &t, JsonOut &jsout,
                                   std::vector<sol::table> &stack, const lua_path_fn &path )
{
    stack.push_back( t );

    jsout.start_object();

    if( !t.empty() ) {
        jsout.member( "entries" );
        jsout.start_array();

        // TODO: persistent key order?
        t.for_each( [&]( const sol::object & key, const sol::object & val ) {
            // An entry that cannot be saved is left out as a whole (reported once, with its path)
            // so that the saved file stays loadable.
            const lua_path_fn entry_path = [&path, &key]() {
                const auto parent = path();
                const auto key_name = lua_key_name( key );
                return parent.empty() ? key_name : parent + "." + key_name;
            };
            const auto prepared_key = prepare_lua_value( key, stack, entry_path );
            if( !prepared_key.ok ) {
                return;
            }
            const auto prepared_val = prepare_lua_value( val, stack, entry_path );
            if( !prepared_val.ok ) {
                return;
            }
            write_prepared_lua_value( key, prepared_key, jsout, stack, entry_path );
            write_prepared_lua_value( val, prepared_val, jsout, stack, entry_path );
        } );

        jsout.end_array();
    }
    jsout.end_object();

    stack.pop_back();
}

void serialize_lua_table( const sol::table &t, JsonOut &jsout, const std::string &context )
{
    std::vector<sol::table> stack;
    const lua_path_fn path = [&context]() {
        return context;
    };
    serialize_lua_table_internal( t, jsout, stack, path );
}

static sol::object
deserialize_lua_object( sol::state_view lua, const JsonObject &jo )
{
    std::string entry_type = jo.get_member( "type" );
    if( entry_type == "string" ) {
        std::string data = jo.get_string( "data" );
        return sol::object( lua, sol::in_place, data );
    } else if( entry_type == "bool" ) {
        bool data = jo.get_bool( "data" );
        return sol::object( lua, sol::in_place, data );
    } else if( entry_type == "int" ) {
        int data = jo.get_int( "data" );
        return sol::object( lua, sol::in_place, data );
    } else if( entry_type == "float" ) {
        double data = jo.get_float( "data" );
        return sol::object( lua, sol::in_place, data );
    } else if( entry_type == "lua_table" ) {
        JsonObject data = jo.get_object( "data" );
        sol::table new_table = lua.create_table();
        deserialize_lua_table( new_table, data );
        return new_table;
    } else if( entry_type == "userdata" ) {
        std::string kind = jo.get_member( "kind" );
        if( kind == point_coord_kind || kind == tripoint_coord_kind ) {
            return deserialize_lua_coord( lua, jo, kind );
        }
        JsonIn &data_raw = *jo.get_raw( "data" );
        // Horrible hack ahead
        std::string script = string_format( "return %s.new()", kind );
        sol::protected_function_result res = lua.script( script, "deserialize_init", sol::load_mode::any );
        if( res.status() != sol::call_status::ok ) {
            debugmsg( "Failed to init container for deserializable type '%s'", kind );
        } else {
            sol::object obj = res;
            sol::table obj_table = obj.as<sol::table>();
            if( !obj_table ) {
                debugmsg( "Failed to init container for deserializable type '%s'", kind );
            } else {
                sol::protected_function deserialize_func = obj_table["deserialize"];
                if( !deserialize_func ) {
                    debugmsg( "Failed to deserialize type '%s': not deserializable.", kind );
                } else {
                    sol::protected_function_result res = deserialize_func( obj, data_raw );
                    if( res.status() != sol::call_status::ok ) {
                        sol::error err = res;
                        debugmsg( "Failed to deserialize type '%s': %s", kind, err.what() );
                    } else {
                        return obj;
                    }
                }
            }
        }
    } else {
        debugmsg( "Deserialization of record type '%s' is not implemented.", entry_type );
    }
    return sol::nil;
}

void deserialize_lua_table( sol::table t, JsonObject &obj, const std::string &context )
{
    sol::state_view lua( t.lua_state() );

    if( !obj.has_array( "entries" ) ) {
        return;
    }

    JsonArray arr = obj.get_array( "entries" );
    if( arr.size() % 2 != 0 ) {
        debugmsg( "invalid array size %d", arr.size() );
        return;
    }
    // One entry that cannot be read must not prevent the rest of the table (and the tables
    // after it) from being restored, so each key/value pair is read on its own.
    for( size_t idx = 0; idx < arr.size(); idx += 2 ) {
        std::string entry_name = string_format( "entry #%d", idx / 2 );
        try {
            JsonObject jo_key = arr.get_object( idx );
            if( !jo_key.has_member( "type" ) ) {
                jo_key.allow_omitted_members();
                debugmsg( "Skipping unreadable Lua data %s in '%s': the key is empty or incomplete.",
                          entry_name, context );
                continue;
            }
            sol::object key = deserialize_lua_object( lua, jo_key );
            if( key.get_type() == sol::type::string ) {
                entry_name = string_format( "key '%s'", key.as<std::string>() );
            }
            if( key.get_type() == sol::type::lua_nil || key.get_type() == sol::type::none ) {
                // The reason was already reported; don't report the unread members as well.
                jo_key.allow_omitted_members();
                debugmsg( "Skipping Lua data %s in '%s': the key could not be restored.", entry_name,
                          context );
                continue;
            }
            JsonObject jo_val = arr.get_object( idx + 1 );
            if( !jo_val.has_member( "type" ) ) {
                jo_val.allow_omitted_members();
                debugmsg( "Skipping unreadable Lua data %s in '%s': the value is empty or incomplete.",
                          entry_name, context );
                continue;
            }
            sol::object val = deserialize_lua_object( lua, jo_val );
            if( val.get_type() == sol::type::lua_nil || val.get_type() == sol::type::none ) {
                jo_val.allow_omitted_members();
                debugmsg( "Skipping Lua data %s in '%s': the value could not be restored.", entry_name,
                          context );
                continue;
            }
            t.set( key, val );
        } catch( const std::exception &err ) {
            debugmsg( "Skipping unreadable Lua data %s in '%s': %s", entry_name, context, err.what() );
        }
    }
}

} // namespace cata
