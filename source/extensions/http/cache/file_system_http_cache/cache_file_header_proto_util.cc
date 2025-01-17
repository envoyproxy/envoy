#include "source/extensions/http/cache/file_system_http_cache/cache_file_header_proto_util.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {
namespace {
template <typename KeyValue>
Http::HeaderMap::Iterate copyToKeyValue(const Http::HeaderEntry& header, KeyValue* kv) {
  kv->set_key(std::string{header.key().getStringView()});
  kv->set_value(std::string{header.value().getStringView()});
  return Http::HeaderMap::Iterate::Continue;
}
} // namespace

CacheFileHeader mergeProtoWithHeadersAndMetadata(const CacheFileHeader& entry_headers,
                                                 const Http::ResponseHeaderMap& response_headers,
                                                 const ResponseMetadata& response_metadata) {
  Http::ResponseHeaderMapPtr merge_headers = headersFromHeaderProto(entry_headers);
  applyHeaderUpdate(response_headers, *merge_headers);
  return makeCacheFileHeaderProto(entry_headers.key(), *merge_headers, response_metadata);
}

CacheFileHeader makeCacheFileHeaderProto(const Key& key,
                                         const Http::ResponseHeaderMap& response_headers,
                                         const ResponseMetadata& metadata) {
  CacheFileHeader file_header;
  *file_header.mutable_key() = key;
  TimestampUtil::systemClockToTimestamp(metadata.response_time_,
                                        *file_header.mutable_metadata_response_time());
  response_headers.iterate([&file_header](const Http::HeaderEntry& header) {
    return copyToKeyValue(header, file_header.add_headers());
  });
  return file_header;
}

CacheFileTrailer makeCacheFileTrailerProto(const Http::ResponseTrailerMap& response_trailers) {
  CacheFileTrailer file_trailer;
  response_trailers.iterate([&file_trailer](const Http::HeaderEntry& trailer) {
    return copyToKeyValue(trailer, file_trailer.add_trailers());
  });
  return file_trailer;
}

size_t headerProtoSize(const CacheFileHeader& proto) { return proto.ByteSizeLong(); }

Buffer::OwnedImpl bufferFromProto(const CacheFileHeader& proto) {
  // TODO(ravenblack): consider proto.SerializeToZeroCopyStream with an impl to Buffer.
  return Buffer::OwnedImpl{proto.SerializeAsString()};
}

Buffer::OwnedImpl bufferFromProto(const CacheFileTrailer& proto) {
  // TODO(ravenblack): consider proto.SerializeToZeroCopyStream with an impl to Buffer.
  return Buffer::OwnedImpl{proto.SerializeAsString()};
}

std::string serializedStringFromProto(const CacheFileHeader& proto) {
  return proto.SerializeAsString();
}

Http::ResponseHeaderMapPtr headersFromHeaderProto(const CacheFileHeader& header) {
  Http::ResponseHeaderMapPtr headers = Http::ResponseHeaderMapImpl::create();
  for (const CacheFileHeader::Header& h : header.headers()) {
    headers->addCopy(Http::LowerCaseString(h.key()), h.value());
  }
  return headers;
}

Http::ResponseTrailerMapPtr trailersFromTrailerProto(const CacheFileTrailer& trailer) {
  Http::ResponseTrailerMapPtr trailers = Http::ResponseTrailerMapImpl::create();
  for (const CacheFileTrailer::Trailer& t : trailer.trailers()) {
    trailers->addCopy(Http::LowerCaseString(t.key()), t.value());
  }
  return trailers;
}

ResponseMetadata metadataFromHeaderProto(const CacheFileHeader& header) {
  ResponseMetadata metadata;
  metadata.response_time_ = SystemTime{std::chrono::milliseconds(
      Protobuf::util::TimeUtil::TimestampToMilliseconds(header.metadata_response_time()))};
  return metadata;
}

CacheFileHeader makeCacheFileHeaderProto(Buffer::Instance& buffer) {
  CacheFileHeader ret;
  ret.ParseFromString(buffer.toString());
  return ret;
}

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
