#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

#include "units_def.h"
#include "units_volume.h"

namespace units
{

class length_in_millimeter_tag
{
};

using length = quantity<std::int64_t, length_in_millimeter_tag>;

const length length_min = units::length( std::numeric_limits<units::length::value_type>::min(),
                          units::length::unit_type{} );

const length length_max = units::length( std::numeric_limits<units::length::value_type>::max(),
                         units::length::unit_type{} );

template<typename value_type>
constexpr quantity<value_type, length_in_millimeter_tag> from_millimeter( const value_type v )
{
    return quantity<value_type, length_in_millimeter_tag>( v, length_in_millimeter_tag{} );
}

template<typename value_type>
constexpr quantity<value_type, length_in_millimeter_tag> from_centimeter( const value_type v )
{
    return from_millimeter( v * 10 );
}

template<typename value_type>
constexpr quantity<value_type, length_in_millimeter_tag> from_meter( const value_type v )
{
    return from_millimeter( v * 1000 );
}

template<typename value_type>
constexpr quantity<value_type, length_in_millimeter_tag> from_kilometer( const value_type v )
{
    return from_millimeter( v * 1000000 );
}

template<typename value_type>
constexpr value_type to_millimeter( const quantity<value_type, length_in_millimeter_tag> &v )
{
    return v / from_millimeter<value_type>( 1 );
}

template<typename value_type>
constexpr value_type to_centimeter( const quantity<value_type, length_in_millimeter_tag> &v )
{
    return to_millimeter( v ) / 10;
}

template<typename value_type>
constexpr value_type to_meter( const quantity<value_type, length_in_millimeter_tag> &v )
{
    return to_millimeter( v ) / 1000;
}

template<typename value_type>
constexpr value_type to_kilometer( const quantity<value_type, length_in_millimeter_tag> &v )
{
    return to_meter( v ) / 1000;
}

/**
 * The length of one side of a cube with the given volume, rounded to whole centimeters.
 * The default item length (DDA #40186) and the base of the default pocket length limit.
 */
inline length default_length_from_volume( const volume &v )
{
    return from_centimeter<std::int64_t>(
               static_cast<std::int64_t>( std::round( std::cbrt( static_cast<double>( to_milliliter( v ) ) ) ) ) );
}

} // namespace units

constexpr units::length operator""_mm( const unsigned long long v )
{
    return units::from_millimeter<std::int64_t>( v );
}

constexpr units::length operator""_cm( const unsigned long long v )
{
    return units::from_centimeter<std::int64_t>( v );
}

constexpr units::length operator""_m( const unsigned long long v )
{
    return units::from_meter<std::int64_t>( v );
}

constexpr units::length operator""_km( const unsigned long long v )
{
    return units::from_kilometer<std::int64_t>( v );
}

constexpr units::quantity<double, units::length_in_millimeter_tag> operator""_mm(
    const long double v )
{
    return units::from_millimeter<double>( v );
}

constexpr units::quantity<double, units::length_in_millimeter_tag> operator""_cm(
    const long double v )
{
    return units::from_centimeter<double>( v );
}

constexpr units::quantity<double, units::length_in_millimeter_tag> operator""_m(
    const long double v )
{
    return units::from_meter<double>( v );
}

constexpr units::quantity<double, units::length_in_millimeter_tag> operator""_km(
    const long double v )
{
    return units::from_kilometer<double>( v );
}
