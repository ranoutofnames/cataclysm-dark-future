#pragma once
#ifndef CATA_SRC_DROP_PREVIEW_H
#define CATA_SRC_DROP_PREVIEW_H

#include "carried_stacks.h"
#include "character.h"
#include "item.h"
#include "pickup_token.h"

/// Keep the individual drop targets as well as counts for inventory stack rows.
struct drop_preview {
    drop_locations items;
    excluded_stacks counts;
};

inline auto make_drop_preview( Character &owner, const drop_locations &selected ) -> drop_preview
{
    auto result = drop_preview{};
    if( selected.empty() ) {
        return result;
    }
    for( const auto &entry : pickup::reorder_for_dropping( owner, selected ) ) {
        auto *target = &*entry.loc;
        result.items.emplace_back( *target, entry.count );
        if( !owner.is_worn( *target ) && !owner.is_wielding( *target ) ) {
            target = carried_stack_of( owner, target ).front();
        }
        result.counts[target] += entry.count;
    }
    return result;
}

#endif // CATA_SRC_DROP_PREVIEW_H
