#include "extensions/filters/http/cache/http_cache.h"

#include <algorithm>
#include <ostream>

#include "envoy/http/codes.h"

#include "common/http/headers.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/cache/http_cache_utils.h"

#include "absl/time/time.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    request_cache_control_handle(Http::CustomHeaders::get().CacheControl);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    response_cache_control_handle(Http::CustomHeaders::get().CacheControl);

std::ostream& operator<<(std::ostream& os, CacheEntryStatus status) {
  switch (status) {
  case CacheEntryStatus::Ok:
    return os << "Ok";
  case CacheEntryStatus::Unusable:
    return os << "Unusable";
  case CacheEntryStatus::RequiresValidation:
    return os << "RequiresValidation";
  case CacheEntryStatus::FoundNotModified:
    return os << "FoundNotModified";
  case CacheEntryStatus::UnsatisfiableRange:
    return os << "UnsatisfiableRange";
  }
  NOT_REACHED_GCOVR_EXCL_LINE;
}

std::ostream& operator<<(std::ostream& os, const AdjustedByteRange& range) {
  return os << "[" << range.begin() << "," << range.end() << ")";
}

LookupRequest::LookupRequest(const Http::RequestHeaderMap& request_headers, SystemTime timestamp)
    : timestamp_(timestamp), request_cache_control_(request_headers.getInlineValue(
                                 request_cache_control_handle.handle())) {
  // These ASSERTs check prerequisites. A request without these headers can't be looked up in cache;
  // CacheFilter doesn't create LookupRequests for such requests.
  ASSERT(request_headers.Path(), "Can't form cache lookup key for malformed Http::RequestHeaderMap "
                                 "with null Path.");
  ASSERT(
      request_headers.ForwardedProto(),
      "Can't form cache lookup key for malformed Http::RequestHeaderMap with null ForwardedProto.");
  ASSERT(request_headers.Host(), "Can't form cache lookup key for malformed Http::RequestHeaderMap "
                                 "with null Host.");
  const Http::HeaderString& forwarded_proto = request_headers.ForwardedProto()->value();
  const auto& scheme_values = Http::Headers::get().SchemeValues;
  ASSERT(forwarded_proto == scheme_values.Http || forwarded_proto == scheme_values.Https);
  // TODO(toddmgreer): Let config determine whether to include forwarded_proto, host, and
  // query params.
  // TODO(toddmgreer): get cluster name.
  // TODO(toddmgreer): Parse Range header into request_range_spec_, and handle the resultant
  // vector<AdjustedByteRange> in CacheFilter::onOkHeaders.
  key_.set_cluster_name("cluster_name_goes_here");
  key_.set_host(std::string(request_headers.getHostValue()));
  key_.set_path(std::string(request_headers.getPathValue()));
  key_.set_clear_http(forwarded_proto == scheme_values.Http);
}

// Unless this API is still alpha, calls to stableHashKey() must always return
// the same result, or a way must be provided to deal with a complete cache
// flush. localHashKey however, can be changed at will.
size_t stableHashKey(const Key& key) { return MessageUtil::hash(key); }
size_t localHashKey(const Key& key) { return stableHashKey(key); }

// Returns true if response_headers is fresh.
bool LookupRequest::isFresh(const Http::ResponseHeaderMap& response_headers) const {
  if (!response_headers.Date()) {
    return false;
  }
  const Http::HeaderEntry* cache_control_header =
      response_headers.getInline(response_cache_control_handle.handle());
  if (cache_control_header) {
    const SystemTime::duration effective_max_age =
        HttpCacheUtils::effectiveMaxAge(cache_control_header->value().getStringView());
    return timestamp_ - HttpCacheUtils::httpTime(response_headers.Date()) < effective_max_age;
  }
  // We didn't find a cache-control header with enough info to determine
  // freshness, so fall back to the expires header.
  return timestamp_ <= HttpCacheUtils::httpTime(response_headers.get(Http::Headers::get().Expires));
}

LookupResult LookupRequest::makeLookupResult(Http::ResponseHeaderMapPtr&& response_headers,
                                             uint64_t content_length) const {
  // TODO(toddmgreer): Implement all HTTP caching semantics.
  ASSERT(response_headers);
  LookupResult result;
  result.cache_entry_status_ =
      isFresh(*response_headers) ? CacheEntryStatus::Ok : CacheEntryStatus::RequiresValidation;
  result.headers_ = std::move(response_headers);
  result.content_length_ = content_length;
  if (!adjustByteRangeSet(result.response_ranges_, request_range_spec_, content_length)) {
    result.headers_->setStatus(static_cast<uint64_t>(Http::Code::RangeNotSatisfiable));
  }
  result.has_trailers_ = false;
  return result;
}

bool adjustByteRangeSet(std::vector<AdjustedByteRange>& response_ranges,
                        const std::vector<RawByteRange>& request_range_spec,
                        uint64_t content_length) {
  if (request_range_spec.empty()) {
    // No range header, so the request can proceed.
    return true;
  }

  if (content_length == 0) {
    // There is a range header, but it's unsatisfiable.
    return false;
  }

  for (const RawByteRange& spec : request_range_spec) {
    if (spec.isSuffix()) {
      // spec is a suffix-byte-range-spec
      if (spec.suffixLength() == 0) {
        // This range is unsatisfiable, so skip it.
        continue;
      }
      if (spec.suffixLength() >= content_length) {
        // All bytes are being requested, so we may as well send a '200
        // OK' response.
        response_ranges.clear();
        return true;
      }
      response_ranges.emplace_back(content_length - spec.suffixLength(), content_length);
    } else {
      // spec is a byte-range-spec
      if (spec.firstBytePos() >= content_length) {
        // This range is unsatisfiable, so skip it.
        continue;
      }
      if (spec.lastBytePos() >= content_length - 1) {
        if (spec.firstBytePos() == 0) {
          // All bytes are being requested, so we may as well send a '200
          // OK' response.
          response_ranges.clear();
          return true;
        }
        response_ranges.emplace_back(spec.firstBytePos(), content_length);
      } else {
        response_ranges.emplace_back(spec.firstBytePos(), spec.lastBytePos() + 1);
      }
    }
  }
  if (response_ranges.empty()) {
    // All ranges were unsatisfiable.
    return false;
  }
  return true;
}
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
