#ifndef OSMIUM_GEOM_FACTORY_HPP
#define OSMIUM_GEOM_FACTORY_HPP

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

#include <stdexcept>
#include <string>
#include <utility>

#include <osmium/geom/coordinates.hpp>
#include <osmium/memory/collection.hpp>
#include <osmium/memory/item.hpp>
#include <osmium/osm/area.hpp>
#include <osmium/osm/item_type.hpp>
#include <osmium/osm/location.hpp>
#include <osmium/osm/node.hpp>
#include <osmium/osm/node_ref.hpp>
#include <osmium/osm/node_ref_list.hpp>
#include <osmium/osm/way.hpp>

namespace osmium {

    struct geometry_error : public std::runtime_error {

        geometry_error(const std::string& what) :
            std::runtime_error(what) {
        }

        geometry_error(const char* what) :
            std::runtime_error(what) {
        }

    }; // struct geometry_error

    /**
     * @brief Everything related to geometry handling.
     */
    namespace geom {

        /**
         * Which nodes of a way to use for a linestring.
         */
        enum class use_nodes : bool {
            unique = true, ///< Remove consecutive nodes with same location.
            all    = false ///< Use all nodes.
        };

        /**
         * Which direction the linestring created from a way
         * should have.
         */
        enum class direction : bool {
            backward = true, ///< Linestring has reverse direction.
            forward  = false ///< Linestring has same direction as way.
        };

        struct IdentityProjection {

            Coordinates operator()(osmium::Location location) const {
                return Coordinates{location.lon(), location.lat()};
            }

        };

        /**
         * Geometry factory.
         */
        template <class TGeomImpl, class TProjection = IdentityProjection>
        class GeometryFactory {

            /**
             * Add all points of an outer or inner ring to a multipolygon.
             */
            void add_points(const osmium::OuterRing& nodes) {
                osmium::Location last_location;
                for (const osmium::NodeRef& node_ref : nodes) {
                    if (last_location != node_ref.location()) {
                        last_location = node_ref.location();
                        m_impl.multipolygon_add_location(m_projection(last_location));
                    }
                }
            }

            TGeomImpl m_impl;
            TProjection m_projection;

        public:

            GeometryFactory<TGeomImpl, TProjection>() = default;

            template <class... TArgs>
            GeometryFactory<TGeomImpl, TProjection>(TArgs&&... args) :
                m_impl(std::forward<TArgs>(args)...) {
            }

            typedef typename TGeomImpl::point_type        point_type;
            typedef typename TGeomImpl::linestring_type   linestring_type;
            typedef typename TGeomImpl::polygon_type      polygon_type;
            typedef typename TGeomImpl::multipolygon_type multipolygon_type;
            typedef typename TGeomImpl::ring_type         ring_type;

            /* Point */

            point_type create_point(const osmium::Location location) const {
                return m_impl.make_point(m_projection(location));
            }

            point_type create_point(const osmium::Node& node) {
                return create_point(node.location());
            }

            point_type create_point(const osmium::NodeRef& node_ref) {
                return create_point(node_ref.location());
            }

            /* LineString */

            template <class TIter>
            void fill_linestring(TIter it, TIter end) {
                for (; it != end; ++it) {
                    m_impl.linestring_add_location(m_projection(it->location()));
                }
            }

            template <class TIter>
            void fill_linestring_unique(TIter it, TIter end) {
                osmium::Location last_location;
                for (; it != end; ++it) {
                    if (last_location != it->location()) {
                        last_location = it->location();
                        m_impl.linestring_add_location(m_projection(last_location));
                    }
                }
            }

            linestring_type create_linestring(const osmium::WayNodeList& wnl, use_nodes un=use_nodes::unique, direction dir=direction::forward) {
                m_impl.linestring_start();

                if (un == use_nodes::unique) {
                    osmium::Location last_location;
                    switch (dir) {
                        case direction::forward:
                            fill_linestring_unique(wnl.cbegin(), wnl.cend());
                            break;
                        case direction::backward:
                            fill_linestring_unique(wnl.crbegin(), wnl.crend());
                            break;
                    }
                } else {
                    switch (dir) {
                        case direction::forward:
                            fill_linestring(wnl.cbegin(), wnl.cend());
                            break;
                        case direction::backward:
                            fill_linestring(wnl.crbegin(), wnl.crend());
                            break;
                    }
                }

                return m_impl.linestring_finish();
            }

            linestring_type create_linestring(const osmium::Way& way, use_nodes un=use_nodes::unique, direction dir=direction::forward) {
                return create_linestring(way.nodes(), un, dir);
            }

            /* MultiPolygon */

            multipolygon_type create_multipolygon(const osmium::Area& area) {
                int num_polygons = 0;
                int num_rings = 0;
                m_impl.multipolygon_start();

                for (auto it = area.cbegin(); it != area.cend(); ++it) {
                    const osmium::OuterRing& ring = static_cast<const osmium::OuterRing&>(*it);
                    if (it->type() == osmium::item_type::outer_ring) {
                        if (num_polygons > 0) {
                            m_impl.multipolygon_polygon_finish();
                        }
                        m_impl.multipolygon_polygon_start();
                        m_impl.multipolygon_outer_ring_start();
                        add_points(ring);
                        m_impl.multipolygon_outer_ring_finish();
                        ++num_rings;
                        ++num_polygons;
                    } else if (it->type() == osmium::item_type::inner_ring) {
                        m_impl.multipolygon_inner_ring_start();
                        add_points(ring);
                        m_impl.multipolygon_inner_ring_finish();
                        ++num_rings;
                    }
                }

                // if there are no rings, this area is invalid
                if (num_rings == 0) {
                    throw geometry_error("invalid area");
                }

                m_impl.multipolygon_polygon_finish();
                return m_impl.multipolygon_finish();
            }

        }; // class GeometryFactory

    } // namespace geom

} // namespace osmium

#endif // OSMIUM_GEOM_FACTORY_HPP
