#include "source/extensions/http/cache/file_system_http_cache/cache_file_header_proto_util.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {
namespace {

const absl::flat_hash_set<Http::LowerCaseString> headersNotToUpdate() {
  CONSTRUCT_ON_FIRST_USE(
      absl::flat_hash_set<Http::LowerCaseString>,
      // Content range should not be changed upon validation
      Http::Headers::get().ContentRange,

      // Headers that describe the body content should never be updated.
      Http::Headers::get().ContentLength,

      // It does not make sense for this level of the code to be updating the ETag, when
      // presumably the cached_response_headers reflect this specific ETag.
      Http::CustomHeaders::get().Etag,

      // We don't update the cached response on a Vary; we just delete it
      // entirely. So don't bother copying over the Vary header.
      Http::CustomHeaders::get().Vary);
}
} // namespace

void updateProtoFromHeadersAndMetadata(CacheFileHeader& entry, const CacheFileHeader& response) {
  *entry.mutable_metadata_response_time() = response.metadata_response_time();
  absl::flat_hash_set<Http::LowerCaseString> updated_header_fields;
  for (const auto& incoming_response_header : response.headers()) {
    Http::LowerCaseString key{incoming_response_header.key()};
    if (headersNotToUpdate().contains(key)) {
      continue;
    }
    if (!updated_header_fields.contains(key)) {
      auto it = entry.mutable_headers()->begin();
      while (it != entry.mutable_headers()->end() && Http::LowerCaseString{it->key()} != key) {
        ++it;
      }
      if (it == entry.mutable_headers()->end()) {
        auto h = entry.add_headers();
        h->set_key(key.get());
        h->set_value(incoming_response_header.value());
      } else {
        it->set_value(incoming_response_header.value());
        ++it;
        while (it != entry.mutable_headers()->end()) {
          if (Http::LowerCaseString{it->key()} == key) {
            it = entry.mutable_headers()->erase(it);
          } else {
            ++it;
          }
        }
      }
      updated_header_fields.insert(key);
    } else {
      auto h = entry.add_headers();
      h->set_key(key.get());
      h->set_value(incoming_response_header.value());
    }
  }
}

CacheFileHeader makeCacheFileHeaderProto(const Key& key,
                                         const Http::ResponseHeaderMap& response_headers,
                                         const ResponseMetadata& metadata) {
  CacheFileHeader file_header;
  *file_header.mutable_key() = key;
  TimestampUtil::systemClockToTimestamp(metadata.response_time_,
                                        *file_header.mutable_metadata_response_time());
  response_headers.iterate([&file_header](const Http::HeaderEntry& header) {
    auto h = file_header.add_headers();
    h->set_key(std::string{header.key().getStringView()});
    h->set_value(std::string{header.value().getStringView()});
    return Http::HeaderMap::Iterate::Continue;
  });
  return file_header;
}

CacheFileTrailer makeCacheFileTrailerProto(const Http::ResponseTrailerMap& response_trailers) {
  CacheFileTrailer file_trailer;
  response_trailers.iterate([&file_trailer](const Http::HeaderEntry& trailer) {
    auto t = file_trailer.add_trailers();
    t->set_key(std::string{trailer.key().getStringView()});
    t->set_value(std::string{trailer.value().getStringView()});
    return Http::HeaderMap::Iterate::Continue;
  });
  return file_trailer;
}

size_t headerProtoSize(const CacheFileHeader& proto) { return proto.SerializeAsString().size(); }

Buffer::OwnedImpl bufferFromProto(const CacheFileHeader& proto) {
  return Buffer::OwnedImpl{proto.SerializeAsString()};
}

Buffer::OwnedImpl bufferFromProto(const CacheFileTrailer& proto) {
  return Buffer::OwnedImpl{proto.SerializeAsString()};
}

std::string serializedStringFromProto(const CacheFileHeader& proto) {
  return proto.SerializeAsString();
}

Http::ResponseHeaderMapPtr headersFromHeaderProto(const CacheFileHeader& header) {
  auto headers = Http::ResponseHeaderMapImpl::create();
  for (const auto& h : header.headers()) {
    headers->addCopy(Http::LowerCaseString(h.key()), h.value());
  }
  return headers;
}

Http::ResponseTrailerMapPtr trailersFromTrailerProto(const CacheFileTrailer& trailer) {
  auto trailers = Http::ResponseTrailerMapImpl::create();
  for (const auto& t : trailer.trailers()) {
    trailers->addCopy(Http::LowerCaseString(t.key()), t.value());
  }
  return trailers;
}

ResponseMetadata metadataFromHeaderProto(const CacheFileHeader& header) {
  ResponseMetadata metadata;
  SystemTime epoch_time;
  metadata.response_time_ =
      epoch_time + std::chrono::milliseconds(Protobuf::util::TimeUtil::TimestampToMilliseconds(
                       header.metadata_response_time()));
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
