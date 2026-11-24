// CDF: PC-E1 step E1-11b. The progress of a partial genome sample, "(<specimen> [n/m])", is part of
// the sample file's name. Before E1-11b the progress was shown only for items with the BN flag
// GENOME_DRIVE; E1-10a deleted the flag, so since then a partial sample showed no progress.
// The sample is the e-file digital_genome_sample (E1-11a). Written before the one-line fix.
#include <string>

#include "catch/catch.hpp"
#include "cloning_utils.h"
#include "detached_ptr.h"
#include "item.h"
#include "output.h"
#include "state_helpers.h"
#include "type_id.h"

static const itype_id itype_digital_genome_sample( "digital_genome_sample" );
static const itype_id itype_genome_drive_black_rat( "genome_drive_black_rat" );
static const mtype_id mon_wolf_mutant_huge( "mon_wolf_mutant_huge" );

namespace
{

auto sample_of( const std::string &specimen, int progress ) -> detached_ptr<item>
{
    detached_ptr<item> sample = item::spawn( itype_digital_genome_sample );
    sample->set_var( "specimen_sample", specimen );
    sample->set_var( "specimen_sample_progress", std::to_string( progress ) );
    sample->set_var( "specimen_name", "test specimen" );
    return sample;
}

auto plain_name( const item &it ) -> std::string
{
    return remove_color_tags( it.tname() );
}

} // namespace

TEST_CASE( "efile_genome_a_partial_sample_shows_its_progress_in_the_name", "[efile][genome][tname]" )
{
    clear_all_state();
    const int required = cloning_utils::specimen_required_sample_size( mon_wolf_mutant_huge );
    REQUIRE( required > 2 );

    SECTION( "a partial sample shows n/m" ) {
        const detached_ptr<item> sample = sample_of( "mon_wolf_mutant_huge", 2 );
        const std::string expected = string_format( "[%d/%d]", 2, required );
        CHECK( plain_name( *sample ).find( expected ) != std::string::npos );
    }
    SECTION( "a complete sample shows the specimen only" ) {
        const detached_ptr<item> sample = sample_of( "mon_wolf_mutant_huge", required );
        const std::string name = plain_name( *sample );
        CHECK( name.find( '[' ) == std::string::npos );
        CHECK( name.find( cloning_utils::specimen_name( mon_wolf_mutant_huge, "test specimen" ) ) !=
               std::string::npos );
    }
    SECTION( "an item that is not a sample file shows the specimen without progress" ) {
        // the legacy prefilled drive carries the specimen vars on the drive itself
        item drive( itype_genome_drive_black_rat );
        drive.set_var( "specimen_sample", "mon_wolf_mutant_huge" );
        drive.set_var( "specimen_sample_progress", "1" );
        const std::string name = plain_name( drive );
        CHECK( name.find( '[' ) == std::string::npos );
    }
}
