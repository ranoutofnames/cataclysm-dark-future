// CDF: PC-E1 step E1-10a (cut-over part 1: devices, storage, software items, flags). Data audit of
// the whole item set after the BN digital-media definitions are replaced by the DDA e-file items.
// Written before the data (data/json/items/tool/electronics.json, generic.json, software.json,
// digital_*.json, book/maps.json, flags.json, item_actions.json); DDA data from
// b4b62254ae2:data/json/items/tool/electronics.json, generic.json, software.json.
//
// STATE OF THE TREE: these cases load the core data. Until E1-10b/c fix the itemgroups, recipes,
// mapgen, professions, npcs and the TEST_DATA files that still name the removed ids, the data does
// not load and no case can pass or fail meaningfully; the last agent of E1-10 runs them.
//
// DDA-parity: numbers of the DDA device twins (ememory_max, e_port, etransfer_rate, e_ports_banned,
// the E_FILE_STORAGE pocket shape, the use actions) follow DDA b4b62254ae2. Weight, volume and
// price of the CDF devices stay: AGENT_RULES POCKCLOSE item 1 names those numbers only for
// converted armor. CDF-only readers (control_laptop, wrist computers, afs_atomic_smartphone,
// hard_drive, data_card, genome drives) take the numbers of their closest DDA twin (plan O6 iv, O8).
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "calendar.h"
#include "catch/catch.hpp"
#include "detached_ptr.h"
#include "filesystem.h"
#include "flag.h"
#include "fstream_utils.h"
#include "item.h"
#include "item_contents.h"
#include "item_factory.h"
#include "item_group.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "json.h"
#include "pocket_type.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units.h"
#include "itype_slots/islot_tool.h"

namespace
{

// Flags of the BN digital-media system that no item may carry any more (flags.json:23-40, 191,
// 842-879 before E1-10a).
const std::set<std::string> &removed_flags()
{
    static const std::set<std::string> flags = {
        "DIGITAL", "DIGITAL_STORAGE", "DIGITAL_READER", "DIGITAL_MUSIC", "GENOME_DRIVE",
        "MC_MOBILE", "MC_RANDOM_STUFF", "MC_SCIENCE_STUFF", "MC_USED", "MC_HAS_DATA",
        "MC_MAY_BE_ENCRYPTED", "MC_ENCRYPTED", "MC_TURN_USED"
    };
    return flags;
}

// Item variables of the BN digital reader/storage model (the music, ports and content groups).
const std::set<std::string> &removed_item_vars()
{
    static const std::set<std::string> vars = {
        "digital_content", "digital_reader_ports", "digital_storage_ports"
    };
    return vars;
}

// Item ids that E1-10a deletes (the next agent adds the MIGRATION entries where a map is needed).
const std::vector<const char *> &deleted_item_ids()
{
    static const std::vector<const char *> ids = {
        "software", "software_train_data", "sd_memory_card", "digital_scanned_text",
        "digital_camera_photo_collection", "digital_music_collection"
    };
    return ids;
}

// O7 (owner ruling 2026-11-19): the digital-only twin items stay as loot-only e-files.
const std::vector<const char *> &kept_digital_content_ids()
{
    static const std::vector<const char *> ids = {
        "digital_genome_sample", "digital_cooking_show", "digital_survivor_show",
        "digital_mechanics_show", "digital_adult_video", "digital_urbex_map",
        "digital_evacuation_map", "digital_3d_printing_guide", "digital_nft_artwork",
        "digital_nft_real_estate", "digital_nft_collectible", "digital_home_video",
        "digital_voice_memo", "digital_saved_news", "digital_saved_flier",
        "digital_character_sheet", "digital_radio_archive", "digital_saved_email",
        "digital_hello_world", "digital_corrupted_file", "digital_cooking_recipe_archive",
        "digital_science_recipe_archive", "digital_tailoring_recipe_archive",
        "digital_bartender_drinks_list", "digital_electronic_blueprints",
        "digital_chemical_synthesis_sheet", "digital_workshop_tool_plans",
        "digital_aikido_video", "digital_boxing_video", "digital_eskrima_video",
        "digital_fencing_video", "digital_judo_video", "digital_karate_video",
        "digital_kickboxing_video", "digital_krav_maga_video", "digital_muay_thai_video",
        "digital_taekwondo_video", "digital_blade_blueprints",
        "digital_piercing_weapon_blueprints", "digital_archery_blueprints",
        "digital_firearm_blueprints", "digital_basic_trap_blueprints",
        "digital_mechanical_trap_blueprints", "digital_advanced_trap_blueprints",
        "satellitemap"
    };
    return ids;
}

auto type_of( const std::string &id ) -> const itype *
{
    const itype_id type_id( id );
    return type_id.is_valid() ? &type_id.obj() : nullptr;
}

// Item_factory::find_template makes a runtime placeholder ("undefined-<id>") for every id that a
// group or recipe names without a definition, after which is_valid() is true: a placeholder is no
// definition.
auto is_defined( const std::string &id ) -> bool
{
    const itype *type = type_of( id );
    return type != nullptr && type->nname( 1 ).rfind( "undefined-", 0 ) != 0;
}

// the (single) E_FILE_STORAGE pocket of a type, nullptr when it has none
auto efile_pocket_of( const itype &type ) -> const pocket_data *
{
    for( const pocket_data &pocket : type.pockets ) {
        if( pocket.type == pocket_type::E_FILE_STORAGE ) {
            return &pocket;
        }
    }
    return nullptr;
}

auto efile_pocket_count( const itype &type ) -> int
{
    return static_cast<int>( std::ranges::count_if( type.pockets, []( const pocket_data & pocket ) {
        return pocket.type == pocket_type::E_FILE_STORAGE;
    } ) );
}

auto has_flag_named( const itype &type, const char *name ) -> bool
{
    return std::ranges::any_of( type.get_flags(), [name]( const flag_id & flag ) {
        return flag.str() == name;
    } );
}

auto is_estorable_type( const itype &type ) -> bool
{
    return has_flag_named( type, "E_STORABLE" ) || has_flag_named( type, "E_STORABLE_EXCLUSIVE" );
}

auto max_non_genome_device_memory() -> units::ememory
{
    units::ememory biggest = 0_KB;
    for( const itype *type : item_controller->all() ) {
        const std::string id = type->get_id().str();
        if( id.starts_with( "genome_drive" ) ) {
            continue;
        }
        if( const pocket_data *pocket = efile_pocket_of( *type ) ) {
            biggest = std::max( biggest, pocket->ememory_max );
        }
    }
    return biggest;
}

struct device_numbers {
    const char *id;
    units::ememory ememory_max;
    const char *e_port;
    units::ememory rate;
    std::vector<std::string> banned;
    bool manage_files; // E_FILE_DEVICE and EBOOKSAVE use actions (DDA data: all but the cameras)
    const char *why; // DDA twin, or the CDF-only rule
};

// DDA data/json/items/tool/electronics.json and generic.json (b4b62254ae2); abstract_camera,
// abstract_tablet, abstract_laptop, abstract_smart_phone, abstract_usb_drive and
// abstract_memory_card supply the port, the rate and the banned ports.
const std::vector<device_numbers> &expected_devices()
{
    static const std::vector<device_numbers> devices = {
        { "camera", 32_GB, "camera", 20_MB, {}, false, "DDA camera" },
        { "camera_pro", 64_GB, "camera", 20_MB, {}, false, "DDA camera_pro" },
        { "eink_tablet_pc", 32_GB, "tablet", 30_MB, { "USB-A" }, true, "DDA eink_tablet_pc" },
        { "eink_tablet_pc_on", 32_GB, "tablet", 30_MB, { "USB-A" }, true, "DDA eink_tablet_pc_on" },
        { "laptop", 1_TB, "laptop", 30_MB, {}, true, "DDA laptop" },
        { "laptop_screen_lit", 1_TB, "laptop", 30_MB, {}, true, "DDA laptop_screen_lit" },
        { "smart_phone", 256_GB, "phone", 30_MB, { "USB-A" }, true, "DDA smart_phone" },
        { "smart_phone_flashlight", 256_GB, "phone", 30_MB, { "USB-A" }, true, "DDA smart_phone_flashlight" },
        { "usb_drive", 128_GB, "USB-A", 20_MB, {}, false, "DDA usb_drive" },
        { "usb_drive_older", 32_GB, "USB-A", 20_MB, {}, false, "DDA usb_drive_older" },
        { "usb_drive_nano", 128_GB, "USB-A", 20_MB, {}, false, "DDA usb_drive_nano" },
        { "nano_fabricator_encryption_code", 128_GB, "USB-A", 20_MB, {}, false, "DDA" },
        { "lixa_data", 128_GB, "USB-A", 20_MB, {}, false, "DDA lixa_data" },
        { "portable_hard_drive", 2_TB, "USB-A", 20_MB, {}, false, "DDA portable_hard_drive" },
        { "memory_card", 64_GB, "memory card", 12_MB, {}, false, "DDA memory_card" },
        { "memory_card_older", 16_GB, "memory card", 12_MB, {}, false, "DDA memory_card_older" },
        { "memory_card_unread", 64_GB, "memory card", 12_MB, {}, false, "DDA memory_card_unread" },
        { "memory_card_science", 64_GB, "memory card", 12_MB, {}, false, "DDA memory_card_science" },
        // CDF-only readers: the closest DDA twin (plan O6 iv, O8)
        { "control_laptop", 1_TB, "laptop", 30_MB, {}, true, "CDF-only, sized like laptop" },
        { "control_laptop_screen_lit", 1_TB, "laptop", 30_MB, {}, true, "CDF-only, like laptop" },
        { "wrist_comp_budget", 256_GB, "phone", 30_MB, { "USB-A" }, true, "CDF-only, like smart_phone" },
        { "wrist_comp_control", 256_GB, "phone", 30_MB, { "USB-A" }, true, "CDF-only, like smart_phone" },
        { "afs_atomic_smartphone", 1_TB, "phone", 30_MB, { "USB-A" }, true, "CDF-only, plan O6 iv 1 TB" },
        { "hard_drive", 2_TB, "USB-A", 20_MB, {}, false, "O8: CDF-only, like portable_hard_drive" },
        { "data_card", 1_TB, "data card", 30_MB, {}, false, "CDF-only data card (mission item)" },
    };
    return devices;
}

} // namespace

TEST_CASE( "efile_audit_no_removed_flags_vars_or_uses", "[efile][data][audit]" )
{
    clear_all_state();
    int checked = 0;
    for( const itype *type : item_controller->all() ) {
        INFO( type->get_id().str() );
        for( const flag_id &flag : type->get_flags() ) {
            CAPTURE( flag.str() );
            CHECK( removed_flags().count( flag.str() ) == 0 );
        }
        for( const std::string &var : removed_item_vars() ) {
            CAPTURE( var );
            CHECK_FALSE( type->item_vars.contains( var ) );
        }
        // the BN tablet menu; the DDA uses are E_FILE_DEVICE and EBOOKSAVE
        CHECK( type->get_use( "EINKTABLETPC" ) == nullptr );
        checked++;
    }
    CHECK( checked > 0 );
}

TEST_CASE( "efile_audit_deleted_items_are_gone", "[efile][data][audit]" )
{
    clear_all_state();
    for( const char *id : deleted_item_ids() ) {
        CAPTURE( id );
        CHECK_FALSE( is_defined( id ) );
    }
}

TEST_CASE( "efile_audit_every_efile_pocket_has_memory", "[efile][data][audit]" )
{
    clear_all_state();
    int pockets = 0;
    for( const itype *type : item_controller->all() ) {
        INFO( type->get_id().str() );
        for( const pocket_data &pocket : type->pockets ) {
            if( pocket.type == pocket_type::E_FILE_STORAGE ) {
                CHECK( pocket.ememory_max > 0_KB );
                pockets++;
            }
        }
        CHECK( efile_pocket_count( *type ) <= 1 );
        // DDA: a device that handles files has exactly one place to keep them
        if( type->get_use( "E_FILE_DEVICE" ) != nullptr ) {
            const pocket_data *pocket = efile_pocket_of( *type );
            REQUIRE( pocket != nullptr );
            CHECK( pocket->ememory_max > 0_KB );
        }
    }
    CHECK( pockets > 0 );
}

TEST_CASE( "efile_audit_every_storable_item_has_a_size", "[efile][data][audit]" )
{
    clear_all_state();
    int storable = 0;
    for( const itype *type : item_controller->all() ) {
        if( !is_estorable_type( *type ) ) {
            continue;
        }
        INFO( type->get_id().str() );
        CHECK( type->ememory_size > 0_KB );
        storable++;
    }
    CHECK( storable > 0 );
}

// DDA tests/item_test.cpp:360-396 (check_spawning_in_container, software branch): a default
// container that is not a container is an e-storage device and holds the software as a file.
// Real data: software_utility and software_cryptow (CDF-only, container usb_drive); the DDA
// software ids have no default container (DDA data), E1-3's fixture case covers the other path.
TEST_CASE( "efile_audit_software_in_its_default_container", "[efile][data][audit]" )
{
    clear_all_state();
    int with_container = 0;
    for( const itype *type : item_controller->all() ) {
        if( !type->default_container ) {
            continue;
        }
        const item probe( type->get_id() );
        if( !probe.is_software() ) {
            continue;
        }
        INFO( type->get_id().str() );
        detached_ptr<item> boxed = item::in_its_container( item::spawn( probe ), 1 );
        item &container_item = *boxed;
        CHECK( container_item.typeId() == *type->default_container );
        REQUIRE( container_item.is_estorage() );
        const std::vector<const item *> softwares = container_item.softwares();
        CHECK( !softwares.empty() );
        for( const item *software : softwares ) {
            CHECK( software->typeId() == type->get_id() );
        }
        with_container++;
    }
    CHECK( with_container >= 2 );
}

TEST_CASE( "efile_audit_dda_device_numbers", "[efile][data][audit]" )
{
    clear_all_state();
    for( const device_numbers &expected : expected_devices() ) {
        CAPTURE( expected.id, expected.why );
        const itype *type = type_of( expected.id );
        REQUIRE( type != nullptr );
        const pocket_data *pocket = efile_pocket_of( *type );
        REQUIRE( pocket != nullptr );
        CHECK( efile_pocket_count( *type ) == 1 );
        CHECK( pocket->ememory_max == expected.ememory_max );
        // the DDA pocket shape: files take no room and weigh nothing
        CHECK( pocket->rigid );
        CHECK( pocket->raw_volume_capacity == 1_ml );
        CHECK( pocket->max_contains_weight == 1_gram );
        CHECK( pocket->weight_multiplier == 0.0f );
        REQUIRE( type->tool );
        CHECK( type->tool->e_port == expected.e_port );
        CHECK( type->tool->etransfer_rate == expected.rate );
        CHECK( type->tool->e_ports_banned == expected.banned );
        CHECK( ( type->get_use( "E_FILE_DEVICE" ) != nullptr ) == expected.manage_files );
        CHECK( ( type->get_use( "EBOOKSAVE" ) != nullptr ) == expected.manage_files );
    }
}

// DDA mp3 has no file pocket: the CDF mp3 loses its 1 ml CONTAINER pocket for the files, and the
// music files are gone (D1).
TEST_CASE( "efile_audit_mp3_keeps_no_file_pocket", "[efile][data][audit]" )
{
    clear_all_state();
    for( const char *id : { "mp3", "mp3_on" } ) {
        CAPTURE( id );
        const itype *type = type_of( id );
        REQUIRE( type != nullptr );
        CHECK( efile_pocket_of( *type ) == nullptr );
        CHECK_FALSE( std::ranges::any_of( type->pockets, []( const pocket_data & pocket ) {
            return pocket.type == pocket_type::CONTAINER;
        } ) );
    }
}

// A transform changes the item into its target: the files stay inside, so source and target must
// have the same E_FILE pocket (screen light, flashlight, music: every variant restates the pockets).
TEST_CASE( "efile_audit_transform_targets_keep_the_efile_pocket", "[efile][data][audit]" )
{
    clear_all_state();
    int pairs = 0;
    const auto same_pocket = []( const itype & from, const itype & to ) {
        const pocket_data *a = efile_pocket_of( from );
        const pocket_data *b = efile_pocket_of( to );
        return ( a == nullptr ) == ( b == nullptr ) &&
               ( a == nullptr || a->ememory_max == b->ememory_max );
    };
    for( const itype *type : item_controller->all() ) {
        const bool has_pocket = efile_pocket_of( *type ) != nullptr;
        std::set<std::string> targets;
        for( const auto &[use_name, use] : type->use_methods ) {
            if( const auto *transform = dynamic_cast<const iuse_transform *>( use.get_actor_ptr() ) ) {
                if( !transform->target.is_empty() ) {
                    targets.insert( transform->target.str() );
                }
            }
        }
        if( type->tool && type->tool->revert_to ) {
            targets.insert( type->tool->revert_to->str() );
        }
        for( const std::string &target_id : targets ) {
            const itype *target = type_of( target_id );
            if( target == nullptr ) {
                continue; // checked by the item loader
            }
            if( !has_pocket && efile_pocket_of( *target ) == nullptr ) {
                continue;
            }
            INFO( type->get_id().str() << " -> " << target_id );
            CHECK( same_pocket( *type, *target ) );
            pairs++;
        }
    }
    CHECK( pairs > 0 );
}

// Owner ruling O4: a genome sample ready for cloning is an e-file bigger than the biggest ordinary
// device (DDA portable_hard_drive, 2 TB); only the genome drives, which carry very large pockets,
// can hold one. The drives are not locked to genome data: their pocket is an ordinary E_FILE pocket.
TEST_CASE( "efile_audit_genome_samples_fit_only_the_genome_drives", "[efile][data][audit]" )
{
    clear_all_state();
    const itype *sample = type_of( "digital_genome_sample" );
    REQUIRE( sample != nullptr );
    CHECK( is_estorable_type( *sample ) );
    const units::ememory biggest = max_non_genome_device_memory();
    CHECK( biggest >= 2_TB );
    CHECK( sample->ememory_size > biggest );
    for( const char *id : { "genome_drive", "genome_drive_black_rat", "genome_drive_wolf_mutant_huge" } ) {
        CAPTURE( id );
        const itype *drive = type_of( id );
        REQUIRE( drive != nullptr );
        const pocket_data *pocket = efile_pocket_of( *drive );
        REQUIRE( pocket != nullptr );
        CHECK( pocket->ememory_max >= sample->ememory_size );
        CHECK( pocket->ememory_max > biggest );
    }
    // the sample really fits on a genome drive and on nothing else
    item drive( itype_id( "genome_drive" ) );
    item sample_file( sample->get_id() );
    CHECK( drive.remaining_ememory() >= sample_file.ememory_size() );
    CHECK( drive.put_in( sample_file, pocket_type::E_FILE_STORAGE ).success() );
    // the pocket itself does not count memory (DDA item_pocket.cpp:1367-1372); the e-file actors and
    // the computer download refuse a file bigger than remaining_ememory()
    item big_disk( itype_id( "portable_hard_drive" ) );
    CHECK( big_disk.remaining_ememory() < sample_file.ememory_size() );
}

// Owner ruling O7: the digital-only twin items stay as loot-only e-files, O8: hard_drive stays a
// CDF-only storage item with an E_FILE pocket in the class of portable_hard_drive.
TEST_CASE( "efile_audit_loot_only_files_and_storage_stay", "[efile][data][audit]" )
{
    clear_all_state();
    for( const char *id : kept_digital_content_ids() ) {
        CAPTURE( id );
        const itype *type = type_of( id );
        REQUIRE( type != nullptr );
        CHECK( is_estorable_type( *type ) );
        CHECK( type->ememory_size > 0_KB );
        // loot-only: nothing carries the BN removed flags
        CHECK( removed_flags().count( "DIGITAL" ) == 1 );
        CHECK_FALSE( has_flag_named( *type, "DIGITAL" ) );
    }
    const itype *hard_drive = type_of( "hard_drive" );
    REQUIRE( hard_drive != nullptr );
    const itype *portable = type_of( "portable_hard_drive" );
    REQUIRE( portable != nullptr );
    REQUIRE( efile_pocket_of( *hard_drive ) != nullptr );
    REQUIRE( efile_pocket_of( *portable ) != nullptr );
    CHECK( efile_pocket_of( *hard_drive )->ememory_max == efile_pocket_of( *portable )->ememory_max );
}

// CDF-only files (software_utility, software_cryptow, mind_scan_robofac) are rebased on the DDA
// abstracts and have to fit the devices they come on: the data card of the Robofac mission item.
TEST_CASE( "efile_audit_cdf_only_software_fits_its_container", "[efile][data][audit]" )
{
    clear_all_state();
    for( const char *id : { "software_utility", "software_cryptow", "mind_scan_robofac" } ) {
        CAPTURE( id );
        const itype *type = type_of( id );
        REQUIRE( type != nullptr );
        CHECK( has_flag_named( *type, "E_STORABLE_EXCLUSIVE" ) );
        REQUIRE( type->default_container );
        const itype *container = type_of( type->default_container->str() );
        REQUIRE( container != nullptr );
        const pocket_data *pocket = efile_pocket_of( *container );
        REQUIRE( pocket != nullptr );
        CHECK( type->ememory_size <= pocket->ememory_max );
    }
    // the shared ids are the DDA definitions: category software, estorable exclusive
    for( const char *id : { "software_useless", "software_hacking", "software_medical", "software_math",
                            "software_blood_data", "software_lab_data", "software_electronics_reference",
                            "software_utility", "software_cryptow", "train_data" } ) {
        CAPTURE( id );
        const itype *type = type_of( id );
        REQUIRE( type != nullptr );
        CHECK( has_flag_named( *type, "E_STORABLE_EXCLUSIVE" ) );
        CHECK( item( type->get_id() ).is_software() );
    }
}

// The DDA book_binder takes the CDF generic paper through an item_restriction (the shape of the
// TEST_DATA fixture test_efile_binder; CDF paper stays a generic item, AUTO-371).
TEST_CASE( "efile_audit_book_binder", "[efile][data][audit]" )
{
    clear_all_state();
    const itype *binder = type_of( "book_binder" );
    REQUIRE( binder != nullptr );
    CHECK( binder->get_use( "BINDER_ADD_RECIPE" ) != nullptr );
    CHECK( binder->get_use( "BINDER_MANAGE_RECIPE" ) != nullptr );
    item book_binder( binder->get_id() );
    item sheet( itype_id( "paper" ) );
    CHECK( book_binder.can_contain( sheet ).success() );
    item rock( itype_id( "rock" ) );
    CHECK_FALSE( book_binder.can_contain( rock ).success() );
}

// ---- E1-10b/c: spawn content, ids, migrations (cut-over part 2) ----------------------------------
// Written before the item groups, recipes, migrations and TEST_DATA files were converted.

namespace
{

// Quoted tokens that the cut-over removed from the data: ids, flags, item vars and use actions of
// the BN digital-media system (E1-10a handoff deleted_items, deleted_flags, deleted_item_vars,
// deleted_item_actions).  obsoletion/ may name a deleted id as the source of a MIGRATION entry.
const std::vector<std::string> &removed_tokens()
{
    static const std::vector<std::string> tokens = {
        "sd_memory_card", "software_train_data", "digital_scanned_text",
        "digital_camera_photo_collection", "digital_music_collection", "digital_collection",
        "DIGITAL", "DIGITAL_STORAGE", "DIGITAL_READER", "DIGITAL_MUSIC", "GENOME_DRIVE",
        "MC_MOBILE", "MC_RANDOM_STUFF", "MC_SCIENCE_STUFF", "MC_USED", "MC_HAS_DATA",
        "MC_MAY_BE_ENCRYPTED", "MC_ENCRYPTED", "MC_TURN_USED", "EINKTABLETPC",
        "digital_content", "digital_reader_ports", "digital_storage_ports"
    };
    return tokens;
}

auto core_and_test_data_json_files() -> std::vector<std::string>
{
    std::vector<std::string> files = get_files_from_path( ".json", "data/json", true, true );
    const std::vector<std::string> test_data = get_files_from_path( ".json", "data/mods/TEST_DATA",
            true, true );
    files.insert( files.end(), test_data.begin(), test_data.end() );
    return files;
}

auto is_obsoletion_file( const std::string &path ) -> bool
{
    return path.find( "obsoletion" ) != std::string::npos;
}

// ids of every item_group object of one file
auto item_group_ids_in( const std::string &path ) -> std::vector<std::string>
{
    std::vector<std::string> out;
    read_from_file_json( path, [&]( JsonIn & jsin ) {
        if( !jsin.test_array() ) {
            return;
        }
        JsonArray all = jsin.get_array();
        while( all.has_more() ) {
            JsonObject jo = all.next_object();
            jo.allow_omitted_members();
            if( jo.get_string( "type", "" ) == "item_group" && jo.has_string( "id" ) ) {
                out.push_back( jo.get_string( "id" ) );
            }
        }
    } );
    return out;
}

// a file (DDA item::is_estorable): the E_STORABLE flags, or a book
auto is_file_type( const itype &type ) -> bool
{
    return is_estorable_type( type ) || static_cast<bool>( type.book );
}

auto migrated_is_defined( const std::string &id ) -> bool
{
    return is_defined( item_controller->migrate_id( itype_id( id ) ).str() );
}

} // namespace

TEST_CASE( "efile_audit_no_removed_id_in_core_data_or_test_data", "[efile][data][audit]" )
{
    std::set<std::string> hits;
    int files = 0;
    for( const std::string &path : core_and_test_data_json_files() ) {
        if( is_obsoletion_file( path ) ) {
            continue;
        }
        const std::string text = read_entire_file( path );
        files++;
        for( const std::string &token : removed_tokens() ) {
            if( text.find( "\"" + token + "\"" ) != std::string::npos ) {
                hits.insert( token + " in " + path );
            }
        }
        // the concrete BN `software` base item (the category string "software" stays)
        if( text.find( "\"copy-from\": \"software\"" ) != std::string::npos ) {
            hits.insert( "copy-from software in " + path );
        }
    }
    CHECK( files > 100 );
    for( const std::string &hit : hits ) {
        INFO( hit );
        CHECK( false );
    }
    CHECK( hits.empty() );
}

TEST_CASE( "efile_audit_every_item_group_resolves_to_defined_items", "[efile][data][audit]" )
{
    clear_all_state();
    std::set<std::string> ids;
    for( const std::string &path : core_and_test_data_json_files() ) {
        for( const std::string &id : item_group_ids_in( path ) ) {
            ids.insert( id );
        }
    }
    CHECK( ids.size() > 500 );
    for( const std::string &id : ids ) {
        CAPTURE( id );
        const item_group_id group_id( id );
        REQUIRE( item_group::group_is_defined( group_id ) );
        for( const itype *type : item_group::every_possible_item_from( group_id ) ) {
            CAPTURE( type->get_id().str() );
            CHECK( migrated_is_defined( type->get_id().str() ) );
        }
    }
}

TEST_CASE( "efile_audit_migrations_point_at_defined_items", "[efile][data][audit]" )
{
    clear_all_state();
    // old id, replacement (O8: the SD card becomes the DDA memory card; E1-4 added train_data)
    const std::vector<std::pair<std::string, std::string>> expected = {
        { "sd_memory_card", "memory_card" },
        { "software_train_data", "train_data" },
        { "mobile_memory_card", "memory_card" },
        { "mobile_memory_card_used", "memory_card" },
        { "mobile_memory_card_data", "memory_card" },
        { "mobile_memory_card_encrypted", "memory_card" },
        { "mobile_memory_card_science", "memory_card_science" },
        { "digital_genome_sample_black_rat", "digital_genome_sample" },
        { "digital_genome_sample_wolf_mutant_huge", "digital_genome_sample" },
    };
    for( const auto &[old_id, replacement] : expected ) {
        CAPTURE( old_id, replacement );
        const itype_id migrated = item_controller->migrate_id( itype_id( old_id ) );
        CHECK( migrated.str() == replacement );
        CHECK( is_defined( migrated.str() ) );
    }
}

// The rewritten loot groups: the storage media of the old efile_storage_media groups carry their
// files in the E_FILE pocket (contents-group), no estorable item spawns loose from them, and the
// terminal content, the genome sample and the book_manual_combat move are in the DDA shape.
TEST_CASE( "efile_audit_rewritten_loot_groups_fill_the_file_pockets", "[efile][data][audit]" )
{
    clear_all_state();
    SECTION( "efile_storage_media spawns usb drives and memory cards with files inside" ) {
        int with_files = 0;
        for( int i = 0; i < 400; ++i ) {
            for( detached_ptr<item> &spawned : item_group::items_from( item_group_id( "efile_storage_media" ),
                    calendar::turn ) ) {
                CHECK( spawned->is_estorage() );
                CHECK_FALSE( spawned->is_estorable() );
                if( !spawned->efiles().empty() ) {
                    with_files++;
                }
                for( const item *file : spawned->efiles() ) {
                    CHECK( file->is_estorable() );
                }
            }
        }
        CHECK( with_files > 0 );
    }
    SECTION( "digital_content_terminal is only files" ) {
        const item_group_id terminal( "digital_content_terminal" );
        REQUIRE( item_group::group_is_defined( terminal ) );
        for( const itype *type : item_group::every_possible_item_from( terminal ) ) {
            CAPTURE( type->get_id().str() );
            CHECK( is_file_type( *type ) );
        }
    }
    SECTION( "a genome sample group spawns a genome drive holding the sample" ) {
        const item_group_id group( "digital_genome_sample_black_rat" );
        REQUIRE( item_group::group_is_defined( group ) );
        const std::vector<detached_ptr<item>> spawned = item_group::items_from( group, calendar::turn );
        REQUIRE( spawned.size() == 1 );
        CHECK( spawned.front()->typeId().str() == "genome_drive" );
        const item &drive = *spawned.front();
        const std::vector<const item *> files = drive.efiles();
        REQUIRE( files.size() == 1 );
        CHECK( files.front()->typeId().str() == "digital_genome_sample" );
        CHECK( files.front()->get_var( "specimen_sample" ) == "mon_black_rat" );
    }
    SECTION( "the book_manual_combat group lives with the books" ) {
        CHECK( item_group::group_is_defined( item_group_id( "book_manual_combat" ) ) );
    }
}
