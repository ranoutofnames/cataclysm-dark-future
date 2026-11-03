#pragma once
#ifndef CATA_TESTS_RELOAD_ACTIVITY_ACCESS_H
#define CATA_TESTS_RELOAD_ACTIVITY_ACCESS_H

#include "activity_actor.h"
#include "activity_actor_definitions.h"
#include "player_activity.h"

#include <memory>

// CDF: P3-6-2a test seam. player_activity::actor is private in CDF (DDA keeps it public) and
// the packet may not edit player_activity.h, so the tests reach it through an explicit
// instantiation (the standard-conforming access-check exemption); no production code changes.
namespace reload_activity_access_detail
{
struct actor_tag {
    using type = std::unique_ptr<activity_actor> player_activity::*;
    friend type get_actor_member( actor_tag );
};

template<typename Tag, typename Tag::type Member>
struct member_robber {
    friend typename Tag::type get_actor_member( Tag ) {
        return Member;
    }
};

template struct member_robber<actor_tag, &player_activity::actor>;
} // namespace reload_activity_access_detail

inline activity_actor *activity_actor_of( const player_activity &act )
{
    using namespace reload_activity_access_detail;
    return ( act.*get_actor_member( actor_tag() ) ).get();
}

inline const reload_activity_actor *reload_actor_of( const player_activity &act )
{
    return dynamic_cast<const reload_activity_actor *>( activity_actor_of( act ) );
}

#endif // CATA_TESTS_RELOAD_ACTIVITY_ACCESS_H
