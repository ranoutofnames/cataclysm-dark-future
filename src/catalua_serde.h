#pragma once

#include <string>

#include "catalua_sol_fwd.h"

class JsonObject;
class JsonOut;

namespace cata
{

// `context` names the table (the mod id for mod storage) in messages about skipped entries.
void serialize_lua_table( const sol::table &t, JsonOut &jsout, const std::string &context = {} );
void deserialize_lua_table( sol::table t, JsonObject &obj, const std::string &context = {} );

} // namespace cata


