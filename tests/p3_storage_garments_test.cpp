// CDF STOR4: mod garments whose pockets came from `storage` carry the same pockets as explicit
// pocket_data. The expected pockets are in tests/data/dda_staged/p3_storage/STOR4.json (re-derived
// from CDF item data, `storage`, and the DDA layouts of jacket_army / pants_army).
// Ids of mods that are not loaded in this run are checked in their raw mod JSON instead.
#include <cstdlib>
#include <fstream>
#include <locale>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "character.h"
#include "enum_conversions.h"
#include "generic_factory.h"
#include "generic_readers.h"
#include "item.h"
#include "item_factory.h"
#include "itype.h"
#include "json.h"
#include "player_helpers.h"
#include "pocket_data.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"
#include "itype_slots/islot_tool.h"
#include "itype_slots/islot_armor.h"
#include "itype_slots/islot_magazine.h"

namespace
{

const std::string stor4_batch = "tests/data/dda_staged/p3_storage/STOR4.json";
const std::string stor2_batch = "tests/data/dda_staged/p3_storage/STOR2.json";
const std::string stor7_batch = "tests/data/dda_staged/p3_storage/STOR7.json";
const std::string stor2_before = "tests/data/dda_staged/p3_storage/STOR2-before.json";
const std::string stor7_before = "tests/data/dda_staged/p3_storage/STOR7-before.json";

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

std::vector<row_expect> load_rows( const std::string &path )
{
    std::vector<row_expect> rows;
    std::ifstream file( path, std::ios::binary );
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

const std::vector<row_expect> &stor4_rows()
{
    static const std::vector<row_expect> rows = load_rows( stor4_batch );
    return rows;
}

const std::vector<row_expect> &stor2_rows()
{
    static const std::vector<row_expect> rows = load_rows( stor2_batch );
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
    // CDF (MOD1): a TOOL_ARMOR also gets the toolmod pocket after load; the storage rows do not
    // list it, so compare the non-MOD pockets (the toolmod pocket has no volume)
    std::vector<const pocket_data *> loaded;
    for( const pocket_data &p : t.pockets ) {
        if( p.type != pocket_type::MOD ) {
            loaded.push_back( &p );
        }
    }
    REQUIRE( loaded.size() == r.pockets.size() );
    for( size_t i = 0; i < r.pockets.size(); ++i ) {
        CAPTURE( i );
        const pocket_data &p = *loaded[i];
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

std::vector<const row_expect *> rows_of( const std::vector<row_expect> &rows,
        const std::string &cls )
{
    std::vector<const row_expect *> ret;
    for( const row_expect &r : rows ) {
        if( r.cls == cls ) {
            ret.push_back( &r );
        }
    }
    return ret;
}

std::vector<const row_expect *> rows_of( const std::string &cls )
{
    return rows_of( stor4_rows(), cls );
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
    items.allow_omitted_members();
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

// ---------------------------------------------------------------------------------------------
// STOR2 (garments of suits_protection.json / power_armor.json) and STOR7 (exosuit storage)
// ---------------------------------------------------------------------------------------------
namespace
{

void check_layout_row( const row_expect &r )
{
    const itype &t = *item::spawn_temporary( itype_id( r.id ) )->type;
    CAPTURE( r.id );
    CHECK( t.explicit_pockets );
    std::vector<const pocket_data *> loaded;
    for( const pocket_data &p : t.pockets ) {
        if( p.type != pocket_type::MOD ) {
            loaded.push_back( &p );
        }
    }
    REQUIRE( loaded.size() == r.pockets.size() + 1 );
    for( size_t i = 0; i < r.pockets.size(); ++i ) {
        CAPTURE( i );
        const pocket_data &p = *loaded[i];
        const pk_expect &e = r.pockets[i];
        CHECK( p.type == pocket_type::CONTAINER );
        CHECK( units::to_milliliter( p.raw_volume_capacity ) == e.volume_ml );
        CHECK( p.carry );
        CHECK( !p.holster );
        // DDA layout pockets load non-rigid (no "rigid" key); only the strap is written rigid
        CHECK( !p.rigid );
        CHECK( units::to_millimeter( p.max_item_length ) == e.length_mm );
        CHECK( p.moves == e.moves );
        CHECK( units::to_gram( p.max_contains_weight ) == e.weight_g );
    }
    const pocket_data &strap = *loaded.back();
    CHECK( strap.type == pocket_type::CONTAINER );
    CHECK( units::to_milliliter( strap.raw_volume_capacity ) == 10000 );
    CHECK( units::to_millimeter( strap.max_item_length ) == 1500 );
    CHECK( strap.holster );
    CHECK( !strap.carry );
    CHECK( strap.rigid );
}

struct s7_pocket {
    std::string type;
    int volume_ml = -1;
    int max_item_volume_ml = -1;
    int length_mm = -1;
    int weight_g = -1;
    int carry = -1;
    int holster = -1;
    int rigid = -1;
    int watertight = -1;
    std::string default_magazine;
    std::vector<std::string> flag_restriction;
    std::map<std::string, int> ammo_restriction;
};

struct s7_row {
    std::string id;
    std::string inherits_from;
    int storage_ml = 0;
    int get_storage_ml = 0;
    std::vector<s7_pocket> pockets;
};

// The CDF batch files carry plain integers (a derived view), not unit strings.
s7_pocket read_s7_pocket( JsonObject jo )
{
    jo.allow_omitted_members();
    s7_pocket p;
    p.type = jo.get_string( "pocket_type" );
    p.volume_ml = jo.get_int( "volume_ml", -1 );
    p.max_item_volume_ml = jo.get_int( "max_item_volume_ml", -1 );
    p.length_mm = jo.get_int( "max_item_length_mm", -1 );
    p.weight_g = jo.get_int( "max_contains_weight_g", -1 );
    if( jo.has_member( "carry" ) ) {
        p.carry = jo.get_bool( "carry" ) ? 1 : 0;
    }
    if( jo.has_member( "holster" ) ) {
        p.holster = jo.get_bool( "holster" ) ? 1 : 0;
    }
    if( jo.has_member( "rigid" ) ) {
        p.rigid = jo.get_bool( "rigid" ) ? 1 : 0;
    }
    if( jo.has_member( "watertight" ) ) {
        p.watertight = jo.get_bool( "watertight" ) ? 1 : 0;
    }
    if( jo.has_string( "default_magazine" ) ) {
        p.default_magazine = jo.get_string( "default_magazine" );
    }
    if( jo.has_array( "flag_restriction" ) ) {
        JsonArray a = jo.get_array( "flag_restriction" );
        while( a.has_more() ) {
            p.flag_restriction.push_back( a.next_string() );
        }
    }
    if( jo.has_object( "ammo_restriction" ) ) {
        JsonObject o = jo.get_object( "ammo_restriction" );
        o.allow_omitted_members();
        for( const JsonMember m : o ) {
            p.ammo_restriction[m.name()] = m.get_int();
        }
    }
    return p;
}

// A pocket_data written in an item JSON (unit strings).
s7_pocket read_written_pocket( JsonObject jo )
{
    jo.allow_omitted_members();
    s7_pocket p;
    p.type = jo.get_string( "pocket_type" );
    if( jo.has_member( "max_contains_volume" ) ) {
        units::volume v = 0_ml;
        optional( jo, false, "max_contains_volume", v, volume_reader(), 0_ml );
        CHECK( v > 0_ml );
        p.volume_ml = static_cast<int>( units::to_milliliter( v ) );
    }
    if( jo.has_member( "max_contains_weight" ) ) {
        units::mass m = 0_gram;
        optional( jo, false, "max_contains_weight", m, mass_reader(), 0_gram );
        CHECK( m > 0_gram );
        p.weight_g = static_cast<int>( units::to_gram( m ) );
    }
    if( jo.has_member( "max_item_length" ) ) {
        units::length l = 0_mm;
        optional( jo, false, "max_item_length", l );
        CHECK( l > 0_mm );
        p.length_mm = static_cast<int>( units::to_millimeter( l ) );
    }
    if( jo.has_member( "max_item_volume" ) ) {
        units::volume v = 0_ml;
        optional( jo, false, "max_item_volume", v, volume_reader(), 0_ml );
        CHECK( v > 0_ml );
        p.max_item_volume_ml = static_cast<int>( units::to_milliliter( v ) );
    }
    if( jo.has_member( "carry" ) ) {
        p.carry = jo.get_bool( "carry" ) ? 1 : 0;
    }
    if( jo.has_member( "holster" ) ) {
        p.holster = jo.get_bool( "holster" ) ? 1 : 0;
    }
    if( jo.has_member( "rigid" ) ) {
        p.rigid = jo.get_bool( "rigid" ) ? 1 : 0;
    }
    if( jo.has_member( "watertight" ) ) {
        p.watertight = jo.get_bool( "watertight" ) ? 1 : 0;
    }
    if( jo.has_string( "default_magazine" ) ) {
        p.default_magazine = jo.get_string( "default_magazine" );
    }
    if( jo.has_array( "flag_restriction" ) ) {
        JsonArray a = jo.get_array( "flag_restriction" );
        while( a.has_more() ) {
            p.flag_restriction.push_back( a.next_string() );
        }
    }
    if( jo.has_object( "ammo_restriction" ) ) {
        JsonObject o = jo.get_object( "ammo_restriction" );
        o.allow_omitted_members();
        for( const JsonMember m : o ) {
            p.ammo_restriction[m.name()] = m.get_int();
        }
    }
    return p;
}

std::vector<s7_row> load_s7_rows()
{
    std::vector<s7_row> rows;
    std::ifstream file( stor7_batch, std::ios::binary );
    REQUIRE( file.good() );
    JsonIn jsin( file );
    JsonObject root = jsin.get_object();
    root.allow_omitted_members();
    JsonArray arr = root.get_array( "rows" );
    while( arr.has_more() ) {
        JsonObject jo = arr.next_object();
        jo.allow_omitted_members();
        s7_row r;
        r.id = jo.get_string( "id" );
        r.storage_ml = jo.get_int( "storage_ml" );
        r.get_storage_ml = jo.get_int( "get_storage_ml" );
        if( jo.has_string( "inherits_from" ) ) {
            r.inherits_from = jo.get_string( "inherits_from" );
        }
        JsonArray pa = jo.get_array( "pockets" );
        while( pa.has_more() ) {
            r.pockets.push_back( read_s7_pocket( pa.next_object() ) );
        }
        rows.push_back( r );
    }
    return rows;
}

const std::vector<s7_row> &stor7_rows()
{
    static const std::vector<s7_row> rows = load_s7_rows();
    return rows;
}

// The red-first snapshot files: the loaded pockets before the packet edit.
std::map<std::string, std::vector<s7_pocket>> read_before( const std::string &path )
{
    std::map<std::string, std::vector<s7_pocket>> out;
    std::ifstream file( path, std::ios::binary );
    REQUIRE( file.good() );
    JsonIn jsin( file );
    JsonObject root = jsin.get_object();
    root.allow_omitted_members();
    JsonObject items = root.get_object( "items" );
    for( const JsonMember member : items ) {
        JsonObject item = member.get_object();
        item.allow_omitted_members();
        std::vector<s7_pocket> pockets;
        JsonArray pa = item.get_array( "pockets" );
        while( pa.has_more() ) {
            pockets.push_back( read_s7_pocket( pa.next_object() ) );
        }
        out[member.name()] = pockets;
    }
    return out;
}

void check_s7_pocket( const pocket_data &p, const s7_pocket &e )
{
    CHECK( io::enum_to_string( p.type ) == e.type );
    if( e.type == "CONTAINER" ) {
        CHECK( units::to_milliliter( p.raw_volume_capacity ) == e.volume_ml );
        CHECK( p.carry == ( e.carry == 1 ) );
        CHECK( p.holster == ( e.holster == 1 ) );
        CHECK( p.rigid == ( e.rigid == 1 ) );
        if( e.length_mm >= 0 ) {
            CHECK( units::to_millimeter( p.max_item_length ) == e.length_mm );
        }
        if( e.max_item_volume_ml >= 0 ) {
            REQUIRE( p.max_item_volume.has_value() );
            CHECK( units::to_milliliter( *p.max_item_volume ) == e.max_item_volume_ml );
        } else {
            CHECK( !p.max_item_volume.has_value() );
        }
    } else if( e.type == "MAGAZINE_WELL" ) {
        CHECK( p.rigid == ( e.rigid == 1 ) );
        if( e.length_mm >= 0 ) {
            CHECK( units::to_millimeter( p.max_item_length ) == e.length_mm );
        }
        CHECK( p.default_magazine == itype_id( e.default_magazine ) );
        CHECK( p.get_flag_restrictions().size() == e.flag_restriction.size() );
        for( const std::string &f : e.flag_restriction ) {
            CHECK( p.get_flag_restrictions().count( flag_id( f ) ) == 1 );
        }
    } else if( e.type == "MAGAZINE" ) {
        CHECK( p.rigid == ( e.rigid == 1 ) );
        CHECK( p.ammo_restriction.size() == e.ammo_restriction.size() );
        for( const std::pair<const std::string, int> &kv : e.ammo_restriction ) {
            CHECK( p.ammo_restriction.count( ammotype( kv.first ) ) == 1 );
            if( p.ammo_restriction.count( ammotype( kv.first ) ) == 1 ) {
                CHECK( p.ammo_restriction.at( ammotype( kv.first ) ) == kv.second );
            }
        }
    }
}

void check_s7_row( const s7_row &r )
{
    const itype &t = *item::spawn_temporary( itype_id( r.id ) )->type;
    CAPTURE( r.id );
    CHECK( t.explicit_pockets );
    REQUIRE( t.pockets.size() == r.pockets.size() );
    for( size_t i = 0; i < r.pockets.size(); ++i ) {
        CAPTURE( i );
        check_s7_pocket( t.pockets[i], r.pockets[i] );
    }
    CHECK( units::to_milliliter( item::spawn_temporary( itype_id( r.id ) )->get_storage() ) ==
           r.get_storage_ml );
}

// Same comparison as tests/cdf_mag_port_test.cpp, for the keys a golden pocket carries.
void check_golden_pocket( const JsonObject &g, const pocket_data &p )
{
    bool was_loaded = false;
    if( g.has_member( "pocket_type" ) ) {
        CHECK( io::enum_to_string( p.type ) == g.get_string( "pocket_type" ) );
    }
    if( g.has_member( "default_magazine" ) ) {
        itype_id expected;
        optional( g, was_loaded, "default_magazine", expected );
        CHECK( p.default_magazine == expected );
    }
    if( g.has_member( "max_contains_volume" ) ) {
        units::volume expected = 0_ml;
        optional( g, was_loaded, "max_contains_volume", expected, volume_reader(), 0_ml );
        CHECK( p.raw_volume_capacity == expected );
    }
    if( g.has_member( "max_item_volume" ) ) {
        units::volume expected = 0_ml;
        optional( g, was_loaded, "max_item_volume", expected, volume_reader(), 0_ml );
        REQUIRE( p.max_item_volume.has_value() );
        CHECK( *p.max_item_volume == expected );
    }
    if( g.has_member( "max_item_length" ) ) {
        units::length expected = 0_mm;
        optional( g, was_loaded, "max_item_length", expected );
        CHECK( p.max_item_length == expected );
    }
    if( g.has_member( "rigid" ) ) {
        CHECK( p.rigid == g.get_bool( "rigid" ) );
    }
    if( g.has_member( "holster" ) ) {
        CHECK( p.holster == g.get_bool( "holster" ) );
    }
    if( g.has_member( "carry" ) ) {
        CHECK( p.carry == g.get_bool( "carry" ) );
    }
    if( g.has_member( "ammo_restriction" ) ) {
        std::map<ammotype, int> expected;
        optional( g, was_loaded, "ammo_restriction", expected );
        CHECK( p.ammo_restriction == expected );
    }
    if( g.has_member( "flag_restriction" ) ) {
        pocket_data::FlagsSetType expected;
        optional( g, was_loaded, "flag_restriction", expected );
        CHECK( p.get_flag_restrictions() == expected );
    }
}

const row_expect *find_row( const std::vector<row_expect> &rows, const std::string &id )
{
    for( const row_expect &r : rows ) {
        if( r.id == id ) {
            return &r;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE( "p3 storage STOR2: explicit pockets equal the pockets synthesized before",
           "[p3][storage]" )
{
    clear_all_state();
    const std::vector<const row_expect *> rows = rows_of( stor2_rows(), "NEUTRAL" );
    // CDF: STOR2 rows table: 13 NEUTRAL ids (16 objects; 2 layout, 5 children inherit)
    REQUIRE( rows.size() == 13 );
    for( const row_expect *r : rows ) {
        check_row( *r );
    }
}

TEST_CASE( "p3 storage STOR2: get_storage equals storage", "[p3][storage]" )
{
    clear_all_state();
    int checked = 0;
    for( const row_expect &r : stor2_rows() ) {
        if( r.cls == "NEUTRAL-INHERIT" ) {
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

TEST_CASE( "p3 storage STOR2: worn capacity rises by the storage", "[p3][storage]" )
{
    int checked = 0;
    for( const row_expect &r : stor2_rows() ) {
        if( r.cls == "NEUTRAL-INHERIT" ) {
            continue;
        }
        CAPTURE( r.id );
        clear_all_state();
        avatar &u = get_avatar();
        clear_character( u, false );
        u.unset_mutation( trait_id( "DEBUG_STORAGE" ) );
        // CDF: a POWERARMOR_MOD garment attaches to an exoskeleton and cannot be worn alone
        if( itype_id( r.id ).obj().has_flag( flag_id( "POWERARMOR_MOD" ) ) ) {
            detached_ptr<item> exo = item::spawn( itype_id( "power_armor_exo_1" ), calendar::turn );
            REQUIRE( !u.wear_item( std::move( exo ), false ) );
        }
        const units::volume cap0 = u.carried_capacity();
        detached_ptr<item> it = item::spawn( itype_id( r.id ), calendar::turn );
        // CDF: wear_item returns the item back on failure and an empty pointer on success
        REQUIRE( !u.wear_item( std::move( it ), false ) );
        // the holster strap adds nothing (CAP1c); the main / layout pockets add the storage
        CHECK( u.carried_capacity() == cap0 + units::from_milliliter( r.get_storage_ml ) );
        checked++;
    }
    CHECK( checked > 0 );
}

TEST_CASE( "p3 storage STOR2: children inherit", "[p3][storage]" )
{
    clear_all_state();
    const std::vector<const row_expect *> rows = rows_of( stor2_rows(), "NEUTRAL-INHERIT" );
    // CDF: STOR2 rows table: power_armor_exo_1..3_on, armor_plarmor, armor_scute
    REQUIRE( rows.size() == 5 );
    for( const row_expect *r : rows ) {
        CAPTURE( r->id, r->parent );
        REQUIRE( loaded( *r ) );
        const itype &child = *item::spawn_temporary( itype_id( r->id ) )->type;
        const itype &parent = *item::spawn_temporary( itype_id( r->parent ) )->type;
        CHECK( child.explicit_pockets );
        REQUIRE( child.pockets.size() == parent.pockets.size() );
        for( size_t i = 0; i < parent.pockets.size(); ++i ) {
            CHECK( child.pockets[i].raw_volume_capacity == parent.pockets[i].raw_volume_capacity );
            CHECK( child.pockets[i].max_item_length == parent.pockets[i].max_item_length );
            CHECK( child.pockets[i].carry == parent.pockets[i].carry );
            CHECK( child.pockets[i].holster == parent.pockets[i].holster );
            CHECK( child.pockets[i].rigid == parent.pockets[i].rigid );
        }
        CHECK( units::to_milliliter( child.pockets[0].raw_volume_capacity ) == r->storage_ml );
    }
}

TEST_CASE( "p3 storage STOR2: strap garments", "[p3][storage]" )
{
    clear_all_state();
    // CDF: STOR2 rows table: the five storage >= 10 L garments with their own edit
    const std::vector<std::string> strap_ids = {
        "power_armor_frame", "heavy_power_armor_frame", "armor_nomad",
        "armor_nomad_light", "armor_scavenger"
    };
    for( const std::string &id : strap_ids ) {
        CAPTURE( id );
        const row_expect *r = find_row( stor2_rows(), id );
        REQUIRE( r != nullptr );
        const itype &t = *item::spawn_temporary( itype_id( id ) )->type;
        std::vector<const pocket_data *> loaded;
        for( const pocket_data &p : t.pockets ) {
            if( p.type == pocket_type::CONTAINER ) {
                loaded.push_back( &p );
            }
        }
        REQUIRE( loaded.size() == 2 );
        const pocket_data &strap = *loaded[1];
        CHECK( strap.type == pocket_type::CONTAINER );
        CHECK( strap.holster );
        CHECK( !strap.carry );
        CHECK( strap.rigid );
        CHECK( units::to_milliliter( strap.raw_volume_capacity ) == 10000 );
        CHECK( units::to_millimeter( strap.max_item_length ) == 1500 );
        REQUIRE( strap.max_item_volume.has_value() );
        CHECK( units::to_milliliter( *strap.max_item_volume ) == 10000 );
        CHECK( units::to_milliliter( item::spawn_temporary( itype_id( id ) )->get_storage() ) ==
               r->storage_ml );
    }
}

TEST_CASE( "p3 storage STOR2: survivor suits follow the parent layout shape", "[p3][storage]" )
{
    clear_all_state();
    const std::vector<const row_expect *> rows = rows_of( stor2_rows(), "PARENT-LAYOUT-SHAPE" );
    REQUIRE( rows.size() == 2 );
    for( const row_expect *r : rows ) {
        CAPTURE( r->id );
        int sum = 0;
        for( const pk_expect &e : r->pockets ) {
            sum += e.volume_ml;
        }
        // CDF: scaled to the CDF storage of the child (15 L): hsurvivor_suit 2/2/1/1 L -> x2.5,
        // lsurvivor_suit 2/2/1500/1500 ml -> x15/7 (DDA item_overlay layouts)
        CHECK( r->pockets.size() == 4u );
        CHECK( sum == 15000 );
        CHECK( r->storage_ml == 15000 );
        check_layout_row( *r );
        CHECK( units::to_milliliter( item::spawn_temporary( itype_id( r->id ) )->get_storage() ) == 15000 );
    }
    // CDF: sasurvivor_suit 5000/5000/2500/2500, salsurvivor_suit 4286/4286/3214/3214
    const row_expect *sa = find_row( stor2_rows(), "sasurvivor_suit" );
    REQUIRE( sa != nullptr );
    CHECK( sa->pockets[0].volume_ml == 5000 );
    CHECK( sa->pockets[2].volume_ml == 2500 );
    const row_expect *sal = find_row( stor2_rows(), "salsurvivor_suit" );
    REQUIRE( sal != nullptr );
    CHECK( sal->pockets[0].volume_ml == 4286 );
    CHECK( sal->pockets[2].volume_ml == 3214 );
}

TEST_CASE( "p3 storage STOR2: mod child of an edited parent keeps its own pockets",
           "[p3][storage]" )
{
    clear_all_state();
    const std::map<std::string, std::vector<s7_pocket>> before = read_before( stor2_before );
    REQUIRE( before.count( "armor_wyrm" ) == 1 );
    const std::vector<s7_pocket> &want = before.at( "armor_wyrm" );
    REQUIRE( want.size() == 2 );
    REQUIRE( want[0].type == "MAGAZINE" );
    REQUIRE( want[1].type == "MOD" );
    // CDF: Arcana_BN is not one of the mods the test binary loads; check the written object.
    const std::string wyrm_file = "data/mods/Arcana_BN/items/tool_armor.json";
    std::ifstream file( wyrm_file, std::ios::binary );
    REQUIRE( file.good() );
    JsonIn jsin( file );
    JsonArray all = jsin.get_array();
    bool found = false;
    std::vector<s7_pocket> written;
    while( all.has_more() ) {
        JsonObject jo = all.next_object();
        jo.allow_omitted_members();
        if( !jo.has_string( "id" ) || jo.get_string( "id" ) != "armor_wyrm" ) {
            continue;
        }
        found = true;
        JsonArray pa = jo.get_array( "pocket_data" );
        while( pa.has_more() ) {
            written.push_back( read_written_pocket( pa.next_object() ) );
        }
    }
    CHECK( found );
    REQUIRE( written.size() == want.size() );
    // the guard reproduces today's integral MAGAZINE pocket (add_integral_magazine_pocket) and MOD
    CHECK( written[0].type == "MAGAZINE" );
    CHECK( written[0].rigid == 1 );
    CHECK( written[0].holster == 1 );
    CHECK( written[0].watertight == 1 );
    CHECK( written[0].volume_ml == want[0].volume_ml );
    CHECK( written[0].weight_g == want[0].weight_g );
    CHECK( written[0].length_mm == want[0].length_mm );
    CHECK( written[0].ammo_restriction == want[0].ammo_restriction );
    CHECK( written[1].type == "MOD" );
    for( const s7_pocket &p : written ) {
        CHECK( p.type != "CONTAINER" );
    }
    // the edited parent keeps the 1 L carry pocket
    const itype &blarmor = *item::spawn_temporary( itype_id( "armor_blarmor" ) )->type;
    CHECK( blarmor.explicit_pockets );
    REQUIRE( blarmor.pockets.size() == 1 );
    CHECK( blarmor.pockets[0].type == pocket_type::CONTAINER );
    CHECK( units::to_milliliter( blarmor.pockets[0].raw_volume_capacity ) == 1000 );
    CHECK( blarmor.pockets[0].carry );
    CHECK( blarmor.pockets[0].rigid );
    CHECK( units::to_milliliter( item::spawn_temporary( itype_id( "armor_blarmor" ) )->get_storage() )
           == 1000 );
    // the committed golden entry for armor_wyrm (MAGAZINE, rigid true, essence 16) is unchanged
    std::ifstream gf( "tests/data/dda_staged/p3_golden.json", std::ios::binary );
    REQUIRE( gf.good() );
    JsonIn gjsin( gf );
    JsonObject golden = gjsin.get_object();
    golden.allow_omitted_members();
    JsonObject items = golden.get_object( "items" );
    items.allow_omitted_members();
    JsonObject wyrm = items.get_object( "armor_wyrm" );
    wyrm.allow_omitted_members();
    JsonArray gold_pockets = wyrm.get_array( "pockets" );
    REQUIRE( gold_pockets.size() == 1 );
    JsonObject gp = gold_pockets.next_object();
    gp.allow_omitted_members();
    CHECK( gp.get_string( "pocket_type" ) == written[0].type );
    CHECK( gp.get_bool( "rigid" ) == ( written[0].rigid == 1 ) );
    JsonObject g_ammo = gp.get_object( "ammo_restriction" );
    g_ammo.allow_omitted_members();
    REQUIRE( g_ammo.size() == written[0].ammo_restriction.size() );
    for( const JsonMember m : g_ammo ) {
        REQUIRE( written[0].ammo_restriction.count( m.name() ) == 1 );
        CHECK( written[0].ammo_restriction.at( m.name() ) == m.get_int() );
    }
}

TEST_CASE( "p3 storage STOR7: the explicit list reproduces today's pockets", "[p3][storage]" )
{
    clear_all_state();
    const std::vector<s7_row> &rows = stor7_rows();
    // CDF: STOR7 rows: exosuit_industrial(_on), exosuit_military(_on)
    REQUIRE( rows.size() == 4 );
    const std::map<std::string, std::vector<s7_pocket>> before = read_before( stor7_before );
    for( const s7_row &r : rows ) {
        CAPTURE( r.id );
        check_s7_row( r );
        // same pocket list as before the edit, except the ruled flags: storage pockets rigid,
        // the industrial strap no longer carry
        REQUIRE( before.count( r.id ) == 1 );
        const std::vector<s7_pocket> &b = before.at( r.id );
        REQUIRE( b.size() == r.pockets.size() );
        for( size_t i = 0; i < r.pockets.size(); ++i ) {
            CAPTURE( i );
            CHECK( b[i].type == r.pockets[i].type );
            CHECK( b[i].volume_ml == r.pockets[i].volume_ml );
            if( r.pockets[i].type == "MAGAZINE_WELL" ) {
                CHECK( b[i].default_magazine == r.pockets[i].default_magazine );
                CHECK( b[i].length_mm == r.pockets[i].length_mm );
            }
        }
    }
    CHECK( item::spawn_temporary( itype_id( "exosuit_industrial" ) )->get_storage() == 15000_ml );
    CHECK( item::spawn_temporary( itype_id( "exosuit_military" ) )->get_storage() == 7000_ml );
}

TEST_CASE( "p3 storage STOR7: capacity and charges unchanged", "[p3][storage]" )
{
    clear_all_state();
    // CDF: the well's default cell is heavy_battery_cell (CDF capacity 1000, data/json/items/battery.json)
    const int cell_capacity = itype_id( "heavy_battery_cell" ).obj().magazine->capacity;
    REQUIRE( cell_capacity > 0 );
    for( const s7_row &r : stor7_rows() ) {
        CAPTURE( r.id );
        item it( itype_id( r.id ), calendar::turn );
        CHECK( it.ammo_capacity( true ) == cell_capacity );
        // CDF: 7-R4b moved the battery into the MAGAZINE_WELL pocket; no legacy integral value
        REQUIRE( itype_id( r.id ).obj().tool );
        CHECK( itype_id( r.id ).obj().tool->max_charges == 0 );
    }
}

TEST_CASE( "p3 storage STOR7: the _on forms inherit", "[p3][storage]" )
{
    clear_all_state();
    int checked = 0;
    for( const s7_row &r : stor7_rows() ) {
        if( r.inherits_from.empty() ) {
            continue;
        }
        CAPTURE( r.id, r.inherits_from );
        const itype &child = *item::spawn_temporary( itype_id( r.id ) )->type;
        const itype &parent = *item::spawn_temporary( itype_id( r.inherits_from ) )->type;
        REQUIRE( child.pockets.size() == parent.pockets.size() );
        for( size_t i = 0; i < parent.pockets.size(); ++i ) {
            CHECK( child.pockets[i].type == parent.pockets[i].type );
            CHECK( child.pockets[i].raw_volume_capacity == parent.pockets[i].raw_volume_capacity );
            CHECK( child.pockets[i].carry == parent.pockets[i].carry );
            CHECK( child.pockets[i].holster == parent.pockets[i].holster );
            CHECK( child.pockets[i].rigid == parent.pockets[i].rigid );
            CHECK( child.pockets[i].max_item_length == parent.pockets[i].max_item_length );
        }
        checked++;
    }
    CHECK( checked == 2 );
}

TEST_CASE( "p3 storage STOR7: worn capacity rises by the storage", "[p3][storage]" )
{
    int checked = 0;
    for( const s7_row &r : stor7_rows() ) {
        CAPTURE( r.id );
        clear_all_state();
        avatar &u = get_avatar();
        clear_character( u, false );
        u.unset_mutation( trait_id( "DEBUG_STORAGE" ) );
        const units::volume cap0 = u.carried_capacity();
        detached_ptr<item> it = item::spawn( itype_id( r.id ), calendar::turn );
        REQUIRE( !u.wear_item( std::move( it ), false ) );
        // the 10 L strap is a holster and adds nothing (CAP1c)
        CHECK( u.carried_capacity() == cap0 + units::from_milliliter( r.get_storage_ml ) );
        checked++;
    }
    CHECK( checked == 4 );
}

TEST_CASE( "p3 storage STOR7: the four golden entries equal the loaded pockets", "[p3][storage]" )
{
    clear_all_state();
    std::ifstream file( "tests/data/dda_staged/p3_golden.json", std::ios::binary );
    REQUIRE( file.good() );
    JsonIn jsin( file );
    JsonObject golden = jsin.get_object();
    golden.allow_omitted_members();
    JsonObject items = golden.get_object( "items" );
    items.allow_omitted_members();
    for( const s7_row &r : stor7_rows() ) {
        CAPTURE( r.id );
        REQUIRE( items.has_object( r.id ) );
        JsonObject item = items.get_object( r.id );
        item.allow_omitted_members();
        JsonArray gold_pockets = item.get_array( "pockets" );
        // golden_view: the golden never lists the MOD pocket
        std::vector<const pocket_data *> loaded;
        for( const pocket_data &p : item::spawn_temporary( itype_id( r.id ) )->type->pockets ) {
            if( p.type != pocket_type::MOD && p.type != pocket_type::CORPSE &&
                p.type != pocket_type::SOFTWARE ) {
                loaded.push_back( &p );
            }
        }
        REQUIRE( loaded.size() == gold_pockets.size() );
        size_t i = 0;
        while( gold_pockets.has_more() ) {
            JsonObject gp = gold_pockets.next_object();
            gp.allow_omitted_members();
            CAPTURE( i );
            check_golden_pocket( gp, *loaded[i] );
            i++;
        }
        // the strap CONTAINER is present for the industrial suit only
        int holsters = 0;
        for( const pocket_data *p : loaded ) {
            if( p->type == pocket_type::CONTAINER && p->holster ) {
                holsters++;
            }
        }
        CHECK( holsters == ( r.storage_ml >= 10000 ? 1 : 0 ) );
    }
}

