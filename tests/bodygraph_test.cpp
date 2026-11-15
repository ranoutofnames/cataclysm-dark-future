#include "bodygraph.h"

#include <algorithm>
#include <string>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "catacharset.h"
#include "catch/catch.hpp"
#include "item.h"
#include "player_helpers.h"
#include "subbodypart.h"
#include "type_id.h"

static const bodygraph_id bodygraph_full_body_iteminfo( "full_body_iteminfo" );

namespace
{

// Same shape as the iteminfo_bodygraph.cpp callback: '#' where the armor covers the part, '.' for
// a graph section the armor does not cover, ' ' for cells that belong to no section.
auto coverage_cb( const item &armor ) -> bodygraph_callback
{
    return [&armor]( const bodygraph_part * bgp, const std::string & sym ) -> std::string {
        if( bgp == nullptr ) {
            return sym;
        }
        int cov_val = 0;
        for( const bodypart_id &bid : bgp->bodyparts ) {
            for( const sub_bodypart_str_id &sbp : bid->sub_parts ) {
                cov_val = std::max( cov_val, armor.get_coverage( bid, sbp ) );
            }
        }
        for( const sub_bodypart_id &sid : bgp->sub_bodyparts ) {
            cov_val = std::max( cov_val, armor.get_coverage( sid->parent.id(), sid.id() ) );
        }
        return cov_val > 0 ? "#" : ".";
    };
}

} // namespace

TEST_CASE( "bodygraph_data_loads", "[bodygraph]" )
{
    CHECK( !bodygraph::get_all().empty() );
    REQUIRE( bodygraph_full_body_iteminfo.is_valid() );

    const bodygraph &graph = bodygraph_full_body_iteminfo.obj();
    CHECK( graph.id == bodygraph_full_body_iteminfo );
    CHECK( graph.rows.size() == 20 );
    CHECK( !graph.parts.empty() );
    // The item info graph names every sub body part of the default anatomy.
    CHECK( graph.parts.count( "a" ) == 1 );
    CHECK( !graph.parts.at( "a" ).sub_bodyparts.empty() );
}

TEST_CASE( "bodygraph_renders_armor_coverage", "[bodygraph]" )
{
    clear_avatar();

    const item armor( itype_id( "tshirt" ) );
    REQUIRE( armor.is_armor() );
    REQUIRE( armor.covers( bodypart_id( "torso" ) ) );

    const std::vector<std::string> lines = get_bodygraph_lines( get_avatar(), coverage_cb( armor ),
                                            bodygraph_full_body_iteminfo );
    REQUIRE( lines.size() == 20 );
    for( const std::string &line : lines ) {
        // The graph keeps the 40-column layout of the data file.
        CHECK( utf8_width( line ) == 40 );
    }

    // A t-shirt covers the torso, so the graph renders covered sections.
    const bool covered = std::any_of( lines.begin(), lines.end(), []( const std::string & line ) {
        return line.find( '#' ) != std::string::npos;
    } );
    CHECK( covered );

    // Sections the shirt does not cover (row 0 is the head crown) stay uncovered.
    CHECK( lines[0].find( '.' ) != std::string::npos );
}
