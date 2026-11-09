#include "catch/catch.hpp"

#include "ui.h"

#include <string>

// ZM1 step 1: the list navigation helper shared by the zone manager (and other
// UI lists). The helper handles UP/DOWN/SCROLL_UP/SCROLL_DOWN, PAGE_UP/PAGE_DOWN,
// HOME and END; see src/ui.cpp.

TEST_CASE( "zm1 NAV-T1 navigate_ui_list wraps or clamps", "[uilist]" )
{
    SECTION( "UP at the first entry wraps to the last" ) {
        int val = 0;
        CHECK( navigate_ui_list( "UP", val, 3, 5, true ) );
        CHECK( val == 4 );
    }

    SECTION( "DOWN at the last entry wraps to the first" ) {
        int val = 4;
        CHECK( navigate_ui_list( "DOWN", val, 3, 5, true ) );
        CHECK( val == 0 );
    }

    SECTION( "SCROLL_UP and SCROLL_DOWN wrap the same way" ) {
        int up = 0;
        CHECK( navigate_ui_list( "SCROLL_UP", up, 3, 5, true ) );
        CHECK( up == 4 );

        int down = 4;
        CHECK( navigate_ui_list( "SCROLL_DOWN", down, 3, 5, true ) );
        CHECK( down == 0 );
    }

    SECTION( "without wrap UP and DOWN clamp" ) {
        int top = 0;
        CHECK( navigate_ui_list( "UP", top, 3, 5, false ) );
        CHECK( top == 0 );

        int bottom = 4;
        CHECK( navigate_ui_list( "DOWN", bottom, 3, 5, false ) );
        CHECK( bottom == 4 );
    }
}

TEST_CASE( "zm1 NAV-T2 paging clamps and never wraps", "[uilist]" )
{
    int down = 0;
    CHECK( navigate_ui_list( "PAGE_DOWN", down, 3, 10, false ) );
    CHECK( down == 3 );
    CHECK( navigate_ui_list( "PAGE_DOWN", down, 3, 10, false ) );
    CHECK( down == 6 );
    CHECK( navigate_ui_list( "PAGE_DOWN", down, 3, 10, false ) );
    CHECK( down == 9 ); // clamp at size - 1

    int up = 9;
    CHECK( navigate_ui_list( "PAGE_UP", up, 3, 10, false ) );
    CHECK( up == 6 );
    CHECK( navigate_ui_list( "PAGE_UP", up, 3, 10, false ) );
    CHECK( up == 3 );
    CHECK( navigate_ui_list( "PAGE_UP", up, 3, 10, false ) );
    CHECK( up == 0 );

    int at_end = 9;
    CHECK( navigate_ui_list( "PAGE_DOWN", at_end, 5, 10, true ) );
    CHECK( at_end == 9 ); // a page never wraps, even when wrap is requested
}

TEST_CASE( "zm1 NAV-T3 HOME and END", "[uilist]" )
{
    int home = 3;
    CHECK( navigate_ui_list( "HOME", home, 3, 5, true ) );
    CHECK( home == 0 );

    int end = 0;
    CHECK( navigate_ui_list( "END", end, 3, 5, true ) );
    CHECK( end == 4 );

    int empty_home = 2;
    CHECK( navigate_ui_list( "HOME", empty_home, 3, 0, true ) );
    CHECK( empty_home == 0 );

    int empty_end = 2;
    CHECK( navigate_ui_list( "END", empty_end, 3, 0, true ) );
    CHECK( empty_end == 0 );
}

TEST_CASE( "zm1 NAV-T4 unhandled actions leave the value alone", "[uilist]" )
{
    int val = 3;
    CHECK_FALSE( navigate_ui_list( "CONFIRM", val, 3, 5, true ) );
    CHECK( val == 3 );

    CHECK_FALSE( navigate_ui_list( "LEFT", val, 3, 5, false ) );
    CHECK( val == 3 );
}
