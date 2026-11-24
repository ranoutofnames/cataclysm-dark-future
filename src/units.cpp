#include <cmath>
#include <string>

#include "json.h"
#include "string_formatter.h"
#include "units.h"
#include "units_serde.h"
#include "translations.h"

namespace units
{
template<>
void volume::serialize( JsonOut &jsout ) const
{
    if( value_ % 1000 == 0 ) {
        jsout.write( std::to_string( value_ / 1000 ) + " L" );
    } else {
        jsout.write( std::to_string( value_ ) + " ml" );
    }
}

template<>
void mass::serialize( JsonOut &jsout ) const
{
    if( value_ % 1000000 == 0 ) {
        jsout.write( string_format( "%d kg", value_ / 1000000 ) );
    } else if( value_ % 1000 == 0 ) {
        jsout.write( string_format( "%d g", value_ / 1000 ) );
    } else {
        jsout.write( string_format( "%d mg", value_ ) );
    }
}

template<>
void energy::serialize( JsonOut &jsout ) const
{
    if( value_ % 1000 == 0 ) {
        jsout.write( string_format( "%d kJ", value_ / 1000 ) );
    } else {
        jsout.write( string_format( "%d J", value_ ) ) ;
    }
}

template<>
void money::serialize( JsonOut &jsout ) const
{
    if( value_ % 100 == 0 ) {
        jsout.write( string_format( "%d USD", value_ / 100 ) );
    } else {
        jsout.write( string_format( "%d cent", value_ / 100 ) );
    }
}

template<>
void sound::serialize( JsonOut &jsout ) const
{
    jsout.write( string_format( "%d dB", value_ ) );
}

template<>
void length::serialize( JsonOut &jsout ) const
{
    if( value_ != 0 && value_ % 1000000 == 0 ) {
        jsout.write( string_format( "%d km", value_ / 1000000 ) );
    } else if( value_ != 0 && value_ % 1000 == 0 ) {
        jsout.write( string_format( "%d meter", value_ / 1000 ) );
    } else if( value_ % 10 == 0 ) {
        jsout.write( string_format( "%d cm", value_ / 10 ) );
    } else {
        jsout.write( string_format( "%d mm", value_ ) );
    }
}

template<>
void length::deserialize( JsonIn &jsin )
{
    *this = read_from_json_string( jsin, units::length_units );
}

template<>
void money::deserialize( JsonIn &jsin )
{
    *this = read_from_json_string<money>( jsin, units::money_units );
}

template<>
void sound::deserialize( JsonIn &jsin )
{
    *this = read_from_json_string<sound>( jsin, units::sound_units );
}

template<>
void energy::deserialize( JsonIn &jsin )
{
    *this = read_from_json_string( jsin, units::energy_units );
}

// CDF: E1-1, DDA units.cpp:157-188
template<>
void ememory::serialize( JsonOut &jsout ) const
{
    jsout.write( string_format( "%d KB", value_ ) );
}

template<>
void ememory::deserialize( JsonIn &jsin )
{
    *this = read_from_json_string( jsin, units::ememory_units );
}

template<>
void angle::serialize( JsonOut &jsout ) const
{
    jsout.write( string_format( "%f rad", value_ ) );
}

template<>
void angle::deserialize( JsonIn &jsin )
{
    *this = read_from_json_string( jsin, units::angle_units );
}

std::string display( const units::ememory &v )
{
    // DDA units.cpp:164-186: the largest unit the value reaches, one decimal from MB up
    int64_t ebytes = v.value();
    size_t i = 0;
    for( i = units::ememory_units.size() - 1; i > 0; i-- ) {
        const int64_t metric_factor = static_cast<int64_t>( std::pow( 10, 3 * i ) );
        if( ebytes >= metric_factor ) {
            const double ebytes_decimal = static_cast<double>( ebytes ) / metric_factor;
            return string_format( "%.1f %s", ebytes_decimal, units::ememory_units[i].first );
        }
    }
    return string_format( "%d %s", ebytes, units::ememory_units[i].first );
}

std::string display( const units::energy v )
{
    const int kj = units::to_kilojoule( v );
    const int j = units::to_joule( v );
    // at least 1 kJ and there is no fraction
    if( kj >= 1 && float( j ) / kj == 1000 ) {
        return std::to_string( kj ) + ' ' + pgettext( "energy unit: kilojoule", "kJ" );
    }
    return std::to_string( j ) + ' ' + pgettext( "energy unit: joule", "J" );
}
} // namespace units
