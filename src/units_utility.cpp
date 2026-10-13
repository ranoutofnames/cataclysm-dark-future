#include "units_utility.h"

#include <cmath>
#include <cstdint>

#include "options.h"
#include "string_formatter.h"

const char *velocity_units( const units_type vel_units )
{
    if( get_option<std::string>( "USE_METRIC_SPEEDS" ) == "mph" ) {
        return _( "mph" );
    } else if( get_option<std::string>( "USE_METRIC_SPEEDS" ) == "t/t" ) {
        //~ vehicle speed tiles per turn
        return _( "t/t" );
    } else {
        switch( vel_units ) {
            case VU_VEHICLE:
                return _( "km/h" );
            case VU_WIND:
                return _( "m/s" );
        }
    }
    return "error: unknown units!";
}

units::angle normalize( units::angle a, units::angle mod )
{
    a = units::fmod( a, mod );
    if( a < 0_degrees ) {
        a += mod;
    }
    return a;
}

const char *weight_units()
{
    return get_option<std::string>( "USE_METRIC_WEIGHTS" ) == "lbs" ? _( "lbs" ) : _( "kg" );
}

const char *volume_units_abbr()
{
    const std::string vol_units = get_option<std::string>( "VOLUME_UNITS" );
    if( vol_units == "c" ) {
        return pgettext( "Volume unit", "c" );
    } else if( vol_units == "l" ) {
        return pgettext( "Volume unit", "L" );
    } else {
        return pgettext( "Volume unit", "qt" );
    }
}

const char *volume_units_long()
{
    const std::string vol_units = get_option<std::string>( "VOLUME_UNITS" );
    if( vol_units == "c" ) {
        return _( "cup" );
    } else if( vol_units == "l" ) {
        return _( "liter" );
    } else {
        return _( "quart" );
    }
}

double convert_velocity( int velocity, const units_type vel_units )
{
    const std::string type = get_option<std::string>( "USE_METRIC_SPEEDS" );
    if( vel_units == VU_VEHICLE ) {
        const double meters_per_second = static_cast<double>( velocity ) / 100.0;
        if( type == "mph" ) {
            return meters_per_second * 2.23694;
        }
        if( type == "t/t" ) {
            return meters_per_second / 1.78816;
        }
        return meters_per_second * 3.6;
    }

    double ret = static_cast<double>( velocity ) / 100;
    if( type == "km/h" ) {
        ret *= 0.447f;
    } else if( type == "t/t" ) {
        ret /= 4;
    }

    return ret;
}

double convert_weight( const units::mass &weight )
{
    double ret = to_gram( weight );
    if( get_option<std::string>( "USE_METRIC_WEIGHTS" ) == "kg" ) {
        ret /= 1000;
    } else {
        ret /= 453.6;
    }
    return ret;
}

double convert_volume( int volume )
{
    return convert_volume( volume, nullptr );
}

double convert_volume( int volume, int *out_scale )
{
    double ret = volume;
    int scale = 0;
    const std::string vol_units = get_option<std::string>( "VOLUME_UNITS" );
    if( vol_units == "c" ) {
        ret *= 0.004;
        scale = 1;
    } else if( vol_units == "l" ) {
        ret *= 0.001;
        scale = 2;
    } else {
        ret *= 0.00105669;
        scale = 2;
    }
    if( out_scale != nullptr ) {
        *out_scale = scale;
    }
    return ret;
}

double convert_length_cm_in( const units::length &length )
{
    double ret = units::to_millimeter( length );
    if( get_option<std::string>( "DISTANCE_UNITS" ) == "metric" ) {
        ret /= 10;
    } else {
        ret /= 25.4;
    }
    return ret;
}

namespace
{
struct length_choice {
    int value = 0;
    const char *unit = "";
};

length_choice choose_length( const units::length &length )
{
    length_choice res;
    const std::int64_t mm = units::to_millimeter( length );
    if( get_option<std::string>( "DISTANCE_UNITS" ) == "metric" ) {
        if( mm != 0 && mm % 1000000 == 0 ) {
            res = { static_cast<int>( mm / 1000000 ), _( "km" ) };
        } else if( mm != 0 && mm % 1000 == 0 ) {
            res = { static_cast<int>( mm / 1000 ), _( "m" ) };
        } else if( mm % 10 == 0 ) {
            res = { static_cast<int>( mm / 10 ), _( "cm" ) };
        } else {
            res = { static_cast<int>( mm ), _( "mm" ) };
        }
    } else {
        // Imperial can only be approximated: round to whole inches first.
        const std::int64_t in = std::llround( static_cast<double>( mm ) / 25.4 );
        if( in != 0 && in % 63360 == 0 ) {
            res = { static_cast<int>( in / 63360 ), _( "mi" ) };
        } else if( in != 0 && in % 36 == 0 ) {
            res = { static_cast<int>( in / 36 ), _( "yd" ) };
        } else if( in != 0 && in % 12 == 0 ) {
            res = { static_cast<int>( in / 12 ), _( "ft" ) };
        } else {
            res = { static_cast<int>( in ), _( "in" ) };
        }
    }
    return res;
}
} // namespace

int convert_length( const units::length &length )
{
    return choose_length( length ).value;
}

std::string length_units( const units::length &length )
{
    return choose_length( length ).unit;
}

std::string string_format_length( const units::length &length, bool compact )
{
    const length_choice c = choose_length( length );
    return string_format( "%d%s%s", c.value, compact ? "" : " ", c.unit );
}
