#ifndef OSMIUM_OSM_ITEM_TYPE_HPP
#define OSMIUM_OSM_ITEM_TYPE_HPP

/*

This file is part of Osmium (http://osmcode.org/libosmium).

Copyright 2013,2014 Jochen Topf <jochen@topf.org> and others (see README).

Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

#include <cstdint> // IWYU pragma: keep
#include <iosfwd>
#include <stdexcept>

namespace osmium {

    enum class item_type : uint16_t {

        undefined                              = 0x00,
        node                                   = 0x01,
        way                                    = 0x02,
        relation                               = 0x03,
        area                                   = 0x04,
        changeset                              = 0x05,
        tag_list                               = 0x11,
        way_node_list                          = 0x12,
        relation_member_list                   = 0x13,
        relation_member_list_with_full_members = 0x23,
        outer_ring                             = 0x40,
        inner_ring                             = 0x41,

    }; // enum class item_type

    inline item_type char_to_item_type(const char c) {
        switch (c) {
            case 'X':
                return item_type::undefined;
            case 'n':
                return item_type::node;
            case 'w':
                return item_type::way;
            case 'r':
                return item_type::relation;
            case 'a':
                return item_type::area;
            case 'c':
                return item_type::changeset;
            case 'T':
                return item_type::tag_list;
            case 'N':
                return item_type::way_node_list;
            case 'M':
                return item_type::relation_member_list;
            case 'F':
                return item_type::relation_member_list_with_full_members;
            case 'O':
                return item_type::outer_ring;
            case 'I':
                return item_type::inner_ring;
            default:
                return item_type::undefined;
        }
        return item_type::undefined; // to silence the warning
    }

    inline char item_type_to_char(const item_type type) {
        switch (type) {
            case item_type::undefined:
                return 'X';
            case item_type::node:
                return 'n';
            case item_type::way:
                return 'w';
            case item_type::relation:
                return 'r';
            case item_type::area:
                return 'a';
            case item_type::changeset:
                return 'c';
            case item_type::tag_list:
                return 'T';
            case item_type::way_node_list:
                return 'N';
            case item_type::relation_member_list:
                return 'M';
            case item_type::relation_member_list_with_full_members:
                return 'F';
            case item_type::outer_ring:
                return 'O';
            case item_type::inner_ring:
                return 'I';
        }
        return 'X'; // to silence the warning
    }

    inline const char* item_type_to_name(const item_type type) {
        switch (type) {
            case item_type::undefined:
                return "undefined";
            case item_type::node:
                return "node";
            case item_type::way:
                return "way";
            case item_type::relation:
                return "relation";
            case item_type::area:
                return "area";
            case item_type::changeset:
                return "changeset";
            case item_type::tag_list:
                return "tag_list";
            case item_type::way_node_list:
                return "way_node_list";
            case item_type::relation_member_list:
                return "relation_member_list";
            case item_type::relation_member_list_with_full_members:
                return "relation_member_list_with_full_members";
            case item_type::outer_ring:
                return "outer_ring";
            case item_type::inner_ring:
                return "inner_ring";
        }
        return "undefined"; // to silence the warning
    }

    template <typename TChar, typename TTraits>
    inline std::basic_ostream<TChar, TTraits>& operator<<(std::basic_ostream<TChar, TTraits>& out, const item_type item_type) {
        return out << item_type_to_char(item_type);
    }

    struct unknown_type : public std::runtime_error {

        unknown_type() :
            std::runtime_error("unknown item type") {
        }

    }; // struct unknown_type

} // namespace osmium

#endif // OSMIUM_OSM_ITEM_TYPE_HPP
