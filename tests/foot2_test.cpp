// FOOT2: the plain DDA-only foot items (footwear, anklets, toe rings, ankle pouch) on the FOOT1
// foot sub-locations.
// DDA source: b4b62254ae2 data/json/items/armor/{boots,legs_clothes,sheath,swimming,jewelry,
// storage,bespoke_armor/custom_boots}.json. Every number is re-derived from the DDA portions with
// the sub-location shares of DDA body_parts.json:1296-1395 (sole 25, arch 25, toes 10, ankle 20,
// heel 20); nothing is read back from the converted JSON except where a // CDF: note says so.
#include "avatar.h"
#include "bodypart.h"
#include "catch/catch.hpp"
#include "character.h"
#include "item.h"
#include "item_group.h"
#include "item_pocket.h"
#include "itype.h"
#include "material.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "subbodypart.h"
#include "type_id.h"
#include "units.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

bool covers_sub( const std::vector<sub_bodypart_id> &covered, const std::string &id )
{
    const sub_bodypart_str_id wanted( id );
    return std::ranges::any_of( covered, [&wanted]( const sub_bodypart_id & sbp ) {
        return sbp.id() == wanted;
    } );
}

// The sub-locations an armor item names, independent of its coverage (a coverage-0 jewelry
// portion is named but does not "cover" in item::covers(), which asks for coverage > 0).
std::vector<std::string> named_subs( const item &it )
{
    std::vector<std::string> out;
    for( const sub_bodypart_id &sbp : it.get_covered_sub_body_parts() ) {
        out.push_back( sbp.id().str() );
    }
    std::sort( out.begin(), out.end() );
    return out;
}

} // namespace

// G1: the five materials FOOT2 owns.
TEST_CASE( "foot2_materials_load", "[armor][foot2][p3]" )
{
    // parent and numbers: CDF data/json/materials.json (cotton:315, leather:863, hardsteel:616,
    // steel:1238, budget_steel:176). DDA b4b62254ae2 materials.json: denim:691, leather_treated:1423,
    // ch_steel:2041, lc_steel_chain:2119, budget_steel_chain:2097.
    struct mat_expect {
        const char *id;
        const char *parent;
        int bash;
        int cut;
        int bullet;
    };
    const std::vector<mat_expect> mats = {
        { "denim", "cotton", 1, 1, 1 },
        { "leather_treated", "leather", 2, 2, 2 },
        // DDA calls ch_steel "Hardened Carbon Steel", so hardsteel is its CDF parent (rule 7)
        { "ch_steel", "hardsteel", 4, 7, 7 },
        { "lc_steel_chain", "steel", 6, 6, 5 },
        { "budget_steel_chain", "budget_steel", 6, 6, 5 },
    };
    REQUIRE( mats.size() == 5 );
    for( const mat_expect &m : mats ) {
        CAPTURE( m.id );
        REQUIRE( material_id( m.id ).is_valid() );
        // CDF: the CDF parent's numbers are kept (HUB01 ruling 2, no balance judgement)
        CHECK( material_id( m.id )->bash_resist() == m.bash );
        CHECK( material_id( m.id )->cut_resist() == m.cut );
        CHECK( material_id( m.id )->bullet_resist() == m.bullet );
        const material_id parent( m.parent );
        REQUIRE( parent.is_valid() );
        CHECK( material_id( m.id )->bash_resist() == parent->bash_resist() );
        CHECK( material_id( m.id )->cut_resist() == parent->cut_resist() );
        CHECK( material_id( m.id )->bullet_resist() == parent->bullet_resist() );
    }
    // plastic_pad is RIOT1's (CDF materials.json:1955); FOOT2 must not define it a second time
    // (a second definition aborts initialization, so this case cannot even run then).
    CHECK( material_id( "plastic_pad" ).is_valid() );
}

namespace {

// The 23 ported rows. coverage is the DDA aggregate over the foot sub-locations (shares above);
// encumbrance is the sum of the DDA foot portions; thickness is DDA's, truncated because CDF's
// islot_armor::thickness is an int (itype.h:389) - the DDA value is written in the JSON.
struct foot2_row {
    const char *id;
    int coverage;
    int encumbrance;
    int thickness;
};

const std::vector<foot2_row> foot2_table = {
    // boots_denim: upper c100 e12, sole c100 e0; DDA 2.3 = leather 1.5 + denim 0.8
    { "boots_denim", 100, 12, 2 },
    // boots_faux_fur: upper c100 e20, sole c100 e0; DDA item-level 3
    { "boots_faux_fur", 100, 20, 3 },
    // bootsheath_birchbark: ankle c25 e2; DDA 1
    { "bootsheath_birchbark", 5, 2, 1 },
    // chainmail_junk_feet: upper c100 e14, sole c100 e0; DDA 3.2 = leather 2.0 + chain 1.2
    { "chainmail_junk_feet", 100, 14, 3 },
    // espadrilles: toes+heel c100 e4, sole+arch c100 e0 -> 0.30 * 100 + 0.50 * 100; DDA sole 6
    { "espadrilles", 80, 4, 6 },
    // foot_protectors_eod: toes+ankle+heel+arch c100 e34 -> 75; DDA item-level 14
    { "foot_protectors_eod", 75, 34, 14 },
    // gaiter_boot: ankle c100 e0, arch+heel c80 -> 0.20 * 100 + 0.45 * 80; DDA 0.4
    { "gaiter_boot", 56, 0, 0 },
    // gaiter_boot_makeshift: ankle c95 e0, arch+heel c70 e1 (DDA legs_clothes.json:2950-2961) ->
    // coverage 0.20 * 95 + 0.45 * 70 = 50.5 -> 51; the foot encumbrance is the arch+heel 1 (the
    // earlier table row said 0, contradicting this very comment; the JSON has the DDA 1)
    { "gaiter_boot_makeshift", 51, 1, 0 },
    // jikatabi: toes c100 e4, ankle c80, arch+heel c100, sole c100 -> 10 + 16 + 45 + 25; DDA 1.0
    { "jikatabi", 96, 4, 1 },
    // lc_chainmail_feet: upper c100 e14, sole c100 e0; DDA 3.2 = leather 2.0 + chain 1.2
    { "lc_chainmail_feet", 100, 14, 3 },
    // motor_police_boots: sole c100 e15, upper c100 e0 -> encumbrance 15; DDA item-level 2
    { "motor_police_boots", 100, 15, 2 },
    // shoes_denim: toes+arch+heel c100 e5, ankle c60, sole c100 -> 55 + 12 + 25; DDA 1.15
    { "shoes_denim", 92, 5, 1 },
    // sneakers_steel: arch+heel c100 e8, toes c100, ankle c60, sole c100 -> 45 + 10 + 12 + 25
    { "sneakers_steel", 92, 8, 2 },
    // toecaps: toes c100 e10 -> 10; DDA item-level 3
    { "toecaps", 10, 10, 3 },
    // wetsuit_booties_thick: upper c100 e20, sole c100 e0; DDA 5.4 = nylon 0.4 + neoprene 5.0
    { "wetsuit_booties_thick", 100, 20, 5 },
    // ankle_wallet_pouch: ankle c25 e1 -> 5; DDA 0.1
    { "ankle_wallet_pouch", 5, 1, 0 },
    // jewelry: ankle or toes c0 e0, no material_thickness in DDA
    { "bat_anklet", 0, 0, 0 },
    { "flower_anklet", 0, 0, 0 },
    { "gold_anklet", 0, 0, 0 },
    { "silver_anklet", 0, 0, 0 },
    { "woven_anklet", 0, 0, 0 },
    { "gold_toe_ring", 0, 0, 0 },
    { "silver_toe_ring", 0, 0, 0 },
};

// The six rows FOOT2 does not port: their materials belong to packets that have not merged.
// Such a row must be valid exactly when everything it needs is (the step-6 gate).
struct foot2_gated {
    const char *id;
    std::vector<const char *> materials;
    const char *parent;
};

const std::vector<foot2_gated> foot2_gated_rows = {
    { "boots_wsurvivor_nofur", { "leather_treated", "faux_fur", "vinyl", "rubber", "hc_steel" }, nullptr },
    { "gaiter_boot_heavy", { "nylon", "canvas" }, nullptr },
    { "jikatabi_sneakers", { "canvas", "rubber" }, nullptr },
    { "sabaton_metal_sheets", { "lc_steel" }, nullptr },
    { "ch_sabaton_metal_sheets", { "ch_steel" }, "sabaton_metal_sheets" },
    { "lc_sabaton", { "lc_steel" }, nullptr },
};

} // namespace

// G2: the 29-row table: coverage, foot encumbrance, thickness.
TEST_CASE( "foot2_table_numbers", "[armor][foot2][p3]" )
{
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );
    const bodypart_id foot_l( "foot_l" );

    REQUIRE( foot2_table.size() + foot2_gated_rows.size() == 29 );
    for( const foot2_row &row : foot2_table ) {
        CAPTURE( row.id );
        // a missing id must be a clean failure (is_valid() is the CDF type_is_defined),
        // never a null item or null armor slot below
        REQUIRE( itype_id( row.id ).is_valid() );
        REQUIRE( itype_id( row.id )->armor );
        detached_ptr<item> it = item::spawn( itype_id( row.id ) );
        REQUIRE( !it->is_null() );
        CHECK( it->get_coverage( foot_l ) == row.coverage );
        CHECK( it->get_encumber( guy, foot_l ) == row.encumbrance );
        // CDF: islot_armor::thickness is an int, so DDA's 0.4/1.15/2.3/3.2/5.4 truncate
        CHECK( itype_id( row.id )->armor->thickness == row.thickness );
        CHECK( it->has_sub_coverage() );
    }
    // the six group C rows: ported exactly when all their materials (and the copy-from parent)
    // exist, so a later packet that adds one of them turns this red (FOOT2 amendment, rule 8)
    for( const foot2_gated &row : foot2_gated_rows ) {
        CAPTURE( row.id );
        const bool materials_exist = std::ranges::all_of( row.materials,
        []( const char *m ) {
            return material_id( m ).is_valid();
        } );
        const bool parent_exists = row.parent == nullptr || itype_id( row.parent ).is_valid();
        CHECK( itype_id( row.id ).is_valid() == ( materials_exist && parent_exists ) );
    }
}

// G3: what each item covers, and what it leaves open.
TEST_CASE( "foot2_gaps", "[armor][foot2][p3]" )
{
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );
    const bodypart_id foot_l( "foot_l" );

    SECTION( "espadrilles cover the toes and the heel, not the ankle" ) {
        detached_ptr<item> espadrilles = item::spawn( itype_id( "espadrilles" ) );
        CHECK( espadrilles->covers( sub_bodypart_str_id( "foot_toes_l" ) ) );
        CHECK( espadrilles->covers( sub_bodypart_str_id( "foot_heel_l" ) ) );
        CHECK( espadrilles->covers( sub_bodypart_str_id( "foot_arch_l" ) ) );
        CHECK( espadrilles->covers( sub_bodypart_str_id( "foot_sole_l" ) ) );
        CHECK_FALSE( espadrilles->covers( sub_bodypart_str_id( "foot_ankle_l" ) ) );
        CHECK( espadrilles->get_coverage( foot_l, sub_bodypart_str_id( "foot_toes_l" ) ) == 100 );
        CHECK( espadrilles->get_coverage( foot_l, sub_bodypart_str_id( "foot_ankle_l" ) ) == 0 );
        CHECK( espadrilles->get_encumber( guy, foot_l ) == 4 );
    }

    SECTION( "toecaps guard the toes only" ) {
        detached_ptr<item> toecaps = item::spawn( itype_id( "toecaps" ) );
        CHECK( toecaps->covers( sub_bodypart_str_id( "foot_toes_l" ) ) );
        CHECK( toecaps->covers( sub_bodypart_str_id( "foot_toes_r" ) ) );
        CHECK_FALSE( toecaps->covers( sub_bodypart_str_id( "foot_arch_l" ) ) );
        CHECK_FALSE( toecaps->covers( sub_bodypart_str_id( "foot_ankle_l" ) ) );
        CHECK_FALSE( toecaps->covers( sub_bodypart_str_id( "foot_heel_l" ) ) );
        CHECK_FALSE( toecaps->covers( sub_bodypart_str_id( "foot_sole_l" ) ) );
        CHECK( toecaps->get_coverage( foot_l ) == 10 );
    }

    SECTION( "gaiters guard the ankle, arch and heel, not the toes or the sole" ) {
        detached_ptr<item> gaiters = item::spawn( itype_id( "gaiter_boot" ) );
        CHECK( gaiters->covers( sub_bodypart_str_id( "foot_ankle_l" ) ) );
        CHECK( gaiters->covers( sub_bodypart_str_id( "foot_arch_l" ) ) );
        CHECK( gaiters->covers( sub_bodypart_str_id( "foot_heel_l" ) ) );
        CHECK_FALSE( gaiters->covers( sub_bodypart_str_id( "foot_toes_l" ) ) );
        CHECK_FALSE( gaiters->covers( sub_bodypart_str_id( "foot_sole_l" ) ) );
        CHECK( gaiters->get_coverage( foot_l, sub_bodypart_str_id( "foot_ankle_l" ) ) == 100 );
        CHECK( gaiters->get_coverage( foot_l, sub_bodypart_str_id( "foot_arch_l" ) ) == 80 );
    }

    SECTION( "jikatabi cover all five foot sub-locations" ) {
        detached_ptr<item> jikatabi = item::spawn( itype_id( "jikatabi" ) );
        for( const char *sub : { "foot_toes_l", "foot_ankle_l", "foot_arch_l", "foot_heel_l",
                                 "foot_sole_l"
                               } ) {
            CAPTURE( sub );
            CHECK( jikatabi->covers( sub_bodypart_str_id( sub ) ) );
        }
        CHECK( jikatabi->get_coverage( foot_l, sub_bodypart_str_id( "foot_ankle_l" ) ) == 80 );
    }

    SECTION( "a toe ring is worn on the toes of one side and names nothing else" ) {
        // CDF: DDA coverage 0 (NO_WEAR_EFFECT), so item::covers() is false by design; the
        // structural sub-location list is what has to be right
        detached_ptr<item> ring = item::spawn( itype_id( "gold_toe_ring" ) );
        ring->set_side( side::LEFT );
        CHECK( named_subs( *ring ) == std::vector<std::string>{ "foot_toes_l" } );
        ring->set_side( side::RIGHT );
        CHECK( named_subs( *ring ) == std::vector<std::string>{ "foot_toes_r" } );
        CHECK( ring->get_encumber( guy, foot_l ) == 0 );
    }

    SECTION( "an anklet is worn on the ankle of one side and names nothing else" ) {
        detached_ptr<item> anklet = item::spawn( itype_id( "bat_anklet" ) );
        anklet->set_side( side::LEFT );
        CHECK( named_subs( *anklet ) == std::vector<std::string>{ "foot_ankle_l" } );
        anklet->set_side( side::RIGHT );
        CHECK( named_subs( *anklet ) == std::vector<std::string>{ "foot_ankle_r" } );
    }
}

// G4: the leg portions of the gaiters and the police boots.
TEST_CASE( "foot2_multi_part", "[armor][foot2][p3]" )
{
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );
    const bodypart_id foot_l( "foot_l" );
    const bodypart_id leg_l( "leg_l" );
    const sub_bodypart_str_id lower_l( "leg_lower_l" );

    SECTION( "gaiters add the lower legs at 60" ) {
        detached_ptr<item> gaiters = item::spawn( itype_id( "gaiter_boot" ) );
        CHECK( gaiters->get_coverage( leg_l, lower_l ) == 60 );
        CHECK( gaiters->get_encumber( guy, leg_l ) == 1 );
        // the leg portion does not touch the foot aggregate
        CHECK( gaiters->get_coverage( foot_l ) == 56 );
        CHECK( gaiters->get_encumber( guy, foot_l ) == 0 );
    }

    SECTION( "makeshift gaiters add the lower legs at 100" ) {
        detached_ptr<item> makeshift = item::spawn( itype_id( "gaiter_boot_makeshift" ) );
        CHECK( makeshift->get_coverage( leg_l, lower_l ) == 100 );
        CHECK( makeshift->get_encumber( guy, leg_l ) == 2 );
        CHECK( makeshift->get_coverage( foot_l ) == 51 );
    }

    SECTION( "police boots cover the legs at 100 with encumbrance 10" ) {
        detached_ptr<item> police = item::spawn( itype_id( "motor_police_boots" ) );
        CHECK( police->get_coverage( leg_l, lower_l ) == 100 );
        CHECK( police->get_encumber( guy, leg_l ) == 10 );
        CHECK( police->get_coverage( foot_l ) == 100 );
        CHECK( police->get_encumber( guy, foot_l ) == 15 );
    }
}

// G5: the ankle wallet pouch and the birchbark ankle sheath.
TEST_CASE( "foot2_pockets", "[armor][foot2][p3]" )
{
    clear_all_state();
    Character &guy = get_player_character();
    clear_character( *guy.as_player(), true );
    const bodypart_id foot_l( "foot_l" );

    SECTION( "the wallet pouch carries 500 ml / 600 g, at most 9 cm long" ) {
        detached_ptr<item> pouch = item::spawn( itype_id( "ankle_wallet_pouch" ) );
        // CDF: DDA storage.json:1850-1858
        CHECK( pouch->get_storage() == 500_ml );
        int pockets = 0;
        for( const item_pocket &pocket : pouch->contents.get_pockets() ) {
            if( !pocket.get_data().carry ) {
                continue;
            }
            ++pockets;
            CHECK( pocket.get_data().raw_volume_capacity == 500_ml );
            CHECK( pocket.get_data().max_contains_weight == 600_gram );
            CHECK( pocket.get_data().max_item_length == 9_cm );
            // CDF: the earlier premise "270win_jsp (85 mm) fits, 338lapua_hpbt (94 mm) does not" was
            // wrong, not the pocket data (9 cm, 500 ml, 600 g as DDA storage.json:1846-1848): every
            // round is made of powder, a soft material, so item::length() is 0 mm for it (the same
            // as in DDA) and a round never meets max_item_length. The length limit is checked with
            // a rigid 25 ml lighter (derived length 29 mm, fits) and a crowbar (46 cm overlay
            // length, too long: its volume is not checked before the length).
            detached_ptr<item> lighter = item::spawn( itype_id( "lighter" ) );
            REQUIRE( lighter->length() < 9_cm );
            CHECK( pocket.can_contain( *lighter ).success() );
            detached_ptr<item> crowbar = item::spawn( itype_id( "crowbar" ) );
            REQUIRE( crowbar->length() > 9_cm );
            const auto too_long = pocket.can_contain( *crowbar );
            CHECK_FALSE( too_long.success() );
            CHECK( too_long.value() == item_pocket::contain_code::ERR_TOO_BIG );
        }
        CHECK( pockets == 1 );
        pouch->set_side( side::LEFT );
        CHECK( named_subs( *pouch ) == std::vector<std::string>{ "foot_ankle_l" } );
        pouch->set_side( side::RIGHT );
        CHECK( named_subs( *pouch ) == std::vector<std::string>{ "foot_ankle_r" } );
        CHECK( pouch->get_coverage( foot_l ) == 5 );
        CHECK( pouch->get_encumber( guy, foot_l ) == 1 );
    }

    SECTION( "the birchbark sheath holsters a SHEATH_KNIFE only" ) {
        detached_ptr<item> sheath = item::spawn( itype_id( "bootsheath_birchbark" ) );
        int pockets = 0;
        for( const item_pocket &pocket : sheath->contents.get_pockets() ) {
            if( !pocket.get_data().holster ) {
                continue;
            }
            ++pockets;
            CHECK( pocket.get_data().get_flag_restrictions().contains( flag_id( "SHEATH_KNIFE" ) ) );
            CHECK( pocket.get_data().raw_volume_capacity == 500_ml );
            CHECK( pocket.get_data().max_item_length == 30_cm );
            CHECK( pocket.get_data().magazine_well == 200_ml );
            detached_ptr<item> knife = item::spawn( itype_id( "knife_combat" ) );
            CHECK( knife->has_flag( flag_id( "SHEATH_KNIFE" ) ) );
            CHECK( pocket.can_contain( *knife ).success() );
            // CDF: a plain item well inside the 500 ml / 1 kg / 30 cm pocket (hammer: 350 ml,
            // 566 g, 7 cm) is refused by the flag restriction alone
            detached_ptr<item> hammer = item::spawn( itype_id( "hammer" ) );
            CHECK_FALSE( hammer->has_flag( flag_id( "SHEATH_KNIFE" ) ) );
            CHECK_FALSE( pocket.can_contain( *hammer ).success() );
        }
        CHECK( pockets == 1 );
        sheath->set_side( side::LEFT );
        CHECK( named_subs( *sheath ) == std::vector<std::string>{ "foot_ankle_l" } );
        CHECK( sheath->get_coverage( foot_l, sub_bodypart_str_id( "foot_ankle_l" ) ) == 25 );
        CHECK( sheath->get_encumber( guy, foot_l ) == 2 );
    }
}

// G6: the fin mount of the thick diving boots.
TEST_CASE( "foot2_fin_mount", "[armor][foot2][p3]" )
{
    clear_all_state();

    CHECK( flag_id( "BOOT_FINS" ).is_valid() );
    CHECK( flag_id( "BOOT_FINS_CUSTOM" ).is_valid() );
    detached_ptr<item> fins = item::spawn( itype_id( "swim_fins" ) );
    CHECK( fins->has_flag( flag_id( "BOOT_FINS" ) ) );

    detached_ptr<item> boots = item::spawn( itype_id( "wetsuit_booties_thick" ) );
    int fin_slots = 0;
    for( const item_pocket &pocket : boots->contents.get_pockets() ) {
        if( !pocket.get_data().ablative ) {
            continue;
        }
        ++fin_slots;
        CHECK( pocket.get_data().get_flag_restrictions().contains( flag_id( "BOOT_FINS" ) ) );
        // CDF: DDA swimming.json:690-702, an ablative slot of 1300 ml / 1000 g
        CHECK( pocket.get_data().raw_volume_capacity == 1300_ml );
        // swim_fins has BOOT_FINS (DDA swimming.json:740); plain boots do not
        detached_ptr<item> mount_fins = item::spawn( itype_id( "swim_fins" ) );
        CHECK( pocket.can_contain( *mount_fins ).success() );
        detached_ptr<item> plain_boots = item::spawn( itype_id( "boots" ) );
        CHECK_FALSE( plain_boots->has_flag( flag_id( "BOOT_FINS" ) ) );
        CHECK_FALSE( pocket.can_contain( *plain_boots ).success() );
    }
    CHECK( fin_slots == 1 );
}

namespace {

// The 65 rows of foot2-spawn.md whose item FOOT2 ports (the table has 69; the four rows of
// gaiter_boot_heavy and jikatabi_sneakers are held back with those two items).
struct spawn_row {
    const char *group;
    const char *item;
};

const std::vector<spawn_row> foot2_spawn = {
    { "bedroom", "ankle_wallet_pouch" },
    { "clothing_hunting", "ankle_wallet_pouch" },
    { "contraband", "ankle_wallet_pouch" },
    { "dollar_clothes", "ankle_wallet_pouch" },
    { "dresser", "ankle_wallet_pouch" },
    { "everyday_gear", "ankle_wallet_pouch" },
    { "gear_homeless", "ankle_wallet_pouch" },
    { "nightstand", "ankle_wallet_pouch" },
    { "traveler", "ankle_wallet_pouch" },
    { "pawn", "bat_anklet" },
    { "allclothes", "boots_denim" },
    { "clothing_outdoor_shoes", "boots_denim" },
    { "lab_shoes", "boots_denim" },
    { "shoes", "boots_denim" },
    { "shoes_unisex", "boots_denim" },
    { "shoestore_shoes", "boots_denim" },
    { "survivorzed_boots", "boots_denim" },
    { "shoes_unisex", "boots_faux_fur" },
    { "museum_armor_feet", "chainmail_junk_feet" },
    { "bedroom", "espadrilles" },
    { "clutter_bedroom", "espadrilles" },
    { "dress_shoes", "espadrilles" },
    { "livingroom", "espadrilles" },
    { "pawn", "espadrilles" },
    { "shoes", "espadrilles" },
    { "shoes_unisex", "espadrilles" },
    { "shoestore_shoes", "espadrilles" },
    { "swimmer_shoes", "espadrilles" },
    { "pawn", "flower_anklet" },
    { "clothing_hunting", "gaiter_boot" },
    { "dresser", "gaiter_boot" },
    { "survivorzed_extra", "gaiter_boot" },
    { "tools_hunting", "gaiter_boot" },
    { "jewelry_accessories", "gold_anklet" },
    { "allclothes", "shoes_denim" },
    { "clothing_outdoor_shoes", "shoes_denim" },
    { "clothing_prisoner_shoes", "shoes_denim" },
    { "lab_shoes", "shoes_denim" },
    { "shoes", "shoes_denim" },
    { "shoes_unisex", "shoes_denim" },
    { "shoestore_shoes", "shoes_denim" },
    { "jewelry_accessories", "silver_anklet" },
    { "clothing_hunting", "sneakers_steel" },
    { "clothing_work_boots", "sneakers_steel" },
    { "construction_worker", "sneakers_steel" },
    { "fireman_boots", "sneakers_steel" },
    { "hardware_clothing", "sneakers_steel" },
    { "lab_dorm", "sneakers_steel" },
    { "lab_shoes", "sneakers_steel" },
    { "mine_equipment", "sneakers_steel" },
    { "oa_ig_rd_lockers", "sneakers_steel" },
    { "plumbing_clothing", "sneakers_steel" },
    { "shoes_unisex", "sneakers_steel" },
    { "survivorzed_boots", "sneakers_steel" },
    { "toxic_dump_equipment", "sneakers_steel" },
    { "clothing_work_boots", "toecaps" },
    { "construction_worker", "toecaps" },
    { "hardware_clothing", "toecaps" },
    { "lab_shoes", "toecaps" },
    { "plumbing_clothing", "toecaps" },
    { "allclothes", "wetsuit_booties_thick" },
    { "fishing_items", "wetsuit_booties_thick" },
    { "sports", "wetsuit_booties_thick" },
    { "swimmer_shoes", "wetsuit_booties_thick" },
    { "pawn", "woven_anklet" },
};

} // namespace

// G7: the spawn entries.
TEST_CASE( "foot2_spawn", "[armor][foot2][p3]" )
{
    clear_all_state();
    REQUIRE( foot2_spawn.size() == 65 );
    for( const spawn_row &row : foot2_spawn ) {
        CAPTURE( row.group, row.item );
        REQUIRE( itype_id( row.item ).is_valid() );
        CHECK( item_group::group_contains_item( item_group_id( row.group ), itype_id( row.item ) ) );
    }
}
