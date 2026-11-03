#include "subbodypart.h"

#include <algorithm>

#include "debug.h"
#include "enum_conversions.h"
#include "generic_factory.h"
#include "json.h"
#include "type_id_implement.h"

namespace
{

generic_factory<sub_body_part_type> sub_body_part_factory( "sub body part" );

} // namespace

IMPLEMENT_STRING_AND_INT_IDS( sub_body_part_type, sub_body_part_factory );

void sub_body_part_type::load_bp( const JsonObject &jo, const std::string &src )
{
    sub_body_part_factory.load( jo, src );
}

void sub_body_part_type::load( const JsonObject &jo, const std::string & )
{
    mandatory( jo, was_loaded, "id", id );
    mandatory( jo, was_loaded, "name", name );
    mandatory( jo, was_loaded, "parent", parent );
    optional( jo, was_loaded, "name_multiple", name_multiple );
    optional( jo, was_loaded, "secondary", secondary, false );
    optional( jo, was_loaded, "locations_under", locations_under );
    optional( jo, was_loaded, "max_coverage", max_coverage, 0 );

    const auto side_reader = enum_flags_reader<side> { "side" };
    optional( jo, was_loaded, "side", part_side, side_reader, side::BOTH );

    // as in DDA: no blind copy-from of the opposite, which would be inconsistent
    if( jo.has_string( "opposite" ) ) {
        mandatory( jo, was_loaded, "opposite", opposite );
    } else {
        opposite = id;
    }
}

void sub_body_part_type::reset()
{
    sub_body_part_factory.reset();
}

void sub_body_part_type::finalize_all()
{
    sub_body_part_factory.finalize();
}

void sub_body_part_type::finalize()
{
}

const std::vector<sub_body_part_type> &sub_body_part_type::get_all()
{
    return sub_body_part_factory.get_all();
}

void sub_body_part_type::check_consistency()
{
    sub_body_part_factory.check();
}

void sub_body_part_type::check() const
{
    if( id.is_null() ) {
        return;
    }
    if( max_coverage < 0 || max_coverage > 100 ) {
        debugmsg( "Sub body part %s has max_coverage %d outside 0-100", id.c_str(), max_coverage );
    }
    if( !parent.is_valid() ) {
        debugmsg( "Sub body part %s has invalid parent %s", id.c_str(), parent.c_str() );
    } else {
        const std::vector<sub_bodypart_str_id> &listed = parent->sub_parts;
        if( std::find( listed.begin(), listed.end(), id ) == listed.end() ) {
            debugmsg( "Sub body part %s is not listed in sub_parts of its parent %s", id.c_str(),
                      parent.c_str() );
        }
    }
    for( const sub_bodypart_str_id &under : locations_under ) {
        if( !under.is_valid() ) {
            debugmsg( "Sub body part %s has invalid locations_under entry %s", id.c_str(), under.c_str() );
        } else if( under->secondary ) {
            debugmsg( "Sub body part %s: locations_under entry %s is not a primary location", id.c_str(),
                      under.c_str() );
        }
    }
    if( !opposite.is_valid() ) {
        debugmsg( "Sub body part %s has invalid opposite %s", id.c_str(), opposite.c_str() );
    }
}
