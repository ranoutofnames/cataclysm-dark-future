#if __has_include( "reload/reload_ui.h" )
#error "reload/reload_ui.h is back: P3-6-6b deleted the BN reload menu"
#endif

#include "catch/catch.hpp"

TEST_CASE( "reload_ui_stays_removed", "[reload][removed]" )
{
    // The guard is the #if above; this case makes it visible in the test list.
    CHECK( true );
}
