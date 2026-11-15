// Item info armor coverage body graph, after DDA item_info.cpp:2138 and :2432 (the show_bodygraph
// block of item::armor_info).  CDF's armor_info() lives in item/iteminfo_armor.cpp, but the info
// assembly is item::info() in item.cpp, so the block is called from there.
#include "bodygraph.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "color.h"
#include "item.h"
#include "iteminfo_format_utils.h"
#include "iteminfo_query.h"
#include "options.h"
#include "subbodypart.h"

static const bodygraph_id bodygraph_full_body_iteminfo( "full_body_iteminfo" );

void item_bodygraph_info( const item &it, std::vector<iteminfo> &info,
                          const iteminfo_query &parts )
{
    if( !it.is_armor() ) {
        return;
    }

    const bool show_bodygraph = get_option<bool>( "ITEM_BODYGRAPH" ) &&
                                parts.test( iteminfo_parts::ARMOR_BODYGRAPH );
    if( !show_bodygraph ) {
        return;
    }

    insert_separation_line( info );

    auto bg_cb = [&it]( const bodygraph_part * bgp, const std::string & sym ) {
        if( !bgp ) {
            return sym;  // it's " "
        }
        std::set<sub_bodypart_id> grp{ bgp->sub_bodyparts.begin(), bgp->sub_bodyparts.end() };
        for( const bodypart_id &bid : bgp->bodyparts ) {
            grp.insert( bid->sub_parts.begin(), bid->sub_parts.end() );
        }
        nc_color clr = c_dark_gray;
        int cov_val = 0;
        for( const sub_bodypart_id &sid : grp ) {
            // CDF: item::get_coverage takes the parent body part id and the sub part id
            const int cov = it.get_coverage( sid->parent.id(), sid.id() );
            cov_val = cov > cov_val ? cov : cov_val;
        }
        if( cov_val <= 5 ) {
            clr = c_dark_gray;
        } else if( cov_val <= 10 ) {
            clr = c_light_gray;
        } else if( cov_val <= 25 ) {
            clr = c_light_red;
        } else if( cov_val <= 60 ) {
            clr = c_yellow;
        } else if( cov_val <= 80 ) {
            clr = c_green;
        } else {
            clr = c_light_green;
        }
        return colorize( sym, clr );
    };
    const std::vector<std::string> bg_lines = get_bodygraph_lines( get_player_character(), bg_cb,
            bodygraph_full_body_iteminfo );
    for( const std::string &line : bg_lines ) {
        // CDF: iteminfo has no is_art flag; a DESCRIPTION entry always starts and ends its own line
        info.emplace_back( "DESCRIPTION", line );
    }
    insert_separation_line( info );
}
