// DDA source: tests/multimag_test.cpp (DDA commit b4b62254ae2).
// Ported TEST_CASEs: expected_cost_per_use sums effective_qty (DDA:1799);
// CDF-only guard: expected_cost_per_use method string is neutral (Decision 3).

#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "examine_item_menu.h"
#include "flag.h"
#include "item.h"
#include "item_contents.h"
#include "item_pocket.h"
#include "itype.h"
#include "json.h"
#include "map/map.h"
#include "map_helpers.h"
#include "npc.h"
#include "state_helpers.h"
#include "units.h"

#include <sstream>
#include <string>
#include <vector>

static const itype_id itype_glock_19( "glock_19" );
static const itype_id itype_test_multimag_gun_consume( "test_multimag_gun_consume" );

// DDA tests/multimag_test.cpp:1799-1809. The DDA literal 1 for the legacy gun
// is replaced by ammo_required(), because the CDF value is read from data.
TEST_CASE( "expected_cost_per_use_sums_effective_qty", "[p3][firing]" )
{
    item gun( itype_test_multimag_gun_consume );
    CHECK( gun.expected_cost_per_use() == 6 );

    item legacy( itype_glock_19 );
    CHECK( legacy.expected_cost_per_use() == legacy.ammo_required() );
}

// Decision 3: itype::ammo_scale is absent, so method_scale is neutral and any
// method string keeps the same cost.
TEST_CASE( "expected_cost_per_use_method_string_is_neutral", "[p3][firing]" )
{
    item gun( itype_test_multimag_gun_consume );
    CHECK( gun.expected_cost_per_use( "no_such_method" ) == 6 );

    item legacy( itype_glock_19 );
    CHECK( legacy.expected_cost_per_use( "no_such_method" ) == legacy.ammo_required() );
}
