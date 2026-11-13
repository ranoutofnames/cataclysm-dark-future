#include "iteminfo_format_utils.h"

#include <string>

#include "cata_utility.h"
#include "output.h"
#include "string_formatter.h"
#include "units.h"
#include "units_utility.h"

void insert_separation_line(std::vector<iteminfo>& info) {
    if (info.empty() || info.back().sName != "--") {
        info.push_back(iteminfo("DESCRIPTION", "--"));
    }
}

// DDA item_info.cpp:4568 and :4585
auto vol_to_info(const std::string& type, const std::string& left, const units::volume& vol,
                 int decimal_places, bool lower_is_better) -> iteminfo {
    iteminfo::flags f = iteminfo::no_newline;
    if (lower_is_better) { f |= iteminfo::lower_is_better; }
    int converted_volume_scale = 0;
    const double converted_volume =
        round_up(convert_volume(vol.value(), &converted_volume_scale), decimal_places);
    if (converted_volume_scale != 0) { f |= iteminfo::is_decimal; }
    return iteminfo(type, left, string_format("<num> %s", volume_units_abbr()), f,
                    converted_volume);
}

auto weight_to_info(const std::string& type, const std::string& left, const units::mass& weight,
                    int /* decimal_places */, bool lower_is_better) -> iteminfo {
    iteminfo::flags f = iteminfo::no_newline;
    if (lower_is_better) { f |= iteminfo::lower_is_better; }
    const double converted_weight = convert_weight(weight);
    f |= iteminfo::is_decimal;
    return iteminfo(type, left, string_format("<num> %s", weight_units()), f, converted_weight);
}
