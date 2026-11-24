#pragma once

#include "itype.h"

struct islot_book {
    /**
     * Which skill it upgrades, if any. Can be @ref skill_id::NULL_ID.
     */
    skill_id skill = skill_id::NULL_ID();
    /**
     * Which martial art it teaches.  Can be @ref matype_id::NULL_ID.
     */
    matype_id martial_art = matype_id::NULL_ID();
    /**
     * The skill level the book provides.
     */
    int level = 0;
    /**
     * The skill level required to understand it.
     */
    int req = 0;
    /**
     * How fun reading this is, can be negative.
     */
    int fun = 0;
    /**
     * Intelligence required to read it.
     */
    int intel = 0;
    /**
     * How long in minutes it takes to read.
     * "To read" means getting 1 skill point, not all of them.
     */
    int time = 0;
    /**
     * Fun books have chapters; after all are read, the book is less fun.
     */
    int chapters = 0;

    std::set<book_recipe> recipes;

    // CDF: E1-1, DDA itype.h:655-657
    /**
     * This book counts chapters by item instance instead of by type
     * (i.e. this book represents a generic variety of books, like "book of essays").
     */
    bool generic = false;
    // CDF: E1-1, DDA itype.h:694 (loaded, no DDA code reads it either)
    bool is_scannable = true;
};
