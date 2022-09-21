#pragma once

#include "envoy/http/header_map.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/cache/cache_entry_utils.h"
#include "source/extensions/filters/http/cache/file_system_http_cache/cache_file_header.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

void updateProtoFromHeadersAndMetadata(CacheFileHeader& entry,
                                       const Http::ResponseHeaderMap& response_headers,
                                       const ResponseMetadata& metadata);

CacheFileHeader protoFromHeadersAndMetadata(const Key& key,
                                            const Http::ResponseHeaderMap& response_headers,
                                            const ResponseMetadata& metadata);

size_t headerProtoSize(const CacheFileHeader& proto);

Buffer::OwnedImpl bufferFromProto(const CacheFileHeader& proto);
Buffer::OwnedImpl bufferFromProto(const CacheFileTrailer& proto);
std::string serializedStringFromProto(const CacheFileHeader& proto);

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
