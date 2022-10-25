#include "source/extensions/http/cache/file_system_http_cache/cache_file_header_proto_util.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {
namespace {

const absl::flat_hash_set<absl::string_view> headersNotToUpdate() {
  CONSTRUCT_ON_FIRST_USE(
      absl::flat_hash_set<absl::string_view>,
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
  // This behavior:
  // 1. retains the order of existing header fields if they have only one value.
  // 2. if an existing header had more than one value, the first one's order is retained,
  //    others are moved to the end of the headers.
  // 3. if a header existed in the original but not in the replacement, it persists
  //    (I doubt this is correct behavior; it mimics simple_http_cache's behavior,
  //    and is currently required by the cache tests.)
  // 4. headers from headersNotToUpdate are left unchanged.
  absl::flat_hash_set<absl::string_view> updated_header_fields;
  for (const auto& incoming_response_header : response.headers()) {
    const std::string& key = incoming_response_header.key();
    if (headersNotToUpdate().contains(key)) {
      continue;
    }
    if (!updated_header_fields.contains(key)) {
      auto it = std::find_if(entry.mutable_headers()->begin(), entry.mutable_headers()->end(),
                             [&key](const CacheFileHeader_Header& h) { return h.key() == key; });
      if (it == entry.mutable_headers()->end()) {
        auto h = entry.add_headers();
        h->set_key(key);
        h->set_value(incoming_response_header.value());
      } else {
        it->set_value(incoming_response_header.value());
        ++it;
        while (it != entry.mutable_headers()->end()) {
          if (it->key() == key) {
            it = entry.mutable_headers()->erase(it);
          } else {
            ++it;
          }
        }
      }
      updated_header_fields.insert(key);
    } else {
      auto h = entry.add_headers();
      h->set_key(key);
      h->set_value(incoming_response_header.value());
    }
  }
}

CacheFileHeader protoFromHeadersAndMetadata(const Key& key,
                                            const Http::ResponseHeaderMap& response_headers,
                                            const ResponseMetadata& metadata) {
  CacheFileHeader file_header;
  *file_header.mutable_key() = key;
  TimestampUtil::systemClockToTimestamp(metadata.response_time_,
                                        *file_header.mutable_metadata_response_time());
  response_headers.iterate([&file_header](const Http::HeaderEntry& header) {
    absl::string_view header_key = header.key().getStringView();
    bool found = false;
    for (auto& h : *file_header.mutable_headers()) {
      if (h.key() == header_key) {
        found = true;
        h.set_value(absl::StrCat(h.value(), ",", header.value().getStringView()));
        break;
      }
    }
    if (!found) {
      auto h = file_header.add_headers();
      h->set_key(std::string{header_key});
      h->set_value(std::string{header.value().getStringView()});
    }
    return Http::HeaderMap::Iterate::Continue;
  });
  return file_header;
}

CacheFileTrailer protoFromTrailers(const Http::ResponseTrailerMap& response_trailers) {
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

CacheFileHeader headerProtoFromBuffer(Buffer::Instance& buffer) {
  CacheFileHeader ret;
  ret.ParseFromString(buffer.toString());
  return ret;
}

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
