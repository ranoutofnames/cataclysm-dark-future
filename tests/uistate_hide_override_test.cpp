#include "catch/catch.hpp"
#include "json.h"
#include "uistate.h"

#include <optional>
#include <sstream>
#include <string>

// CDF: P3-UI3 fix A-L3. uistatedata::hide_entries_override is an std::optional<bool> (DDA uistate.h:192).
// Loading a save that lacks the key must reset it to nullopt instead of keeping whatever the
// in-memory value was (for example from the game that was open before the load).

namespace
{
std::optional<bool> load_override( const std::string &json_text, std::optional<bool> before )
{
    std::istringstream in( json_text );
    JsonIn jsin( in );
    JsonObject jo = jsin.get_object();
    uistatedata loaded;
    loaded.hide_entries_override = before;
    loaded.deserialize( jo );
    return loaded.hide_entries_override;
}
} // namespace

TEST_CASE( "uistate_hide_entries_override_round_trip", "[p3][ui][uistate]" )
{
    for( const bool flag : { true, false } ) {
        uistatedata saved;
        saved.hide_entries_override = flag;
        std::ostringstream out;
        JsonOut jsout( out );
        saved.serialize( jsout );

        // start from the opposite value so that a missing read cannot pass for a read one
        const std::optional<bool> loaded = load_override( out.str(), !flag );
        REQUIRE( loaded.has_value() );
        CHECK( *loaded == flag );
    }
}

TEST_CASE( "uistate_hide_entries_override_missing_key_resets_to_default", "[p3][ui][uistate]" )
{
    CHECK_FALSE( uistatedata().hide_entries_override.has_value() );
    // a save from before the key existed: the stale in-memory value must not survive the load
    CHECK_FALSE( load_override( "{}", true ).has_value() );
    CHECK_FALSE( load_override( "{}", false ).has_value() );
}
