#pragma once

#include "envoy/http/header_map.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/cache/cache_entry_utils.h"
#include "source/extensions/http/cache/file_system_http_cache/cache_file_header.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

/**
 * Update an existing CacheFileHeader with new values from an updateHeaders operation.
 * See applyHeaderUpdate in cache_entry_utils.h for details of merge behavior.
 * @param entry_header the CacheFileHeader from the entry to be updated.
 * @param response_headers the http headers from the updateHeaders call.
 * @param response_metadata the metadata from the updateHeaders call.
 * @return the merged CacheFileHeader.
 */
CacheFileHeader mergeProtoWithHeadersAndMetadata(const CacheFileHeader& entry_headers,
                                                 const Http::ResponseHeaderMap& response_headers,
                                                 const ResponseMetadata& response_metadata);

/**
 * Create a CacheFileHeader message from response headers, metadata and key.
 * @param key the cache entry key.
 * @param response_headers the response_headers from updateHeaders or insertHeaders.
 * @param metadata the metadata from updateHeaders or insertHeaders.
 * @return a CacheFileHeader proto containing the key, response headers and metadata.
 */
CacheFileHeader makeCacheFileHeaderProto(const Key& key,
                                         const Http::ResponseHeaderMap& response_headers,
                                         const ResponseMetadata& metadata);

/**
 * Create a CacheFilterTrailer message from response trailers.
 * @param response_trailers the response_trailers from insertTrailers.
 * @return a CacheFileTrailer message containing the http trailers.
 */
CacheFileTrailer makeCacheFileTrailerProto(const Http::ResponseTrailerMap& response_trailers);

/**
 * Serializes the CacheFileHeader proto and returns its size in bytes.
 * @param proto the CacheFileHeader proto to have its serialized size measured.
 */
size_t headerProtoSize(const CacheFileHeader& proto);

/**
 * Serializes the CacheFileHeader proto into a Buffer object.
 * @param proto the CacheFileHeader proto to be serialized.
 * @return a Buffer::OwnedImpl containing the serialized CacheFileHeader.
 */
Buffer::OwnedImpl bufferFromProto(const CacheFileHeader& proto);

/**
 * Serializes the CacheFileTrailer proto into a Buffer object.
 * @param proto the CacheFileTrailer proto to be serialized.
 * @return a Buffer::OwnedImpl containing the serialized CacheFileTrailer.
 */
Buffer::OwnedImpl bufferFromProto(const CacheFileTrailer& proto);

/**
 * Serializes the CacheFileHeader proto into a std::string.
 * @param proto the CacheFileHeader proto to be serialized.
 * @return a std::string containing the serialized CacheFileHeader.
 */
std::string serializedStringFromProto(const CacheFileHeader& proto);

/**
 * Gets the headers from a CacheFileHeader message as an Envoy::Http::ResponseHeaderMapPtr.
 * @param header the CacheFileHeader message from which to extract the headers.
 * @return an Http::ResponseHeaderMapPtr containing the cached response headers.
 */
Http::ResponseHeaderMapPtr headersFromHeaderProto(const CacheFileHeader& header);

/**
 * Gets the trailers from a CacheFileTrailer message as an Envoy::Http::ResponseTrailerMapPtr.
 * @param trailer the CacheFileTrailer message from which to extract the trailers.
 * @return an Http::ResponseTrailerMapPtr containing the cached response trailers.
 */
Http::ResponseTrailerMapPtr trailersFromTrailerProto(const CacheFileTrailer& trailer);

/**
 * Gets the cache metadata from a CacheFileHeader message.
 * @param header the CacheFileHeader message from which to extract the metadata.
 * @return a ResponseMetadata object containing the cached metadata.
 */
ResponseMetadata metadataFromHeaderProto(const CacheFileHeader& header);

/**
 * Deserializes a CacheFileHeader message from a Buffer.
 * @param buffer the buffer containing a serialized CacheFileHeader message.
 * @return the deserialized CacheFileHeader message.
 */
CacheFileHeader makeCacheFileHeaderProto(Buffer::Instance& buffer);

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
