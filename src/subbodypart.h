#pragma once

#include <string>
#include <vector>

#include "bodypart.h"
#include "translations.h"
#include "type_id.h"

class JsonObject;

/**
 * A sub-location of a body part (upper torso, left shoulder, throat ...).
 *
 * CDF subset of DDA's `sub_body_part_type` (DDA #52468, src/subbodypart.h). The JSON schema
 * and ids are DDA's, so DDA data loads unchanged. Hanging/secondary locations,
 * `locations_under` and `similar_bodypart` are not taken (they belong to the armor-layering
 * system): `secondary` is parsed and must be false.
 */
struct sub_body_part_type {
    sub_bodypart_str_id id;
    bool was_loaded = false;

    translation name;
    translation name_multiple;

    side part_side = side::BOTH;

    /** The body part this location belongs to. */
    bodypart_str_id parent;
    /** The location on the other side of the body (itself when there is none). */
    sub_bodypart_str_id opposite;

    /** Reserved (DDA hanging locations). Must be false for now. */
    bool secondary = false;

    /**
     * Share of the parent body part this location makes up. A body part's primary
     * locations are meant to add up to 100; hits choose a location weighted by it.
     */
    int max_coverage = 0;

    static void load_bp( const JsonObject &jo, const std::string &src );
    void load( const JsonObject &jo, const std::string &src );

    /** Clears all sub body parts. */
    static void reset();
    /** Post-load finalization. */
    static void finalize_all();
    /** Verifies that the sub body parts and the body parts' lists agree. */
    static void check_consistency();

    void finalize();
    void check() const;

    /** All loaded sub body parts. */
    static const std::vector<sub_body_part_type> &get_all();

    // string_id<T> compares like a pointer, so sol2 needs T to compare too (see body_part_type)
    LUA_TYPE_OPS( sub_body_part_type, id );
};
