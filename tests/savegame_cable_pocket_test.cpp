#include "catch/catch.hpp"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"
#include "json.h"
#include "state_helpers.h"
#include "type_id.h"

#include <sstream>
#include <string>
#include <vector>

// CDF: E24 regression test (area-engine.md row E24, GAP-STUDY.md "CABLE save bug").
// pocket_type_to_dda had no CABLE case, so a CABLE pocket was saved as the MIGRATION int (8) and
// the load mapped it back to MIGRATION: item_contents::deserialize then re-routed the extension
// cables through insert_item and the link chain was lost. DDA saves the enum int (DDA
// savegame_json.cpp:280-291) with CABLE = 7 (DDA pocket_type.h:8-18).

namespace
{

// CDF: the JSON round trip helpers other save tests use
// (tests/item_contents_migration_test.cpp:23-34).
auto save_item( const item &it ) -> std::string
{
    std::ostringstream buffer;
    JsonOut jsout( buffer );
    it.serialize( jsout );
    return buffer.str();
}

auto load_item( const std::string &json ) -> detached_ptr<item>
{
    std::istringstream buffer( json );
    JsonIn jsin( buffer );
    return item::spawn( jsin );
}

auto cable_pockets( const item &it ) -> std::vector<const item_pocket *>
{
    return it.get_contents().get_pockets( pocket_type::CABLE );
}

} // namespace

TEST_CASE( "a CABLE pocket keeps its type through a save and load", "[pocket][savegame][cable]" )
{
    clear_all_state();

    const itype_id cable_id( "test_linkupb_cable" );
    detached_ptr<item> cable = item::spawn( cable_id );
    REQUIRE( cable );
    // the loader adds the CABLE pocket to every link_up item (src/item_factory.cpp
    // add_cable_pocket, LINKUP1b)
    REQUIRE( cable_pockets( *cable ).size() == 1 );

    const std::string saved = save_item( *cable );
    INFO( saved );
    // DDA int 7 is CABLE; 8 would be MIGRATION
    CHECK( saved.find( "\"pocket_type\":7" ) != std::string::npos );

    detached_ptr<item> loaded = load_item( saved );
    REQUIRE( loaded );
    CHECK( loaded->typeId() == cable_id );
    CHECK( cable_pockets( *loaded ).size() == 1 );
}

TEST_CASE( "an extension cable stays in the CABLE pocket across a save",
           "[pocket][savegame][cable]" )
{
    clear_all_state();

    detached_ptr<item> cable = item::spawn( itype_id( "test_linkupb_cable" ) );
    REQUIRE( cable );
    item extension( itype_id( "test_linkupb_extension" ) );
    REQUIRE( cable->put_in( extension, pocket_type::CABLE ).success() );
    REQUIRE( cable->cables().size() == 1 );

    detached_ptr<item> loaded = load_item( save_item( *cable ) );
    REQUIRE( loaded );
    // the owner-visible symptom of E24: after a save and load the extension is no longer a cable
    // in the CABLE pocket, so has_extensions / cables() is empty (iuse_actor.cpp link_up menu)
    REQUIRE( loaded->cables().size() == 1 );
    const item *restored = loaded->cables().front();
    const item_pocket *pocket = loaded->contents.pocket_of( restored );
    REQUIRE( pocket != nullptr );
    CHECK( pocket->is_type( pocket_type::CABLE ) );
}
