#include "audit_support.h"

#include "avatar.h"
#include "calendar.h"
#include "detached_ptr.h"
#include "item.h"
#include "item_pocket.h"
#include "type_id.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace audit
{

std::string tsv_safe( const std::string &value )
{
    std::string out = value;
    for( char &c : out ) {
        if( c == '\t' || c == '\n' || c == '\r' ) {
            c = ' ';
        }
    }
    return out;
}

std::string compact( const std::string &value, std::size_t limit )
{
    std::string out = tsv_safe( value );
    std::string collapsed;
    collapsed.reserve( out.size() );
    bool last_space = false;
    for( const char c : out ) {
        const bool space = c == ' ';
        if( space && last_space ) {
            continue;
        }
        collapsed += c;
        last_space = space;
    }
    while( !collapsed.empty() && collapsed.front() == ' ' ) {
        collapsed.erase( collapsed.begin() );
    }
    while( !collapsed.empty() && collapsed.back() == ' ' ) {
        collapsed.pop_back();
    }
    if( collapsed.size() > limit ) {
        collapsed.resize( limit );
        collapsed += "...";
    }
    return collapsed;
}

void write_tsv( const std::string &path, const std::vector<std::string> &header,
                const std::vector<std::vector<std::string>> &rows )
{
    const std::filesystem::path file( path );
    if( file.has_parent_path() ) {
        std::filesystem::create_directories( file.parent_path() );
    }
    std::ofstream out( path, std::ios::trunc );
    if( !out ) {
        return;
    }
    const auto write_cells = [&out]( const std::vector<std::string> &cells ) {
        for( std::size_t i = 0; i < cells.size(); ++i ) {
            if( i > 0 ) {
                out << '\t';
            }
            out << tsv_safe( cells[i] );
        }
        out << '\n';
    };
    write_cells( header );
    for( const std::vector<std::string> &cells : rows ) {
        std::vector<std::string> padded = cells;
        padded.resize( std::max( header.size(), cells.size() ) );
        write_cells( padded );
    }
}

std::vector<std::string> read_keys( const std::string &path )
{
    std::vector<std::string> keys;
    std::ifstream in( path );
    std::string line;
    while( std::getline( in, line ) ) {
        if( !line.empty() && line.back() == '\r' ) {
            line.pop_back();
        }
        if( line.empty() || line.front() == '#' ) {
            continue;
        }
        const std::size_t tab = line.find( '\t' );
        if( tab == std::string::npos ) {
            keys.push_back( line + '\t' );
            continue;
        }
        keys.push_back( line.substr( 0, tab ) + '\t' + line.substr( tab + 1, line.find( '\t', tab + 1 ) - tab - 1 ) );
    }
    return keys;
}

std::string key( const row &r )
{
    return r.item + '\t' + r.flow;
}

bool allows( const row &r, const std::vector<std::string> &allowlist )
{
    return std::find( allowlist.begin(), allowlist.end(), key( r ) ) != allowlist.end();
}

std::string join( const std::vector<std::string> &values, const std::string &separator )
{
    std::string out;
    for( std::size_t i = 0; i < values.size(); ++i ) {
        if( i > 0 ) {
            out += separator;
        }
        out += values[i];
    }
    return out;
}

namespace
{
// Spawns @p id and returns both the raw pointer (for the scene) and the owning pointer.
detached_ptr<item> spawn_item( const char *id, int charges = 0 )
{
    if( charges > 0 ) {
        return item::spawn( itype_id( id ), calendar::turn, charges );
    }
    return item::spawn( itype_id( id ), calendar::turn );
}

// Puts @p id into @p outer's container pocket; the raw pointer goes to @p out.
bool put_container_item( item &outer, const char *id, item *&out, int charges = 0 )
{
    detached_ptr<item> ptr = spawn_item( id, charges );
    item *const raw = ptr.get();
    if( !outer.put_in( std::move( ptr ), pocket_type::CONTAINER ).success() ) {
        return false;
    }
    out = raw;
    return true;
}

// Wears @p id and returns the worn item (wear_item inserts by layer, not at the end).
item *wear( avatar &u, const char *id )
{
    const itype_id type( id );
    u.wear_item( spawn_item( id ), false );
    for( item *worn_item : u.worn.as_vector() ) {
        if( worn_item->typeId() == type ) {
            return worn_item;
        }
    }
    return nullptr;
}
} // namespace

bool build_pack_scene( avatar &u, item *&jacket, item *&pants, item *&pack, item *&box,
                       item *&rock )
{
    jacket = wear( u, "jacket_windbreaker" );
    pants = wear( u, "pants_cargo" );
    pack = wear( u, "backpack_hiking" );
    if( jacket == nullptr || pants == nullptr || pack == nullptr ) {
        return false;
    }
    return put_container_item( *pack, "box_small", box ) &&
           put_container_item( *box, "test_rock", rock );
}

bool build_jacket_box_scene( avatar &u, item *&jacket, item *&box, item *&cube )
{
    jacket = wear( u, "jacket_windbreaker" );
    if( jacket == nullptr ) {
        return false;
    }
    return put_container_item( *jacket, "box_small", box ) &&
           put_container_item( *box, "test_1kg_cube", cube );
}

bool build_drop_scene( avatar &u, item *&box, item *&rock )
{
    box = &u.i_add( spawn_item( "box_small" ) );
    rock = &u.i_add( spawn_item( "test_rock", 3 ) );
    return true;
}

bool build_vest_scene( avatar &u, item *&vest, item *&medium_plate, item *&large_plate )
{
    vest = wear( u, "ballistic_vest_esapi" );
    if( vest == nullptr ) {
        return false;
    }
    item *ignored = nullptr;
    if( !put_container_item( *vest, "esapi_plate", ignored ) ||
        !put_container_item( *vest, "esapi_plate", ignored ) ) {
        return false;
    }
    medium_plate = &u.i_add( spawn_item( "esbi_plate" ) );
    large_plate = &u.i_add( spawn_item( "esapi_plate" ) );
    return true;
}

} // namespace audit
