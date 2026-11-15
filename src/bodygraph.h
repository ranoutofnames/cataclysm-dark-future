#pragma once
#ifndef CATA_SRC_BODYGRAPH_H
#define CATA_SRC_BODYGRAPH_H

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "color.h"
#include "type_id.h"

class Character;
class JsonObject;
class item;
class iteminfo_query;
struct iteminfo;

// CDF: DDA declares bodygraph_id in type_id.h, which is not part of this port's file set.
struct bodygraph;
using bodygraph_id = string_id<bodygraph>;

struct bodygraph_part {
    // Which main bodyparts are represented in this graph section
    std::vector<bodypart_id> bodyparts;
    // Which sub bodyparts are represented in this graph section
    std::vector<sub_bodypart_id> sub_bodyparts;
    // If selected, which graph gets shown next?
    bodygraph_id nested_graph;
    // What color to use when this graph section is selected
    nc_color sel_color;
    // What symbol to use for fragments that belong to this section
    std::string sym;
};

struct bodygraph {
    bodygraph_id id;
    std::optional<bodypart_id> parent_bp;
    std::optional<bodygraph_id> mirror;
    std::vector<std::vector<std::string>> rows;
    std::vector<std::vector<std::string>> fill_rows;
    std::map<std::string, bodygraph_part> parts;
    std::string label_fill;
    std::string fill_sym;
    nc_color fill_color = c_white;
    bool was_loaded = false;

    static void load_bodygraphs( const JsonObject &jo, const std::string &src );
    static void finalize_all();
    static void check_all();
    static void reset();
    static const std::vector<bodygraph> &get_all();
    void load( const JsonObject &jo, const std::string &src );
    void finalize();
    void check() const;
};

using bodygraph_callback =
    std::function<std::string( const bodygraph_part *, std::string )>;

/**
 * @brief Returns a prepared bodygraph ready to be rendered line-by-line.
 *
 * The callback function is for processing a bodygraph symbol. The args are as follows:
 *
 * @param bodygraph_part points to the graph part represented in the symbol (or nullptr if none)
 * @param string the default symbol to use if no other processing is done
 * @returns the final colorized symbol
 */
std::vector<std::string> get_bodygraph_lines( const Character &u,
        const bodygraph_callback &fragment_cb, const bodygraph_id &id = bodygraph_id::NULL_ID(),
        int width = 0, int height = 0, std::string_view label = "" );

/**
 * CDF: item info armor coverage panel (DDA item_info.cpp:2432, the show_bodygraph block of
 * item::armor_info).  Implemented in iteminfo_bodygraph.cpp and called from item::info().
 */
void item_bodygraph_info( const item &it, std::vector<iteminfo> &info,
                          const iteminfo_query &parts );

#endif // CATA_SRC_BODYGRAPH_H
