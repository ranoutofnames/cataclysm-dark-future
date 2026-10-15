#pragma once

#include <string>

#include "catalua_luna.h"
#include "catalua_sol.h"

/// Result of a fallible Lua binding: whether it worked and, if not, why.
/// Lua receives it as two return values: `ok, reason`.
struct lua_bool_reason {
    bool ok = false;
    std::string reason;
};

LUNA_DOC( lua_bool_reason, "(boolean, string)" );

namespace sol::stack
{
template<>
struct unqualified_pusher<lua_bool_reason> {
    static auto push( lua_State *L, const lua_bool_reason &res ) -> int {
        const auto first = stack::push( L, res.ok );
        return first + stack::push( L, res.reason );
    }
};
} // namespace sol::stack
