#include "catalua_bindings_game_internal.h"
#include "catalua_bindings_utils.h"
#include "catalua_coord.h"
#include "catalua_luna_doc.h"
#include "game.h"
#include "iexamine.h"
#include "line.h"
#include "magic/magic_ter_furn_transform.h"
#include "map/map.h"
#include "map/mapbuffer_registry.h"
#include "mapgen/mapgen_constructor.h"
#include "mapgen/mapgen_functions.h"
#include "overmap/overmap.h"
#include "overmap/overmapbuffer.h"
#include "player.h"

#include <stdexcept>
#include <vector>

namespace
{

auto direction_from_relative_delta( const cata::detail::lua_coords::lua_tripoint_coord &delta ) ->
direction
{
    if( delta.origin != coords::origin::relative ) {
        throw std::runtime_error( "direction_from expects a relative TripointCoord delta" );
    }
    return direction_from( delta.raw );
}

auto overmap_terrain_cardinal_directions() -> std::vector<tripoint_rel_omt>
{
    return std::vector<tripoint_rel_omt> {
        tripoint_rel_omt::north(), tripoint_rel_omt::south(), tripoint_rel_omt::east(),
        tripoint_rel_omt::west(), tripoint_rel_omt::above(), tripoint_rel_omt::below()
    };
}

} // namespace

namespace cata::detail
{

auto reg_game_api_world_helpers( luna::userlib &lib ) -> void
{
    DOC( "Get the global overmap buffer" );
    luna::set_fx( lib, "get_overmap_buffer", []() -> overmapbuffer & { return get_active_overmapbuffer(); } );
    DOC( "Run a built-in examine action at a position." );
    luna::set_fx( lib, "call_builtin_examine",
    []( const std::string & examine_id, player & who, const tripoint_bub_ms & pos ) -> void {
        iexamine_function_from_string( examine_id )( who, pos );
    } );

    DOC( "Get direction from a relative tripoint coordinate delta." );
    luna::set_fx( lib, "direction_from", &direction_from_relative_delta );

    DOC( "Get direction name from direction enum" );
    luna::set_fx( lib, "direction_name", []( direction dir ) -> std::string { return direction_name( dir ); } );

    DOC( "Applies a JSON ter_furn_transform (by id string) to every tile in a disc (trig_dist <= radius) around an absolute map-square position, on the center's z-level. "
         "Works inside and outside the reality bubble; outside it, each overlapped overmap terrain is loaded (and generated if it does not exist yet). "
         "Returns the number of tiles visited; 0 for an unknown id. Main thread only. "
         "Cost grows with the square of the radius when the area is outside the bubble (every overlapped overmap terrain is loaded or generated), so keep radii small." );
    luna::set_fx( lib, "transform_radius_abs",
    []( const std::string & id, const int radius, const tripoint_abs_ms & center ) -> int {
        const auto tid = ter_furn_transform_id( id );
        if( !tid.is_valid() || radius < 0 )
        {
            return 0;
        }
        auto &here = get_map();
        const auto lo = abs_to_bub( center + tripoint_rel_ms( -radius, -radius, 0 ) );
        const auto hi = abs_to_bub( center + tripoint_rel_ms( radius, radius, 0 ) );
        if( here.inbounds( lo ) && here.inbounds( hi ) )
        {
            return here.transform_radius( tid, radius, abs_to_bub( center ) );
        }
        constexpr auto omt_size = SEEX * 2;
        const auto min_omt = project_to<coords::omt>( center + tripoint_rel_ms( -radius, -radius, 0 ) );
        const auto max_omt = project_to<coords::omt>( center + tripoint_rel_ms( radius, radius, 0 ) );
        auto count = 0;
        auto ctor = mapgen_constructor( MAPBUFFER_REGISTRY.get( g_active_dimension_id ) );
        for( auto ox = min_omt.x(); ox <= max_omt.x(); ++ox )
        {
            for( auto oy = min_omt.y(); oy <= max_omt.y(); ++oy )
            {
                const auto omt = tripoint_abs_omt( ox, oy, center.z() );
                if( !ctor.load( omt ) )
                {
                    continue;
                }
                const auto origin = project_to<coords::ms>( omt );
                for( auto lx = 0; lx < omt_size; ++lx )
                {
                    for( auto ly = 0; ly < omt_size; ++ly )
                    {
                        const auto abs = tripoint_abs_ms( origin.x() + lx, origin.y() + ly, center.z() );
                        if( trig_dist( center, abs ) <= static_cast<float>( radius ) )
                        {
                            tid->transform( ctor, point_omt_ms( lx, ly ) );
                            ++count;
                        }
                    }
                }
                here.mark_omt_caches_dirty( omt );
            }
        }
        return count;
    } );

    DOC( "Returns whether a JSON update_mapgen with this id exists." );
    luna::set_fx( lib, "has_update_mapgen", []( const std::string & id ) -> bool {
        return has_update_mapgen_func( id );
    } );
    DOC( "Runs a JSON update_mapgen (by id string) at an absolute overmap-terrain location. "
         "Optional opts table: `cancel_on_collision` (boolean, default true). "
         "Returns false (and changes nothing) for an unknown id or invalid location. "
         "Loads/generates the overmap terrain if needed and refreshes the map caches. Main thread only." );
    DOC_PARAMS( "id: string", "omt: TripointAbsOmt", "opts?: table" );
    luna::set_fx( lib, "run_mapgen_update",
    []( const std::string & id, const tripoint_abs_omt & omt, sol::optional<sol::table> opts ) -> bool {
        if( omt == overmap::invalid_tripoint || !has_update_mapgen_func( id ) )
        {
            return false;
        }
        auto cancel = true;
        if( opts )
        {
            cancel = opts->get_or( "cancel_on_collision", true );
        }
        const auto ok = run_mapgen_update_func( id, omt, nullptr, cancel );
        if( ok )
        {
            get_map().mark_omt_caches_dirty( omt );
        }
        return ok;
    } );

    DOC( "Get the six cardinal overmap-terrain direction offsets (N, S, E, W, Up, Down)." );
    luna::set_fx( lib, "six_cardinal_directions", &overmap_terrain_cardinal_directions );
}

} // namespace cata::detail
