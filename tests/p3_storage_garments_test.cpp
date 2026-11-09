// CDF STOR4: mod garments whose pockets came from `storage` carry the same pockets as explicit
// pocket_data. The expected pockets are in tests/data/dda_staged/p3_storage/STOR4.json (re-derived
// from CDF item data, `storage`, and the DDA layouts of jacket_army / pants_army).
// Ids of mods that are not loaded in this run are checked in their raw mod JSON instead.
#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "item.h"
#include "item_factory.h"
#include "itype.h"
#include "json.h"
#include "player_helpers.h"
#include "pocket_data.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"

namespace
{

const std::string stor4_batch = "tests/data/dda_staged/p3_storage/STOR4.json";

struct pk_expect {
    int volume_ml = 0;
    int length_mm = -1;
    int weight_g = -1;
    int moves = -1;
    bool carry = true;
    bool holster = false;
    bool rigid = true;
};

struct row_expect {
    std::string id;
    std::string cls;
    std::string file;
    std::string parent;
    int storage_ml = 0;
    int get_storage_ml = 0;
    std::vector<pk_expect> pockets;
};

pk_expect read_pk( JsonObject jo )
{
    jo.allow_omitted_members();
    pk_expect p;
    p.volume_ml = jo.get_int( "volume_ml" );
    p.length_mm = jo.get_int( "max_item_length_mm", -1 );
    p.weight_g = jo.get_int( "max_contains_weight_g", -1 );
    p.moves = jo.get_int( "moves", -1 );
    p.carry = jo.get_bool( "carry" );
    p.holster = jo.get_bool( "holster" );
    p.rigid = jo.get_bool( "rigid" );
    return p;
}

const std::vector<row_expect> &stor4_rows()
{
    static std::vector<row_expect> rows;
    if( !rows.empty() ) {
        return rows;
    }
    std::ifstream file( stor4_batch, std::ios::binary );
    REQUIRE( file.good() );
    JsonIn jsin( file );
    JsonObject root = jsin.get_object();
    root.allow_omitted_members();
    JsonArray arr = root.get_array( "rows" );
    while( arr.has_more() ) {
        JsonObject jo = arr.next_object();
        jo.allow_omitted_members();
        row_expect r;
        r.id = jo.get_string( "id" );
        r.cls = jo.get_string( "class_" );
        r.file = jo.get_string( "file" );
        r.storage_ml = jo.get_int( "storage_ml" );
        if( jo.has_string( "copy_from" ) ) {
            r.parent = jo.get_string( "copy_from" );
        }
        if( jo.has_object( "expected" ) ) {
            JsonObject ex = jo.get_object( "expected" );
            ex.allow_omitted_members();
            r.get_storage_ml = ex.get_int( "get_storage_ml" );
            if( ex.has_object( "main" ) ) {
                r.pockets.push_back( read_pk( ex.get_object( "main" ) ) );
                if( ex.has_object( "strap" ) ) {
                    r.pockets.push_back( read_pk( ex.get_object( "strap" ) ) );
                }
            } else {
                JsonArray pa = ex.get_array( "pockets" );
                while( pa.has_more() ) {
                    r.pockets.push_back( read_pk( pa.next_object() ) );
                }
            }
        }
        rows.push_back( r );
    }
    return rows;
}

bool loaded( const row_expect &r )
{
    return itype_id( r.id ).is_valid();
}

int volume_ml_of( const std::string &s )
{
    // "100000 ml" form only (the staged data and the mod JSON both use ml for these pockets)
    REQUIRE( s.size() > 3 );
    REQUIRE( s.substr( s.size() - 3 ) == " ml" );
    return std::atoi( s.c_str() );
}

void check_loaded_pockets( const row_expect &r )
{
    const itype &t = *item::spawn_temporary( itype_id( r.id ) )->type;
    CAPTURE( r.id );
    CHECK( t.explicit_pockets );
    REQUIRE( t.pockets.size() == r.pockets.size() );
    for( size_t i = 0; i < r.pockets.size(); ++i ) {
        CAPTURE( i );
        const pocket_data &p = t.pockets[i];
        const pk_expect &e = r.pockets[i];
        CHECK( p.type == pocket_type::CONTAINER );
        CHECK( units::to_milliliter( p.raw_volume_capacity ) == e.volume_ml );
        CHECK( p.carry == e.carry );
        CHECK( p.holster == e.holster );
        CHECK( p.rigid == e.rigid );
        if( e.length_mm >= 0 ) {
            CHECK( units::to_millimeter( p.max_item_length ) == e.length_mm );
        }
        if( e.moves >= 0 ) {
            CHECK( p.moves == e.moves );
        }
        if( e.weight_g >= 0 ) {
            CHECK( units::to_gram( p.max_contains_weight ) == e.weight_g );
        }
    }
}

// Mod not loaded in this run: read the object out of the mod file and check the written pockets.
void check_raw_pockets( const row_expect &r )
{
    CAPTURE( r.id, r.file );
    std::ifstream file( "data/" + r.file, std::ios::binary );
    REQUIRE( file.good() );
    JsonIn jsin( file );
    JsonArray all = jsin.get_array();
    bool found = false;
    while( all.has_more() ) {
        JsonObject jo = all.next_object();
        jo.allow_omitted_members();
        if( !jo.has_string( "id" ) || jo.get_string( "id" ) != r.id ) {
            continue;
        }
        found = true;
        REQUIRE( jo.has_array( "pocket_data" ) );
        JsonArray pa = jo.get_array( "pocket_data" );
        size_t i = 0;
        while( pa.has_more() ) {
            JsonObject po = pa.next_object();
            po.allow_omitted_members();
            REQUIRE( i < r.pockets.size() );
            const pk_expect &e = r.pockets[i];
            CAPTURE( i );
            CHECK( po.get_string( "pocket_type" ) == "CONTAINER" );
            CHECK( volume_ml_of( po.get_string( "max_contains_volume" ) ) == e.volume_ml );
            CHECK( po.get_bool( "holster", false ) == e.holster );
            CHECK( po.get_bool( "rigid", false ) == e.rigid );
            CHECK( po.has_member( "carry" ) == false );
            ++i;
        }
        CHECK( i == r.pockets.size() );
    }
    CHECK( found );
}

void check_row( const row_expect &r )
{
    if( loaded( r ) ) {
        check_loaded_pockets( r );
    } else {
        WARN( "mod of " + r.id + " not loaded in this run; raw mod JSON checked" );
        check_raw_pockets( r );
    }
}

std::vector<const row_expect *> rows_of( const std::string &cls )
{
    std::vector<const row_expect *> ret;
    for( const row_expect &r : stor4_rows() ) {
        if( r.cls == cls ) {
            ret.push_back( &r );
        }
    }
    return ret;
}

} // namespace

TEST_CASE( "p3 storage STOR4: explicit pockets equal the pockets synthesized before", "[p3][storage]" )
{
    clear_all_state();
    const std::vector<const row_expect *> rows = rows_of( "NEUTRAL" );
    // CDF: STOR4 rows table: 15 NEUTRAL ids
    REQUIRE( rows.size() == 15 );
    for( const row_expect *r : rows ) {
        check_row( *r );
    }
}

TEST_CASE( "p3 storage STOR4: get_storage equals storage", "[p3][storage]" )
{
    clear_all_state();
    int checked = 0;
    for( const row_expect &r : stor4_rows() ) {
        if( r.cls == "NEUTRAL-INHERIT" || !loaded( r ) ) {
            continue;
        }
        CAPTURE( r.id );
        const item *it = item::spawn_temporary( itype_id( r.id ) );
        CHECK( units::to_milliliter( it->get_storage() ) == r.get_storage_ml );
        REQUIRE( it->type->armor );
        CHECK( units::to_milliliter( it->type->armor->storage ) == r.storage_ml );
        // the 10 percent load check: non-holster CONTAINER pockets within 10 percent of storage
        int sum = 0;
        for( const pocket_data &p : it->type->pockets ) {
            if( p.type == pocket_type::CONTAINER && !p.holster ) {
                sum += units::to_milliliter( p.raw_volume_capacity );
            }
        }
        CHECK( std::abs( sum - r.storage_ml ) * 10 <= r.storage_ml );
        checked++;
    }
    CHECK( checked > 0 );
}

TEST_CASE( "p3 storage STOR4: worn capacity rises by the storage", "[p3][storage]" )
{
    int checked = 0;
    for( const row_expect &r : stor4_rows() ) {
        if( r.cls == "NEUTRAL-INHERIT" || !loaded( r ) ) {
            continue;
        }
        CAPTURE( r.id );
        clear_all_state();
        avatar &u = get_avatar();
        clear_character( u, false );
        u.unset_mutation( trait_id( "DEBUG_STORAGE" ) );
        const units::volume cap0 = u.carried_capacity();
        detached_ptr<item> it = item::spawn( itype_id( r.id ), calendar::turn );
        // CDF: wear_item returns the item back on failure and an empty pointer on success
        REQUIRE( !u.wear_item( std::move( it ), false ) );
        // the holster strap adds nothing (CAP1c); the main pocket / layout pockets add the storage
        CHECK( u.carried_capacity() == cap0 + units::from_milliliter( r.get_storage_ml ) );
        checked++;
    }
    CHECK( checked > 0 );
}

TEST_CASE( "p3 storage STOR4: children inherit", "[p3][storage]" )
{
    clear_all_state();
    const std::vector<const row_expect *> rows = rows_of( "NEUTRAL-INHERIT" );
    // CDF: STOR4 rows table: test_pants_faux_fur and test_portion_faux_fur_pants_suit
    REQUIRE( rows.size() == 2 );
    for( const row_expect *r : rows ) {
        CAPTURE( r->id, r->parent );
        REQUIRE( loaded( *r ) );
        const itype &child = *item::spawn_temporary( itype_id( r->id ) )->type;
        const itype &parent = *item::spawn_temporary( itype_id( r->parent ) )->type;
        CHECK( child.explicit_pockets );
        REQUIRE( parent.pockets.size() == 1 );
        REQUIRE( child.pockets.size() == parent.pockets.size() );
        for( size_t i = 0; i < parent.pockets.size(); ++i ) {
            CHECK( child.pockets[i].raw_volume_capacity == parent.pockets[i].raw_volume_capacity );
            CHECK( child.pockets[i].max_item_length == parent.pockets[i].max_item_length );
            CHECK( child.pockets[i].carry == parent.pockets[i].carry );
            CHECK( child.pockets[i].holster == parent.pockets[i].holster );
            CHECK( child.pockets[i].rigid == parent.pockets[i].rigid );
        }
        // CDF: test_pants_fur storage is 1 L
        CHECK( units::to_milliliter( child.pockets[0].raw_volume_capacity ) == 1000 );
    }
}

TEST_CASE( "p3 storage STOR4: CRT children follow the parent layout shape", "[p3][storage]" )
{
    clear_all_state();
    const std::vector<const row_expect *> rows = rows_of( "PARENT-LAYOUT-SHAPE" );
    REQUIRE( rows.size() == 2 );
    for( const row_expect *r : rows ) {
        int sum = 0;
        for( const pk_expect &e : r->pockets ) {
            sum += e.volume_ml;
        }
        // DDA: jacket_army 590/590/361/361/131 ml, pants_army 1250 x 4, 1080 x 2, scaled to storage
        CHECK( sum == r->storage_ml );
        CHECK( r->pockets.size() == ( r->id == "crt_jacket" ? 5u : 6u ) );
        check_row( *r );
    }
}
