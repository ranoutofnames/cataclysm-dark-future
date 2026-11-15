#pragma once
#ifndef CATA_TESTS_AUDIT_SUPPORT_H
#define CATA_TESTS_AUDIT_SUPPORT_H

#include <cstddef>
#include <string>
#include <vector>

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

} // namespace audit

#endif // CATA_TESTS_AUDIT_SUPPORT_H
