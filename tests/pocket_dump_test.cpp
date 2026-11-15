// PL-64 DT1: dump the final (post-synthesis) pockets of every loaded item type.
//
// The test is hidden from the normal suite ([.]); run it explicitly:
//   cata_test --tags "[.pocket_dump]"
// It writes report/pocket-dump.tsv, which tools/pocket_diff.py compares with DDA's raw JSON
// (pin b4b62254ae2).  CDF synthesizes pockets for legacy BN armor and containers in C++, so this
// dump is the only way to see what the game actually loaded.

#include "catch/catch.hpp"
#include "item_factory.h"
#include "item_pocket.h"
#include "itype.h"
#include "pocket_data.h"
#include "pocket_type.h"
#include "units.h"
#include "units_volume.h"

#include "audit_support.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace
{

const char *const pocket_dump_path = "report/pocket-dump.tsv";

auto to_ml( units::volume volume ) -> std::string
{
    return std::to_string( units::to_milliliter( volume ) );
}

auto bool_text( bool value ) -> std::string
{
    return value ? "1" : "0";
}

auto restricted_flags( const pocket_data &p ) -> std::string
{
    std::vector<std::string> flags;
    for( const flag_id &flag : p.get_flag_restrictions() ) {
        flags.push_back( flag.str() );
    }
    std::sort( flags.begin(), flags.end() );
    return audit::join( flags, ";" );
}

auto restricted_items( const pocket_data &p ) -> std::string
{
    std::vector<std::string> ids;
    for( const itype_id &id : p.item_id_restriction ) {
        ids.push_back( id.str() );
    }
    std::sort( ids.begin(), ids.end() );
    return audit::join( ids, ";" );
}

auto restricted_ammo( const pocket_data &p ) -> std::string
{
    std::vector<std::string> entries;
    for( const auto &entry : p.ammo_restriction ) {
        entries.push_back( entry.first.str() + ":" + std::to_string( entry.second ) );
    }
    std::sort( entries.begin(), entries.end() );
    return audit::join( entries, ";" );
}

} // namespace

TEST_CASE( "pocket dump", "[.pocket_dump]" )
{
    const std::vector<std::string> header = {
        "item", "pocket_index", "pocket_type", "flag_restriction", "item_restriction",
        "volume_ml", "max_item_volume_ml", "holster", "ripoff", "magazine_well_ml",
        "ammo_restriction", "rigid", "watertight"
    };

    std::vector<std::vector<std::string>> rows;
    for( const itype *t : item_controller->all() ) {
        if( t == nullptr ) {
            continue;
        }
        const std::string item_id = t->get_id().str();
        for( std::size_t i = 0; i < t->pockets.size(); ++i ) {
            const pocket_data &p = t->pockets[i];
            rows.push_back( {
                item_id,
                std::to_string( i ),
                io::enum_to_string( p.type ),
                restricted_flags( p ),
                restricted_items( p ),
                to_ml( p.raw_volume_capacity ),
                p.max_item_volume ? to_ml( *p.max_item_volume ) : "-",
                bool_text( p.holster ),
                std::to_string( p.ripoff ),
                to_ml( p.magazine_well ),
                restricted_ammo( p ),
                bool_text( p.rigid ),
                bool_text( p.watertight ),
            } );
        }
    }

    audit::write_tsv( pocket_dump_path, header, rows );
    CHECK_FALSE( rows.empty() );
}
