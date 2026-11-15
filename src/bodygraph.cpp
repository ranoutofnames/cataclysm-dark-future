#include "bodygraph.h"

#include <algorithm>
#include <cstddef>
#include <initializer_list>

#include "bodypart.h"
#include "catacharset.h"
#include "character.h"
#include "color.h"
#include "debug.h"
#include "generic_factory.h"
#include "json.h"
#include "string_formatter.h"
#include "subbodypart.h"
#include "translations.h"
#include "type_id_implement.h"

#define BPGRAPH_MAXROWS 20
#define BPGRAPH_MAXCOLS 40

namespace
{

generic_factory<bodygraph> bodygraph_factory( "bodygraph" );

} // namespace

// CDF: CDF has no has_part()/body_part_filter; a part is present when it is in the character's
// anatomy map.  CDF only defines human_anatomy and default_anatomy, so the ids match directly.
static bool has_bodypart( const Character &u, const bodypart_str_id &bp )
{
    return u.get_body().count( bp ) > 0;
}

static bool has_bodypart( const Character &u, const bodypart_id &bp )
{
    return has_bodypart( u, bp.id() );
}

IMPLEMENT_STRING_AND_INT_IDS( bodygraph, bodygraph_factory );

// CDF: string_id_null_ids.cpp is not part of this port's file set; DDA defines the same null id
// there (MAKE_NULL_ID2( bodygraph, "" )).
template<>
const string_id<bodygraph> &string_id<bodygraph>::NULL_ID()
{
    static string_id<bodygraph> id = string_id<bodygraph>( "" );
    return id;
}

void bodygraph::load_bodygraphs( const JsonObject &jo, const std::string &src )
{
    bodygraph_factory.load( jo, src );
}

void bodygraph::reset()
{
    bodygraph_factory.reset();
}

const std::vector<bodygraph> &bodygraph::get_all()
{
    return bodygraph_factory.get_all();
}

void bodygraph::finalize_all()
{
    bodygraph_factory.finalize();
}

void bodygraph::check_all()
{
    bodygraph_factory.check();
}

void bodygraph::load( const JsonObject &jo, const std::string & )
{
    optional( jo, was_loaded, "parent_bodypart", parent_bp );
    optional( jo, was_loaded, "fill_sym", fill_sym );
    if( jo.has_string( "fill_color" ) ) {
        fill_color = color_from_string( jo.get_string( "fill_color" ) );
    }

    if( jo.has_string( "mirror" ) ) {
        mirror.reset();
        mandatory( jo, false, "mirror", mirror );
    } else if( !was_loaded || jo.has_array( "rows" ) ) {
        rows.clear();
        fill_rows.clear();
        for( const JsonValue jval : jo.get_array( "rows" ) ) {
            if( !jval.test_string() ) {
                jval.throw_error( "\"rows\" array must contain string values." );
            } else {
                rows.emplace_back( utf8_display_split( jval.get_string() ) );
            }
        }
        if( jo.has_array( "fill_rows" ) ) {
            for( const JsonValue jval : jo.get_array( "fill_rows" ) ) {
                if( !jval.test_string() ) {
                    jval.throw_error( "\"rows\" array must contain string values." );
                } else {
                    fill_rows.emplace_back( utf8_display_split( jval.get_string() ) );
                }
            }
        }
    }

    if( !was_loaded || jo.has_object( "parts" ) ) {
        parts.clear();
        for( const JsonMember memb : jo.get_object( "parts" ) ) {
            const std::string &sym = memb.name();
            JsonObject mobj = memb.get_object();
            bodygraph_part bpg;
            optional( mobj, false, "body_parts", bpg.bodyparts );
            optional( mobj, false, "sub_body_parts", bpg.sub_bodyparts );
            optional( mobj, false, "sym", bpg.sym, fill_sym );
            if( mobj.has_string( "select_color" ) ) {
                bpg.sel_color = color_from_string( mobj.get_string( "select_color" ) );
            } else {
                bpg.sel_color = fill_color;
            }
            optional( mobj, false, "nested_graph", bpg.nested_graph );
            parts.emplace( sym, bpg );
        }
    }

    if( jo.has_string( "label_fill" ) ) {
        label_fill = jo.get_string( "label_fill" );
    }

}

void bodygraph::finalize()
{
    if( rows.size() > BPGRAPH_MAXROWS ) {
        debugmsg( "body_graph \"%s\" defines more rows than the maximum (%d).", id.c_str(),
                  BPGRAPH_MAXROWS );
    }

    if( !fill_rows.empty() &&  fill_rows.size() != rows.size() ) {
        debugmsg( "body_graph \"%s\" defines a different number of fill_rows than rows (%d vs. %d).",
                  id.c_str(),
                  fill_rows.size(), rows.size() );
    }

    int width = -1;
    bool w_warned = false;
    for( size_t i = 0; i < rows.size(); i++ ) {
        std::vector<std::string> &r = rows[i];
        int w = r.size();
        if( width == -1 ) {
            width = w;
        } else if( !w_warned && width != w ) {
            debugmsg( "body_graph \"%s\" defines rows with different widths.", id.c_str() );
            w_warned = true;
        } else if( !w_warned && w > BPGRAPH_MAXCOLS ) {
            debugmsg( "body_graph \"%s\" defines rows with more columns than the maximum (%d).", id.c_str(),
                      BPGRAPH_MAXCOLS );
            w_warned = true;
        }

        r.insert( r.begin(), ( BPGRAPH_MAXCOLS - w ) / 2, " " );
        r.insert( r.end(), BPGRAPH_MAXCOLS - r.size(), " " );

        if( ! fill_rows.empty() ) {
            std::vector<std::string> &fr = fill_rows[i];
            if( fr.size() != r.size() ) {
                debugmsg( "body_graph \"%s\" defines a different number of columns in fill_rows than in rows (%d vs. %d).",
                          id.c_str(),
                          fr.size(), w );
            }
            fr.insert( fr.begin(), ( BPGRAPH_MAXCOLS - w ) / 2, " " );
            fr.insert( fr.end(), BPGRAPH_MAXCOLS - fr.size(), " " );
        }

    }

    for( std::vector<std::vector<std::string>> temp_rows : {
             rows, fill_rows
         } ) {
        if( !temp_rows.empty() ) {
            for( int i = ( BPGRAPH_MAXROWS - temp_rows.size() ) / 2; i > 0; i-- ) {
                std::vector<std::string> r;
                r.insert( r.begin(), BPGRAPH_MAXCOLS, " " );
                temp_rows.insert( temp_rows.begin(), r );
            }

            for( int i = temp_rows.size(); i <= BPGRAPH_MAXROWS; i++ ) {
                std::vector<std::string> r;
                r.insert( r.begin(), BPGRAPH_MAXCOLS, " " );
                temp_rows.emplace_back( r );
            }
        }
    }
}

void bodygraph::check() const
{
    if( parts.empty() ) {
        debugmsg( "body_graph \"%s\" defined without parts.", id.c_str() );
    }
    for( const auto &bgp : parts ) {
        if( utf8_width( bgp.first ) > 1 ) {
            debugmsg( "part \"%s\" in body_graph \"%s\" is more than 1 character.", bgp.first, id.c_str() );
        }
        if( bgp.second.bodyparts.empty() && bgp.second.sub_bodyparts.empty() ) {
            debugmsg( "part \"%s\" in body_graph \"%s\" contains no body_parts or sub_body_parts definitions.",
                      bgp.first, id.c_str() );
        }
    }
}

/***************************** Display routines *****************************/

static const bodygraph_id &get_bg_rows( const bodygraph_id &bgid )
{
    if( !!bgid->mirror ) {
        return get_bg_rows( bgid->mirror.value() );
    }
    // bodygraph_id is a string_id with stable storage; callers do not pass temporaries.
    // NOLINTNEXTLINE(bugprone-return-const-ref-from-parameter)
    return bgid;
}

std::vector<std::string> get_bodygraph_lines( const Character &u,
        const bodygraph_callback &fragment_cb, const bodygraph_id &id, int width, int height,
        std::string_view label )
{
    width = ( width <= 0 || width > BPGRAPH_MAXCOLS ) ? BPGRAPH_MAXCOLS : width;
    height = ( height <= 0 || height > BPGRAPH_MAXROWS ) ? BPGRAPH_MAXROWS : height;

    std::vector<std::string> ret;

    // Bodypart not present on character
    if( !!id->parent_bp && !has_bodypart( u, *id->parent_bp ) ) {
        std::string txt = string_format( u.is_avatar() ?
                                         //~ 1$ = 2nd person pronoun (You), 2$ = body part (left arm)
                                         _( "%1$s do not have a %2$s." ) :
                                         //~ 1$ = name of character, 2$ = body part (left arm)
                                         _( "%1$s does not have a %2$s." ), u.disp_name( false, true ),
                                         id->parent_bp->obj().name.translated() );
        for( int y = 0; y < height; y++ ) {
            if( y == height / 2 ) {
                txt.insert( txt.begin(), center_text_pos( txt, 0, width ), ' ' );
                ret.emplace_back( colorize( txt, c_light_red ) );
            } else {
                ret.emplace_back( width, ' ' );
            }
        }
        return ret;
    }

    const bool hflip = !!id->mirror;
    const bodygraph_id &rid = get_bg_rows( id );
    for( int i = 0; static_cast<size_t>( i ) < rid->rows.size() && i < height; i++ ) {
        std::string ret_row;
        int j = hflip ? rid->rows[i].size() - 1 : 0;
        size_t label_i = 0;
        for( int x = 0 ; x < width && j < BPGRAPH_MAXCOLS && j >= 0; hflip ? j-- : j++, x++ ) {
            std::string sym = " ";
            const std::string &r = rid->rows[i][j];
            bool label_sym = false;
            if( !id->fill_rows.empty() && id->label_fill == id->fill_rows[i][j] ) {
                if( label_i < label.length() ) {
                    label_sym = true;
                    sym = label[label_i++];
                }
            } else {
                sym = id->fill_sym.empty() ? r : id->fill_sym;
            }
            auto iter = id->parts.find( r );
            const bodygraph_part *bgp = nullptr;
            if( iter != id->parts.end() ) {
                bgp = &iter->second;
                bool missing_section = true;
                for( const bodypart_id &bp : iter->second.bodyparts ) {
                    if( has_bodypart( u, bp ) ) {
                        missing_section = false;
                    }
                }
                for( const sub_bodypart_id &sp : iter->second.sub_bodyparts ) {
                    if( has_bodypart( u, sp->parent ) ) {
                        missing_section = false;
                    }
                }
                if( !label_sym ) {
                    sym = missing_section ? " " : ( id->fill_rows.empty() ? iter->second.sym : id->fill_rows[i][j] );
                }
            }
            if( rid->rows[i][j] == " " ) {
                sym = " ";
            }
            ret_row.append( fragment_cb( bgp, sym ) );
        }
        ret.emplace_back( ret_row );
    }
    return ret;
}
