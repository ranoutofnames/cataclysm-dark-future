#pragma once

#include "units_def.h"

#include <cstdint>

namespace units
{

// CDF: E1-1, DDA units_fwd.h:77-81, units.h:780-795, :1150-1162. Electronic memory in
// kilobytes (decimal, 1 MB = 1000 KB as in DDA), 64 bit so a terabyte fits.
class ememory_in_kilobytes_tag
{
};

using ememory = quantity<std::int64_t, ememory_in_kilobytes_tag>;

template<typename value_type>
inline constexpr auto from_kilobytes( const value_type v ) -> quantity<value_type, ememory_in_kilobytes_tag>
{
    return quantity<value_type, ememory_in_kilobytes_tag>( v, ememory_in_kilobytes_tag{} );
}

template<typename value_type>
inline constexpr auto from_megabytes( const value_type v ) -> quantity<value_type, ememory_in_kilobytes_tag>
{
    return from_kilobytes<value_type>( v ) * 1000;
}

template<typename value_type>
inline constexpr auto from_gigabytes( const value_type v ) -> quantity<value_type, ememory_in_kilobytes_tag>
{
    return from_megabytes<value_type>( v ) * 1000;
}

template<typename value_type>
inline constexpr auto from_terabytes( const value_type v ) -> quantity<value_type, ememory_in_kilobytes_tag>
{
    return from_gigabytes<value_type>( v ) * 1000;
}

} // namespace units

inline constexpr auto operator""_KB( const unsigned long long b ) -> units::ememory
{
    return units::from_kilobytes( b );
}

inline constexpr auto operator""_MB( const unsigned long long b ) -> units::ememory
{
    return units::from_megabytes( b );
}

inline constexpr auto operator""_GB( const unsigned long long b ) -> units::ememory
{
    return units::from_gigabytes( b );
}

inline constexpr auto operator""_TB( const unsigned long long b ) -> units::ememory
{
    return units::from_terabytes( b );
}
