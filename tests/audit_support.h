#pragma once
#ifndef CATA_TESTS_AUDIT_SUPPORT_H
#define CATA_TESTS_AUDIT_SUPPORT_H

#include <cstddef>
#include <string>
#include <vector>

class avatar;
class item;

/**
 * PL-62 AT5: small helpers shared by the audit sweeps.
 *
 * A sweep writes one row per (item, flow) probe to a TSV under report/ and fails on the rows that
 * are not in the allowlist.  The allowlist is a file, not compiled in, so a data fix batch can
 * shrink it without touching a test (PL-64 DT2).
 */
namespace audit
{

struct row {
    std::string item;
    std::string flow;
    bool ok = true;
    std::string reason;
};

/** One TSV cell: tabs, newlines and carriage returns become spaces. */
std::string tsv_safe( const std::string &value );

/** Like tsv_safe(), also collapses runs of whitespace and truncates to @p limit bytes. */
std::string compact( const std::string &value, std::size_t limit = 240 );

/**
 * Writes @p header and @p rows as TSV to @p path, creating the parent directory.
 * A row with fewer cells than the header is padded with empty cells.
 */
void write_tsv( const std::string &path, const std::vector<std::string> &header,
                const std::vector<std::vector<std::string>> &rows );

/**
 * Reads an allowlist: one key per line, two TAB separated fields item and flow.  Blank lines and
 * lines starting with '#' are ignored.  A missing file is an empty allowlist.
 */
std::vector<std::string> read_keys( const std::string &path );

/** The allowlist key of @p r: item TAB flow. */
std::string key( const row &r );

/** True if @p allowlist (from read_keys) contains the key of @p r. */
bool allows( const row &r, const std::vector<std::string> &allowlist );

/** Joins @p values with @p separator. */
std::string join( const std::vector<std::string> &values, const std::string &separator );

/**
 * PL-63 FM8 scene fixtures.  Each builder leaves the avatar with the items the FM8 scene names
 * and reports the item pointers the test needs to drive the selector (select, collapse, mark).
 * A false return means one item could not be placed where the scene wants it.
 */
/** S1/S3/S4/S5/S6: a worn windbreaker, cargo pants and hiking backpack; the pack holds a small
 *  cardboard box, the box a rock. */
bool build_pack_scene( avatar &u, item *&jacket, item *&pants, item *&pack, item *&box,
                       item *&rock );
/** S10: the AT4 fixture, a worn windbreaker holding a small cardboard box with a 1 kg cube. */
bool build_jacket_box_scene( avatar &u, item *&jacket, item *&box, item *&cube );
/** S7: a carried empty small cardboard box and a carried stack of three rocks. */
bool build_drop_scene( avatar &u, item *&box, item *&rock );
/** S9: a worn US ballistic vest with two ESAPI plates in its large pockets; a loose ESBI plate
 *  (fits a medium pocket) and a loose ESAPI plate (no large pocket left) are carried. */
bool build_vest_scene( avatar &u, item *&vest, item *&medium_plate, item *&large_plate );

} // namespace audit

#endif // CATA_TESTS_AUDIT_SUPPORT_H
