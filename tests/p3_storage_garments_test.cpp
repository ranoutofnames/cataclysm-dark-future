// CDF STOR4: mod garments whose pockets came from `storage` carry the same pockets as explicit
// pocket_data. The expected pockets are in tests/data/dda_staged/p3_storage/STOR4.json (re-derived
// from CDF item data, `storage`, and the DDA layouts of jacket_army / pants_army).
// Ids of mods that are not loaded in this run are checked in their raw mod JSON instead.
#include <cstdlib>
#include <fstream>
#include <locale>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "enum_conversions.h"
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

// CDF STOR5: the wolfsuit family (PL-31, AUTO-j). `sparkledogsuit` copies `wolfsuit` and has its own
// battery well, so the parent layout and the child's whole pocket list land together. The expected
// layout is in tests/data/dda_staged/p3_storage/STOR5.json; the well and MOD pockets before the edit
// are recorded in STOR5-before.json by the hidden `stor5snap` case (rewrite it only on base data).
namespace
{

const std::string stor5_batch = "tests/data/dda_staged/p3_storage/STOR5.json";
const std::string stor5_before = "tests/data/dda_staged/p3_storage/STOR5-before.json";

struct sp5_pk {
    std::string type = "CONTAINER";
    int volume_ml = -1;
    int length_mm = -1;
    int weight_g = -1;
    int moves = -1;
    int magazine_well_ml = -1;
    bool carry = false;
    bool holster = false;
    bool rigid = false;
    bool has_restriction = false;
    std::vector<std::string> item_restriction;
    std::string default_magazine;
};

struct sp5_row {
    std::string id;
    std::string cls;
    std::string file;
    std::string parent;
    int storage_ml = 0;
    int get_storage_ml = 0;
    std::vector<sp5_pk> pockets;
};

sp5_pk read_sp5_pk( JsonObject jo )
{
    jo.allow_omitted_members();
    sp5_pk p;
    p.type = jo.get_string( "pocket_type", "CONTAINER" );
    if( jo.has_int( "volume_ml" ) ) {
        p.volume_ml = jo.get_int( "volume_ml" );
    }
    if( jo.has_int( "max_item_length_mm" ) ) {
        p.length_mm = jo.get_int( "max_item_length_mm" );
    }
    if( jo.has_int( "max_contains_weight_g" ) ) {
        p.weight_g = jo.get_int( "max_contains_weight_g" );
    }
    if( jo.has_int( "moves" ) ) {
        p.moves = jo.get_int( "moves" );
    }
    if( jo.has_int( "magazine_well_ml" ) ) {
        p.magazine_well_ml = jo.get_int( "magazine_well_ml" );
    }
    p.carry = jo.get_bool( "carry", false );
    p.holster = jo.get_bool( "holster", false );
    p.rigid = jo.get_bool( "rigid", false );
    if( jo.has_array( "item_restriction" ) ) {
        p.has_restriction = true;
        JsonArray a = jo.get_array( "item_restriction" );
        while( a.has_more() ) {
            p.item_restriction.push_back( a.next_string() );
        }
    }
    p.default_magazine = jo.get_string( "default_magazine", "" );
    return p;
}

std::vector<std::string> restriction_strings( const cata::flat_set<itype_id> &restriction )
{
    std::vector<std::string> ret;
    for( const itype_id &id : restriction ) {
        ret.push_back( id.str() );
    }
    return ret;
}

std::vector<sp5_pk> read_sp5_pockets( JsonArray arr )
{
    std::vector<sp5_pk> ret;
    while( arr.has_more() ) {
        ret.push_back( read_sp5_pk( arr.next_object() ) );
    }
    return ret;
}

const std::vector<sp5_row> &stor5_rows()
{
    static std::vector<sp5_row> rows;
    if( !rows.empty() ) {
        return rows;
    }
    std::ifstream file( stor5_batch, std::ios::binary );
    REQUIRE( file.good() );
    JsonIn jsin( file );
    JsonObject root = jsin.get_object();
    root.allow_omitted_members();
    JsonArray arr = root.get_array( "rows" );
    while( arr.has_more() ) {
        JsonObject jo = arr.next_object();
        jo.allow_omitted_members();
        sp5_row r;
        r.id = jo.get_string( "id" );
        r.cls = jo.get_string( "class_" );
        r.file = jo.get_string( "file" );
        r.storage_ml = jo.get_int( "storage_ml" );
        if( jo.has_string( "copy_from" ) ) {
            r.parent = jo.get_string( "copy_from" );
        }
        JsonObject ex = jo.get_object( "expected" );
        ex.allow_omitted_members();
        r.get_storage_ml = ex.get_int( "get_storage_ml" );
        if( ex.has_array( "pockets" ) ) {
            r.pockets = read_sp5_pockets( ex.get_array( "pockets" ) );
        }
        rows.push_back( r );
    }
    return rows;
}

const sp5_row &stor5_row( const std::string &id )
{
    for( const sp5_row &r : stor5_rows() ) {
        if( r.id == id ) {
            return r;
        }
    }
    FAIL( "STOR5 row missing: " + id );
    return stor5_rows().front();
}

// The pockets recorded on base data (red run), before `sparkledogsuit` got its explicit list.
std::vector<sp5_pk> stor5_before_pockets()
{
    std::ifstream file( stor5_before, std::ios::binary );
    REQUIRE( file.good() );
    JsonIn jsin( file );
    JsonObject root = jsin.get_object();
    root.allow_omitted_members();
    return read_sp5_pockets( root.get_array( "pockets" ) );
}

const pocket_data &container_pocket( const itype &t, size_t index )
{
    REQUIRE( index < t.pockets.size() );
    REQUIRE( t.pockets[index].type == pocket_type::CONTAINER );
    return t.pockets[index];
}

// One CONTAINER pocket equals the layout expected in the batch (S1/S2).
void check_layout_pocket( const pocket_data &p, const sp5_pk &e )
{
    CHECK( p.type == pocket_type::CONTAINER );
    CHECK( units::to_milliliter( p.raw_volume_capacity ) == e.volume_ml );
    CHECK( units::to_gram( p.max_contains_weight ) == e.weight_g );
    CHECK( p.moves == e.moves );
    CHECK( units::to_millimeter( p.max_item_length ) == e.length_mm );
    CHECK( p.carry == e.carry );
    CHECK( p.holster == e.holster );
    CHECK( p.rigid == e.rigid );
}

bool same_set( const std::vector<std::string> &a, const std::vector<std::string> &b )
{
    std::set<std::string> sa( a.begin(), a.end() );
    std::set<std::string> sb( b.begin(), b.end() );
    return sa == sb;
}

} // namespace

TEST_CASE( "p3 storage STOR5: write the before snapshot", "[.][stor5snap]" )
{
    // CDF: run once on base data (red step 0); the file records the synthesized well, MOD and 1 L
    // carry pocket that the explicit list has to reproduce. Do not run it after the data edit.
    clear_all_state();
    const itype &t = itype_id( "sparkledogsuit" ).obj();
    std::ofstream out( stor5_before );
    REQUIRE( out.good() );
    // CDF: the test process runs under the system locale, which would write "2,000,000"
    out.imbue( std::locale::classic() );
    out << "{\n";
    out << " \"format\": 1,\n";
    out << " \"id\": \"sparkledogsuit\",\n";
    out << " \"note\": \"loaded pockets before STOR5: synthesized MAGAZINE_WELL, MOD, one 1 L "
        "CONTAINER (red step 0)\",\n";
    out << " \"pockets\": [\n";
    for( size_t i = 0; i < t.pockets.size(); ++i ) {
        const pocket_data &p = t.pockets[i];
        out << "  {\n";
        out << "   \"pocket_type\": \"" << io::enum_to_string( p.type ) << "\",\n";
        out << "   \"volume_ml\": " << units::to_milliliter( p.raw_volume_capacity ) << ",\n";
        out << "   \"max_item_length_mm\": " << units::to_millimeter( p.max_item_length ) << ",\n";
        out << "   \"max_contains_weight_g\": " << units::to_gram( p.max_contains_weight ) << ",\n";
        out << "   \"moves\": " << p.moves << ",\n";
        out << "   \"magazine_well_ml\": " << units::to_milliliter( p.magazine_well ) << ",\n";
        out << "   \"carry\": " << ( p.carry ? "true" : "false" ) << ",\n";
        out << "   \"holster\": " << ( p.holster ? "true" : "false" ) << ",\n";
        out << "   \"rigid\": " << ( p.rigid ? "true" : "false" ) << ",\n";
        out << "   \"item_restriction\": [";
        bool first = true;
        for( const itype_id &id : p.item_id_restriction ) {
            out << ( first ? "" : ", " ) << "\"" << id.str() << "\"";
            first = false;
        }
        out << "],\n";
        out << "   \"default_magazine\": \"" << p.default_magazine.str() << "\"\n";
        out << "  }" << ( i + 1 < t.pockets.size() ? "," : "" ) << "\n";
    }
    out << " ]\n";
    out << "}\n";
    out.close();
    SUCCEED();
}

TEST_CASE( "p3 storage STOR5: wolfsuit follows the DDA fursuit layout", "[p3][storage]" )
{
    clear_all_state();
    const sp5_row &r = stor5_row( "wolfsuit" );
    const itype &t = itype_id( r.id ).obj();
    CAPTURE( r.id );
    CHECK( t.explicit_pockets );
    // DDA: suits_clothes.json:770-772 (fursuit): two 500 ml CONTAINER pockets, 2 kg, moves 80,
    // no rigid key (non-rigid). Both volumes are CDF `storage` 1 L split in two (500 + 500).
    REQUIRE( r.pockets.size() == 2 );
    REQUIRE( t.pockets.size() == 2 );
    for( size_t i = 0; i < r.pockets.size(); ++i ) {
        CAPTURE( i );
        check_layout_pocket( t.pockets[i], r.pockets[i] );
        CHECK( units::to_milliliter( t.pockets[i].raw_volume_capacity ) == 500 );
    }
    // CDF: two 500 ml pockets sum to the unchanged storage 1 L
    item *it = item::spawn_temporary( itype_id( r.id ) );
    CHECK( units::to_milliliter( it->get_storage() ) == r.get_storage_ml );
    CHECK( units::to_milliliter( it->type->armor->storage ) == r.storage_ml );
}

TEST_CASE( "p3 storage STOR5: sparkledogsuit keeps its well and MOD pocket and the 1 L carry",
           "[p3][storage]" )
{
    clear_all_state();
    const sp5_row &r = stor5_row( "sparkledogsuit" );
    const itype &t = itype_id( r.id ).obj();
    CAPTURE( r.id );
    CHECK( t.explicit_pockets );
    REQUIRE( t.pockets.size() == 4 );
    // index 0: the well is the one synthesized before (volume, rigid, restriction, default)
    const std::vector<sp5_pk> before = stor5_before_pockets();
    REQUIRE( before.size() == 3 );
    const pocket_data &well = t.pockets[0];
    CHECK( well.type == pocket_type::MAGAZINE_WELL );
    CHECK( units::to_milliliter( well.raw_volume_capacity ) == before[0].volume_ml );
    CHECK( well.rigid == before[0].rigid );
    CHECK( units::to_milliliter( well.magazine_well ) == before[0].magazine_well_ml );
    CHECK( same_set( restriction_strings( well.item_id_restriction ), before[0].item_restriction ) );
    CHECK( well.default_magazine.str() == before[0].default_magazine );
    // index 1: the MOD pocket of a tool with ammo/magazines
    CHECK( t.pockets[1].type == pocket_type::MOD );
    // index 2 and 3: the wolfsuit layout
    std::vector<sp5_pk> layout;
    for( const sp5_pk &e : r.pockets ) {
        if( e.type == "CONTAINER" ) {
            layout.push_back( e );
        }
    }
    REQUIRE( layout.size() == 2 );
    for( size_t i = 0; i < layout.size(); ++i ) {
        CAPTURE( i );
        check_layout_pocket( container_pocket( t, i + 2 ), layout[i] );
    }
    // CDF: the two 500 ml pockets replace the single 1 L pocket, so storage stays 1 L
    item *it = item::spawn_temporary( itype_id( r.id ) );
    CHECK( units::to_milliliter( it->get_storage() ) == r.get_storage_ml );
    CHECK( r.get_storage_ml == 1000 );
}

TEST_CASE( "p3 storage STOR5: sparkledogsuit_on inherits the list", "[p3][storage]" )
{
    clear_all_state();
    const sp5_row &r = stor5_row( "sparkledogsuit_on" );
    REQUIRE( r.parent == "sparkledogsuit" );
    const itype &child = itype_id( r.id ).obj();
    const itype &parent = itype_id( r.parent ).obj();
    CAPTURE( r.id, r.parent );
    CHECK( child.explicit_pockets );
    REQUIRE( child.pockets.size() == parent.pockets.size() );
    for( size_t i = 0; i < parent.pockets.size(); ++i ) {
        CAPTURE( i );
        const pocket_data &c = child.pockets[i];
        const pocket_data &p = parent.pockets[i];
        CHECK( c.type == p.type );
        CHECK( c.raw_volume_capacity == p.raw_volume_capacity );
        CHECK( c.max_item_length == p.max_item_length );
        CHECK( c.max_contains_weight == p.max_contains_weight );
        CHECK( c.moves == p.moves );
        CHECK( c.carry == p.carry );
        CHECK( c.holster == p.holster );
        CHECK( c.rigid == p.rigid );
        CHECK( c.magazine_well == p.magazine_well );
        CHECK( c.item_id_restriction == p.item_id_restriction );
        CHECK( c.default_magazine == p.default_magazine );
    }
    item *it = item::spawn_temporary( itype_id( r.id ) );
    CHECK( units::to_milliliter( it->get_storage() ) == r.get_storage_ml );
}

TEST_CASE( "p3 storage STOR5: worn capacity rises by the storage", "[p3][storage]" )
{
    clear_all_state();
    for( const char *id : { "wolfsuit", "sparkledogsuit", "sparkledogsuit_on" } ) {
        CAPTURE( id );
        const sp5_row &r = stor5_row( id );
        avatar &u = get_avatar();
        clear_character( u, false );
        u.unset_mutation( trait_id( "DEBUG_STORAGE" ) );
        const units::volume cap0 = u.carried_capacity();
        detached_ptr<item> it = item::spawn( itype_id( r.id ), calendar::turn );
        // CDF: wear_item returns the item back on failure and an empty pointer on success
        REQUIRE( !u.wear_item( std::move( it ), false ) );
        CHECK( u.carried_capacity() == cap0 + units::from_milliliter( r.get_storage_ml ) );
        CHECK( r.get_storage_ml == 1000 );
    }
}

TEST_CASE( "p3 storage STOR5: the two golden entries equal the loaded pockets", "[p3][storage]" )
{
    clear_all_state();
    std::ifstream file( "tests/data/dda_staged/p3_golden.json", std::ios::binary );
    REQUIRE( file.good() );
    JsonIn jsin( file );
    JsonObject golden = jsin.get_object();
    golden.allow_omitted_members();
    JsonObject items = golden.get_object( "items" );
    for( const char *id : { "sparkledogsuit", "sparkledogsuit_on" } ) {
        CAPTURE( id );
        REQUIRE( items.has_object( id ) );
        JsonObject entry = items.get_object( id );
        entry.allow_omitted_members();
        JsonArray gp = entry.get_array( "pockets" );
        // the golden omits MOD pockets (README convention), so compare the rest in order
        std::vector<const pocket_data *> loaded;
        const itype &t = itype_id( id ).obj();
        for( const pocket_data &p : t.pockets ) {
            if( p.type != pocket_type::MOD ) {
                loaded.push_back( &p );
            }
        }
        REQUIRE( loaded.size() == gp.size() );
        size_t i = 0;
        int container_sum = 0;
        while( gp.has_more() ) {
            JsonObject g = gp.next_object();
            g.allow_omitted_members();
            const pocket_data &p = *loaded[i];
            CAPTURE( i );
            if( g.has_member( "pocket_type" ) ) {
                CHECK( io::enum_to_string( p.type ) == g.get_string( "pocket_type" ) );
            }
            if( g.has_member( "item_restriction" ) ) {
                std::vector<std::string> want;
                JsonArray a = g.get_array( "item_restriction" );
                while( a.has_more() ) {
                    want.push_back( a.next_string() );
                }
                CHECK( same_set( restriction_strings( p.item_id_restriction ), want ) );
            }
            if( g.has_member( "default_magazine" ) ) {
                CHECK( p.default_magazine.str() == g.get_string( "default_magazine" ) );
            }
            if( g.has_member( "magazine_well" ) ) {
                CHECK( units::to_milliliter( p.magazine_well ) ==
                       volume_ml_of( g.get_string( "magazine_well" ) ) );
            }
            if( g.has_member( "max_contains_volume" ) ) {
                CHECK( units::to_milliliter( p.raw_volume_capacity ) ==
                       volume_ml_of( g.get_string( "max_contains_volume" ) ) );
            }
            if( g.has_member( "carry" ) ) {
                CHECK( p.carry == g.get_bool( "carry" ) );
            }
            if( g.has_member( "holster" ) ) {
                CHECK( p.holster == g.get_bool( "holster" ) );
            }
            if( g.has_member( "rigid" ) ) {
                CHECK( p.rigid == g.get_bool( "rigid" ) );
            }
            if( g.has_member( "moves" ) ) {
                CHECK( p.moves == g.get_int( "moves" ) );
            }
            if( p.type == pocket_type::CONTAINER ) {
                container_sum += units::to_milliliter( p.raw_volume_capacity );
            }
            ++i;
        }
        CHECK( container_sum == 1000 );
    }
}

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
