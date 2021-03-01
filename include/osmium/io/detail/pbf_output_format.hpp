#ifndef OSMIUM_IO_DETAIL_PBF_OUTPUT_FORMAT_HPP
#define OSMIUM_IO_DETAIL_PBF_OUTPUT_FORMAT_HPP

/*

This file is part of Osmium (https://osmcode.org/libosmium).

Copyright 2013-2021 Jochen Topf <jochen@topf.org> and others (see README).

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

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <osmium/handler.hpp>
#include <osmium/io/detail/output_format.hpp>
#include <osmium/io/detail/pbf.hpp> // IWYU pragma: export
#include <osmium/io/detail/protobuf_tags.hpp>
#include <osmium/io/detail/queue_util.hpp>
#include <osmium/io/detail/string_table.hpp>
#include <osmium/io/detail/zlib.hpp>
#include <osmium/io/file.hpp>
#include <osmium/io/file_format.hpp>
#include <osmium/io/header.hpp>
#include <osmium/memory/buffer.hpp>
#include <osmium/memory/item_iterator.hpp>
#include <osmium/osm/box.hpp>
#include <osmium/osm/item_type.hpp>
#include <osmium/osm/location.hpp>
#include <osmium/osm/metadata_options.hpp>
#include <osmium/osm/node.hpp>
#include <osmium/osm/node_ref.hpp>
#include <osmium/osm/object.hpp>
#include <osmium/osm/relation.hpp>
#include <osmium/osm/tag.hpp>
#include <osmium/osm/timestamp.hpp>
#include <osmium/osm/types.hpp>
#include <osmium/osm/way.hpp>
#include <osmium/thread/pool.hpp>
#include <osmium/util/delta.hpp>
#include <osmium/util/misc.hpp>
#include <osmium/visitor.hpp>

#ifdef OSMIUM_WITH_LZ4
# include <osmium/io/detail/lz4.hpp>
#endif

#include <protozero/pbf_builder.hpp>
#include <protozero/pbf_writer.hpp>
#include <protozero/types.hpp>

namespace osmium {

    namespace io {

        namespace detail {

            struct pbf_output_options {

                /// Which metadata of objects should be added?
                osmium::metadata_options add_metadata;

                /// Compression level used for compression
                int compression_level = 0;

                /**
                 * Which compression (if any) should be used to compress the
                 * PBF blobs?
                 */
                pbf_compression use_compression = pbf_compression::zlib;

                /// Should nodes be encoded in DenseNodes?
                bool use_dense_nodes = true;

                /// Add the "HistoricalInformation" header flag.
                bool add_historical_information_flag = false;

                /// Should the visible flag be added to all OSM objects?
                bool add_visible_flag = false;

                /// Should node locations be added to ways?
                bool locations_on_ways = false;

            }; // struct pbf_output_options

            /**
             * Maximum number of items in a primitive block.
             *
             * The uncompressed length of a Blob *should* be less
             * than 16 megabytes and *must* be less than 32 megabytes.
             *
             * A block may contain any number of entities, as long as
             * the size limits for the surrounding blob are obeyed.
             * However, for simplicity, the current Osmosis (0.38)
             * as well as Osmium implementation always
             * uses at most 8k entities in a block.
             */
            enum {
                max_entities_per_block = 8000
            };

            enum {
                location_granularity = 100
            };

            /**
             * convert a double lat or lon value to an int, respecting the granularity
             */
            inline int64_t lonlat2int(double lonlat) {
                return static_cast<int64_t>(std::round(lonlat * lonlat_resolution / location_granularity));
            }

            enum class pbf_blob_type {
                header = 0,
                data = 1
            };

            /**
             * Contains the code to pack any number of nodes into a DenseNode
             * structure.
             */
            class DenseNodes {

                std::vector<int64_t> m_ids;

                std::vector<int32_t> m_versions;
                std::vector<int64_t> m_timestamps;
                std::vector<int64_t> m_changesets;
                std::vector<int32_t> m_uids;
                std::vector<int32_t> m_user_sids;
                std::vector<bool> m_visibles;

                std::vector<int64_t> m_lats;
                std::vector<int64_t> m_lons;
                std::vector<int32_t> m_tags;

                StringTable* m_stringtable;
                const pbf_output_options* m_options;

                osmium::DeltaEncode<object_id_type, int64_t> m_delta_id;

                osmium::DeltaEncode<uint32_t, int64_t> m_delta_timestamp;
                osmium::DeltaEncode<changeset_id_type, int64_t> m_delta_changeset;
                osmium::DeltaEncode<user_id_type, int32_t> m_delta_uid;
                osmium::DeltaEncode<int32_t, int32_t> m_delta_user_sid;

                osmium::DeltaEncode<int64_t, int64_t> m_delta_lat;
                osmium::DeltaEncode<int64_t, int64_t> m_delta_lon;

            public:

                DenseNodes(StringTable* stringtable, const pbf_output_options* options) :
                    m_stringtable(stringtable),
                    m_options(options) {
                }

                std::size_t size() const noexcept {
                    return m_ids.size() * 3 * sizeof(int64_t);
                }

                void add_node(const osmium::Node& node) {
                    m_ids.push_back(m_delta_id.update(node.id()));

                    if (m_options->add_metadata.version()) {
                        assert(node.version() <= static_cast<std::size_t>(std::numeric_limits<int32_t>::max()));
                        m_versions.push_back(static_cast<int32_t>(node.version()));
                    }
                    if (m_options->add_metadata.timestamp()) {
                        m_timestamps.push_back(m_delta_timestamp.update(uint32_t(node.timestamp())));
                    }
                    if (m_options->add_metadata.changeset()) {
                        m_changesets.push_back(m_delta_changeset.update(node.changeset()));
                    }
                    if (m_options->add_metadata.uid()) {
                        m_uids.push_back(m_delta_uid.update(node.uid()));
                    }
                    if (m_options->add_metadata.user()) {
                        m_user_sids.push_back(m_delta_user_sid.update(m_stringtable->add(node.user())));
                    }
                    if (m_options->add_visible_flag) {
                        m_visibles.push_back(node.visible());
                    }

                    m_lats.push_back(m_delta_lat.update(lonlat2int(node.location().lat_without_check())));
                    m_lons.push_back(m_delta_lon.update(lonlat2int(node.location().lon_without_check())));

                    for (const auto& tag : node.tags()) {
                        m_tags.push_back(m_stringtable->add(tag.key()));
                        m_tags.push_back(m_stringtable->add(tag.value()));
                    }
                    m_tags.push_back(0);
                }

                std::string serialize() const {
                    std::string data;
                    protozero::pbf_builder<OSMFormat::DenseNodes> pbf_dense_nodes{data};

                    pbf_dense_nodes.add_packed_sint64(OSMFormat::DenseNodes::packed_sint64_id, m_ids.cbegin(), m_ids.cend());

                    if (m_options->add_metadata.any() || m_options->add_visible_flag) {
                        protozero::pbf_builder<OSMFormat::DenseInfo> pbf_dense_info{pbf_dense_nodes, OSMFormat::DenseNodes::optional_DenseInfo_denseinfo};
                        if (m_options->add_metadata.version()) {
                            pbf_dense_info.add_packed_int32(OSMFormat::DenseInfo::packed_int32_version, m_versions.cbegin(), m_versions.cend());
                        }
                        if (m_options->add_metadata.timestamp()) {
                            pbf_dense_info.add_packed_sint64(OSMFormat::DenseInfo::packed_sint64_timestamp, m_timestamps.cbegin(), m_timestamps.cend());
                        }
                        if (m_options->add_metadata.changeset()) {
                            pbf_dense_info.add_packed_sint64(OSMFormat::DenseInfo::packed_sint64_changeset, m_changesets.cbegin(), m_changesets.cend());
                        }
                        if (m_options->add_metadata.uid()) {
                            pbf_dense_info.add_packed_sint32(OSMFormat::DenseInfo::packed_sint32_uid, m_uids.cbegin(), m_uids.cend());
                        }
                        if (m_options->add_metadata.user()) {
                            pbf_dense_info.add_packed_sint32(OSMFormat::DenseInfo::packed_sint32_user_sid, m_user_sids.cbegin(), m_user_sids.cend());
                        }
                        if (m_options->add_visible_flag) {
                            pbf_dense_info.add_packed_bool(OSMFormat::DenseInfo::packed_bool_visible, m_visibles.cbegin(), m_visibles.cend());
                        }
                    }

                    pbf_dense_nodes.add_packed_sint64(OSMFormat::DenseNodes::packed_sint64_lat, m_lats.cbegin(), m_lats.cend());
                    pbf_dense_nodes.add_packed_sint64(OSMFormat::DenseNodes::packed_sint64_lon, m_lons.cbegin(), m_lons.cend());

                    pbf_dense_nodes.add_packed_int32(OSMFormat::DenseNodes::packed_int32_keys_vals, m_tags.cbegin(), m_tags.cend());

                    return data;
                }

            }; // class DenseNodes

            class PrimitiveBlock {

                std::string m_pbf_primitive_group_data;
                protozero::pbf_builder<OSMFormat::PrimitiveGroup> m_pbf_primitive_group;
                StringTable m_stringtable;
                std::unique_ptr<DenseNodes> m_dense_nodes{};
                const pbf_output_options* m_options;
                OSMFormat::PrimitiveGroup m_type = OSMFormat::PrimitiveGroup::unknown;
                int m_count = 0;

            public:

                explicit PrimitiveBlock(const pbf_output_options& options) :
                    m_pbf_primitive_group(m_pbf_primitive_group_data),
                    m_options(&options) {
                }

                const std::string& group_data() {
                    if (type() == OSMFormat::PrimitiveGroup::optional_DenseNodes_dense) {
                        assert(m_dense_nodes);
                        m_pbf_primitive_group.add_message(OSMFormat::PrimitiveGroup::optional_DenseNodes_dense, m_dense_nodes->serialize());
                    }
                    return m_pbf_primitive_group_data;
                }

                void reset(OSMFormat::PrimitiveGroup type) {
                    m_pbf_primitive_group_data.clear();
                    m_stringtable.clear();
                    m_dense_nodes.reset();
                    m_type = type;
                    m_count = 0;
                }

                void write_stringtable(protozero::pbf_builder<OSMFormat::StringTable>& pbf_string_table) {
                    for (const char* s : m_stringtable) {
                        pbf_string_table.add_bytes(OSMFormat::StringTable::repeated_bytes_s, s);
                    }
                }

                protozero::pbf_builder<OSMFormat::PrimitiveGroup>& group() noexcept {
                    ++m_count;
                    return m_pbf_primitive_group;
                }

                void add_dense_node(const osmium::Node& node) {
                    if (!m_dense_nodes) {
                        m_dense_nodes.reset(new DenseNodes{&m_stringtable, m_options});
                    }
                    m_dense_nodes->add_node(node);
                    ++m_count;
                }

                // There are two functions store_in_stringtable(_unsigned)
                // here because of an inconsistency in the OSMPBF format
                // specification. Both uint32 and sint32 types are used in
                // the format for essentially the same thing.

                int32_t store_in_stringtable(const char* s) {
                    return m_stringtable.add(s);
                }

                uint32_t store_in_stringtable_unsigned(const char* s) {
                    // static_cast okay, because result of add is always >= 0
                    return static_cast<uint32_t>(m_stringtable.add(s));
                }

                int count() const noexcept {
                    return m_count;
                }

                OSMFormat::PrimitiveGroup type() const noexcept {
                    return m_type;
                }

                std::size_t size() const noexcept {
                    return m_pbf_primitive_group_data.size() +
                           m_stringtable.size() +
                           (m_dense_nodes ? m_dense_nodes->size() : 0);
                }

                /**
                 * The output buffer (block) will be filled to about
                 * 95% and then written to disk. This leaves more than
                 * enough space for the string table (which typically
                 * needs about 0.1 to 0.3% of the block size).
                 */
                enum {
                    max_used_blob_size = max_uncompressed_blob_size * 95U / 100U
                };

                bool can_add(OSMFormat::PrimitiveGroup type) const noexcept {
                    if (type != m_type) {
                        return false;
                    }
                    if (count() >= max_entities_per_block) {
                        return false;
                    }
                    return size() < max_used_blob_size;
                }

            }; // class PrimitiveBlock

            class SerializeBlob {

                std::string m_msg;

                int m_compression_level;

                pbf_blob_type m_blob_type;

                pbf_compression m_use_compression;

            public:

                /**
                 * Initialize a blob serializer.
                 *
                 * @param msg Protobuf-message containing the blob data.
                 * @param type Type of blob.
                 * @param use_compression The type of compression to use.
                 * @param compression_level Compression level.
                 */
                SerializeBlob(std::string&& msg, pbf_blob_type type, pbf_compression use_compression, int compression_level) :
                    m_msg(std::move(msg)),
                    m_compression_level(compression_level),
                    m_blob_type(type),
                    m_use_compression(use_compression) {
                }

                /**
                 * Serialize a protobuf message into a Blob, optionally apply
                 * compression and return it together with a BlobHeader ready
                 * to be written to a file.
                 */
                std::string operator()() {
                    assert(m_msg.size() <= max_uncompressed_blob_size);

                    std::string blob_data;
                    protozero::pbf_builder<FileFormat::Blob> pbf_blob{blob_data};

                    switch (m_use_compression) {
                        case pbf_compression::none:
                            pbf_blob.add_bytes(FileFormat::Blob::optional_bytes_raw, m_msg);
                            break;
                        case pbf_compression::zlib:
                            pbf_blob.add_int32(FileFormat::Blob::optional_int32_raw_size, int32_t(m_msg.size()));
                            pbf_blob.add_bytes(FileFormat::Blob::optional_bytes_zlib_data, osmium::io::detail::zlib_compress(m_msg, m_compression_level));
                            break;
                        case pbf_compression::lz4:
#ifdef OSMIUM_WITH_LZ4
                            pbf_blob.add_int32(FileFormat::Blob::optional_int32_raw_size, int32_t(m_msg.size()));
                            pbf_blob.add_bytes(FileFormat::Blob::optional_bytes_lz4_data, osmium::io::detail::lz4_compress(m_msg, m_compression_level));
                            break;
#else
                            throw osmium::pbf_error{"lz4 blobs not supported"};
#endif
                    }

                    std::string blob_header_data;
                    protozero::pbf_builder<FileFormat::BlobHeader> pbf_blob_header{blob_header_data};

                    pbf_blob_header.add_string(FileFormat::BlobHeader::required_string_type, m_blob_type == pbf_blob_type::data ? "OSMData" : "OSMHeader");

                    // The static_cast is okay, because the size can never
                    // be much larger than max_uncompressed_blob_size. This
                    // is due to the assert above and the fact that the zlib
                    // library will not grow deflated data beyond the original
                    // data plus a few header bytes (https://zlib.net/zlib_tech.html).
                    pbf_blob_header.add_int32(FileFormat::BlobHeader::required_int32_datasize, static_cast<int32_t>(blob_data.size()));

                    const auto size = static_cast<uint32_t>(blob_header_data.size());

                    // write to output: the 4-byte BlobHeader size in network
                    // byte order followed by the BlobHeader followed by the Blob
                    std::string output;
                    output.reserve(4 + blob_header_data.size() + blob_data.size());
                    output += static_cast<char>((size >> 24U) & 0xffU);
                    output += static_cast<char>((size >> 16U) & 0xffU);
                    output += static_cast<char>((size >>  8U) & 0xffU);
                    output += static_cast<char>( size         & 0xffU);
                    output.append(blob_header_data);
                    output.append(blob_data);

                    return output;
                }

            }; // class SerializeBlob

            class PBFOutputFormat : public osmium::io::detail::OutputFormat, public osmium::handler::Handler {

                pbf_output_options m_options;

                PrimitiveBlock m_primitive_block;

                void store_primitive_block() {
                    if (m_primitive_block.count() == 0) {
                        return;
                    }

                    std::string primitive_block_data;
                    protozero::pbf_builder<OSMFormat::PrimitiveBlock> primitive_block{primitive_block_data};

                    {
                        protozero::pbf_builder<OSMFormat::StringTable> pbf_string_table{primitive_block, OSMFormat::PrimitiveBlock::required_StringTable_stringtable};
                        m_primitive_block.write_stringtable(pbf_string_table);
                    }

                    primitive_block.add_message(OSMFormat::PrimitiveBlock::repeated_PrimitiveGroup_primitivegroup, m_primitive_block.group_data());

                    m_output_queue.push(m_pool.submit(
                        SerializeBlob{std::move(primitive_block_data),
                                      pbf_blob_type::data,
                                      m_options.use_compression,
                                      m_options.compression_level}
                    ));
                }

                template <typename T>
                void add_meta(const osmium::OSMObject& object, T& pbf_object) {
                    {
                        protozero::packed_field_uint32 field{pbf_object, protozero::pbf_tag_type(T::enum_type::packed_uint32_keys)};
                        for (const auto& tag : object.tags()) {
                            field.add_element(m_primitive_block.store_in_stringtable_unsigned(tag.key()));
                        }
                    }

                    {
                        protozero::packed_field_uint32 field{pbf_object, protozero::pbf_tag_type(T::enum_type::packed_uint32_vals)};
                        for (const auto& tag : object.tags()) {
                            field.add_element(m_primitive_block.store_in_stringtable_unsigned(tag.value()));
                        }
                    }

                    if (m_options.add_metadata.any() || m_options.add_visible_flag) {
                        protozero::pbf_builder<OSMFormat::Info> pbf_info{pbf_object, T::enum_type::optional_Info_info};

                        if (m_options.add_metadata.version()) {
                            assert(object.version() <= static_cast<std::size_t>(std::numeric_limits<int32_t>::max()));
                            pbf_info.add_int32(OSMFormat::Info::optional_int32_version, static_cast<int32_t>(object.version()));
                        }
                        if (m_options.add_metadata.timestamp()) {
                            pbf_info.add_int64(OSMFormat::Info::optional_int64_timestamp, uint32_t(object.timestamp()));
                        }
                        if (m_options.add_metadata.changeset()) {
                            pbf_info.add_int64(OSMFormat::Info::optional_int64_changeset, object.changeset());
                        }
                        if (m_options.add_metadata.uid()) {
                            assert(object.uid() <= static_cast<std::size_t>(std::numeric_limits<int32_t>::max()));
                            pbf_info.add_int32(OSMFormat::Info::optional_int32_uid, static_cast<int32_t>(object.uid()));
                        }
                        if (m_options.add_metadata.user()) {
                            pbf_info.add_uint32(OSMFormat::Info::optional_uint32_user_sid, m_primitive_block.store_in_stringtable_unsigned(object.user()));
                        }
                        if (m_options.add_visible_flag) {
                            pbf_info.add_bool(OSMFormat::Info::optional_bool_visible, object.visible());
                        }
                    }
                }

                void switch_primitive_block_type(OSMFormat::PrimitiveGroup type) {
                    if (!m_primitive_block.can_add(type)) {
                        store_primitive_block();
                        m_primitive_block.reset(type);
                    }
                }

            public:

                PBFOutputFormat(osmium::thread::Pool& pool, const osmium::io::File& file, future_string_queue_type& output_queue) :
                    OutputFormat(pool, output_queue),
                    m_primitive_block(m_options) {

                    if (!file.get("pbf_add_metadata").empty()) {
                        throw std::invalid_argument{"The 'pbf_add_metadata' option is deprecated. Please use 'add_metadata' instead."};
                    }

                    m_options.use_dense_nodes = file.is_not_false("pbf_dense_nodes");
                    m_options.use_compression = get_compression_type(file.get("pbf_compression"));
                    m_options.add_metadata = osmium::metadata_options{file.get("add_metadata")};
                    m_options.add_historical_information_flag = file.has_multiple_object_versions();
                    m_options.add_visible_flag = file.has_multiple_object_versions();
                    m_options.locations_on_ways = file.is_true("locations_on_ways");

                    const auto pbl = file.get("pbf_compression_level");
                    if (pbl.empty()) {
                        switch (m_options.use_compression) {
                            case pbf_compression::none:
                                break;
                            case pbf_compression::zlib:
                                m_options.compression_level = osmium::io::detail::zlib_default_compression_level();
                                break;
                            case pbf_compression::lz4:
#ifdef OSMIUM_WITH_LZ4
                                m_options.compression_level = osmium::io::detail::lz4_default_compression_level();
#endif
                                break;
                        }
                    } else {
                        char *end = nullptr;
                        const auto val = std::strtol(pbl.c_str(), &end, 10);
                        if (*end != '\0') {
                            throw std::invalid_argument{"The 'pbf_compression_level' option must be an integer."};
                        }
                        switch (m_options.use_compression) {
                            case pbf_compression::none:
                                throw std::invalid_argument{"The 'pbf_compression_level' option doesn't make sense without 'pbf_compression' set."};
                            case pbf_compression::zlib:
                                osmium::io::detail::zlib_check_compression_level(val);
                                break;
                            case pbf_compression::lz4:
#ifdef OSMIUM_WITH_LZ4
                                osmium::io::detail::lz4_check_compression_level(val);
#endif
                                break;
                        }
                        m_options.compression_level = static_cast<int>(val);
                    }
                }

                void write_header(const osmium::io::Header& header) final {
                    std::string data;
                    protozero::pbf_builder<OSMFormat::HeaderBlock> pbf_header_block{data};

                    if (!header.boxes().empty()) {
                        protozero::pbf_builder<OSMFormat::HeaderBBox> pbf_header_bbox{pbf_header_block, OSMFormat::HeaderBlock::optional_HeaderBBox_bbox};

                        osmium::Box box = header.joined_boxes();
                        pbf_header_bbox.add_sint64(OSMFormat::HeaderBBox::required_sint64_left,   int64_t(box.bottom_left().lon() * lonlat_resolution));
                        pbf_header_bbox.add_sint64(OSMFormat::HeaderBBox::required_sint64_right,  int64_t(box.top_right().lon()   * lonlat_resolution));
                        pbf_header_bbox.add_sint64(OSMFormat::HeaderBBox::required_sint64_top,    int64_t(box.top_right().lat()   * lonlat_resolution));
                        pbf_header_bbox.add_sint64(OSMFormat::HeaderBBox::required_sint64_bottom, int64_t(box.bottom_left().lat() * lonlat_resolution));
                    }

                    pbf_header_block.add_string(OSMFormat::HeaderBlock::repeated_string_required_features, "OsmSchema-V0.6");

                    if (m_options.use_dense_nodes) {
                        pbf_header_block.add_string(OSMFormat::HeaderBlock::repeated_string_required_features, "DenseNodes");
                    }

                    if (m_options.add_historical_information_flag) {
                        pbf_header_block.add_string(OSMFormat::HeaderBlock::repeated_string_required_features, "HistoricalInformation");
                    }

                    if (m_options.locations_on_ways) {
                        pbf_header_block.add_string(OSMFormat::HeaderBlock::repeated_string_optional_features, "LocationsOnWays");
                    }

                    if (header.get("sorting") == "Type_then_ID") {
                        pbf_header_block.add_string(OSMFormat::HeaderBlock::repeated_string_optional_features, "Sort.Type_then_ID");
                    }

                    pbf_header_block.add_string(OSMFormat::HeaderBlock::optional_string_writingprogram, header.get("generator"));

                    const std::string osmosis_replication_timestamp{header.get("osmosis_replication_timestamp")};
                    if (!osmosis_replication_timestamp.empty()) {
                        osmium::Timestamp ts{osmosis_replication_timestamp.c_str()};
                        pbf_header_block.add_int64(OSMFormat::HeaderBlock::optional_int64_osmosis_replication_timestamp, uint32_t(ts));
                    }

                    const std::string osmosis_replication_sequence_number{header.get("osmosis_replication_sequence_number")};
                    if (!osmosis_replication_sequence_number.empty()) {
                        pbf_header_block.add_int64(OSMFormat::HeaderBlock::optional_int64_osmosis_replication_sequence_number, osmium::detail::str_to_int<int64_t>(osmosis_replication_sequence_number.c_str()));
                    }

                    const std::string osmosis_replication_base_url{header.get("osmosis_replication_base_url")};
                    if (!osmosis_replication_base_url.empty()) {
                        pbf_header_block.add_string(OSMFormat::HeaderBlock::optional_string_osmosis_replication_base_url, osmosis_replication_base_url);
                    }

                    m_output_queue.push(m_pool.submit(
                        SerializeBlob{std::move(data),
                                      pbf_blob_type::header,
                                      m_options.use_compression,
                                      m_options.compression_level}
                        ));
                }

                void write_buffer(osmium::memory::Buffer&& buffer) final {
                    osmium::apply(buffer.cbegin(), buffer.cend(), *this);
                }

                void write_end() final {
                    store_primitive_block();
                }

                void node(const osmium::Node& node) {
                    if (m_options.use_dense_nodes) {
                        switch_primitive_block_type(OSMFormat::PrimitiveGroup::optional_DenseNodes_dense);
                        m_primitive_block.add_dense_node(node);
                        return;
                    }

                    switch_primitive_block_type(OSMFormat::PrimitiveGroup::repeated_Node_nodes);
                    protozero::pbf_builder<OSMFormat::Node> pbf_node{m_primitive_block.group(), OSMFormat::PrimitiveGroup::repeated_Node_nodes};

                    pbf_node.add_sint64(OSMFormat::Node::required_sint64_id, node.id());
                    add_meta(node, pbf_node);

                    pbf_node.add_sint64(OSMFormat::Node::required_sint64_lat, lonlat2int(node.location().lat_without_check()));
                    pbf_node.add_sint64(OSMFormat::Node::required_sint64_lon, lonlat2int(node.location().lon_without_check()));
                }

                void way(const osmium::Way& way) {
                    switch_primitive_block_type(OSMFormat::PrimitiveGroup::repeated_Way_ways);
                    protozero::pbf_builder<OSMFormat::Way> pbf_way{m_primitive_block.group(), OSMFormat::PrimitiveGroup::repeated_Way_ways};

                    pbf_way.add_int64(OSMFormat::Way::required_int64_id, way.id());
                    add_meta(way, pbf_way);

                    {
                        osmium::DeltaEncode<object_id_type, int64_t> delta_id;
                        protozero::packed_field_sint64 field{pbf_way, protozero::pbf_tag_type(OSMFormat::Way::packed_sint64_refs)};
                        for (const auto& node_ref : way.nodes()) {
                            field.add_element(delta_id.update(node_ref.ref()));
                        }
                    }

                    if (m_options.locations_on_ways) {
                        {
                            osmium::DeltaEncode<int64_t, int64_t> delta_id;
                            protozero::packed_field_sint64 field{pbf_way, protozero::pbf_tag_type(OSMFormat::Way::packed_sint64_lon)};
                            for (const auto& node_ref : way.nodes()) {
                                field.add_element(delta_id.update(lonlat2int(node_ref.location().lon_without_check())));
                            }
                        }
                        {
                            osmium::DeltaEncode<int64_t, int64_t> delta_id;
                            protozero::packed_field_sint64 field{pbf_way, protozero::pbf_tag_type(OSMFormat::Way::packed_sint64_lat)};
                            for (const auto& node_ref : way.nodes()) {
                                field.add_element(delta_id.update(lonlat2int(node_ref.location().lat_without_check())));
                            }
                        }
                    }
                }

                void relation(const osmium::Relation& relation) {
                    switch_primitive_block_type(OSMFormat::PrimitiveGroup::repeated_Relation_relations);
                    protozero::pbf_builder<OSMFormat::Relation> pbf_relation{m_primitive_block.group(), OSMFormat::PrimitiveGroup::repeated_Relation_relations};

                    pbf_relation.add_int64(OSMFormat::Relation::required_int64_id, relation.id());
                    add_meta(relation, pbf_relation);

                    {
                        protozero::packed_field_int32 field{pbf_relation, protozero::pbf_tag_type(OSMFormat::Relation::packed_int32_roles_sid)};
                        for (const auto& member : relation.members()) {
                            field.add_element(m_primitive_block.store_in_stringtable(member.role()));
                        }
                    }

                    {
                        osmium::DeltaEncode<object_id_type, int64_t> delta_id;
                        protozero::packed_field_sint64 field{pbf_relation, protozero::pbf_tag_type(OSMFormat::Relation::packed_sint64_memids)};
                        for (const auto& member : relation.members()) {
                            field.add_element(delta_id.update(member.ref()));
                        }
                    }

                    {
                        protozero::packed_field_int32 field{pbf_relation, protozero::pbf_tag_type(OSMFormat::Relation::packed_MemberType_types)};
                        for (const auto& member : relation.members()) {
                            field.add_element(int32_t(osmium::item_type_to_nwr_index(member.type())));
                        }
                    }
                }

            }; // class PBFOutputFormat

            // we want the register_output_format() function to run, setting
            // the variable is only a side-effect, it will never be used
            const bool registered_pbf_output = osmium::io::detail::OutputFormatFactory::instance().register_output_format(osmium::io::file_format::pbf,
                [](osmium::thread::Pool& pool, const osmium::io::File& file, future_string_queue_type& output_queue) {
                    return new osmium::io::detail::PBFOutputFormat{pool, file, output_queue};
            });

            // dummy function to silence the unused variable warning from above
            inline bool get_registered_pbf_output() noexcept {
                return registered_pbf_output;
            }

        } // namespace detail

    } // namespace io

} // namespace osmium

#endif // OSMIUM_IO_DETAIL_PBF_OUTPUT_FORMAT_HPP
