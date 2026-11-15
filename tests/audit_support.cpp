#include "audit_support.h"

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

} // namespace audit
