#include "extensions/filters/http/cache/http_cache.h"

#include <algorithm>

#include "common/http/headers.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/cache/http_cache_utils.h"

#include "absl/time/time.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

LookupRequest::LookupRequest(const Http::HeaderMap& request_headers, SystemTime timestamp)
    : timestamp_(timestamp),
      request_cache_control_(request_headers.CacheControl() == nullptr
                                 ? ""
                                 : request_headers.CacheControl()->value().getStringView()) {
  ASSERT(request_headers.Path(), "Can't form cache lookup key for malformed Http::HeaderMap "
                                 "with null Path.");
  ASSERT(request_headers.Scheme(), "Can't form cache lookup key for malformed Http::HeaderMap "
                                   "with null Scheme.");
  ASSERT(request_headers.Host(), "Can't form cache lookup key for malformed Http::HeaderMap "
                                 "with null Host.");
  const Http::HeaderString& scheme = request_headers.Scheme()->value();
  const auto& scheme_values = Http::Headers::get().SchemeValues;
  ASSERT(scheme == scheme_values.Http || scheme == scheme_values.Https);
  // TODO(toddmgreer) Let config determine whether to include scheme, host, and
  // query params.
  // TODO(toddmgreer) get cluster name.
  // TODO(toddmgreer) Parse Range header into request_range_spec_.
  key_.set_cluster_name("cluster_name_goes_here");
  key_.set_host(std::string(request_headers.Host()->value().getStringView()));
  key_.set_path(std::string(request_headers.Path()->value().getStringView()));
  key_.set_clear_http(scheme == scheme_values.Http);
}

// Unless this API is still alpha, calls to stableHashKey() must always return
// the same result, or a way must be provided to deal with a complete cache
// flush. localHashKey however, can be changed at will.
size_t stableHashKey(const Key& key) { return MessageUtil::hash(key); }
size_t localHashKey(const Key& key) { return stableHashKey(key); }

// Returns true if response_headers is fresh.
bool LookupRequest::isFresh(const Http::HeaderMap& response_headers) const {
  const Http::HeaderEntry* cache_control_header = response_headers.CacheControl();
  if (cache_control_header) {
    const SystemTime::duration effective_max_age =
        Internal::effectiveMaxAge(cache_control_header->value().getStringView());
    return timestamp_ - Internal::httpTime(response_headers.Date()) < effective_max_age;
  }
  // We didn't find a cache-control header with enough info to determine
  // freshness, so fall back to the expires header.
  return timestamp_ <= Internal::httpTime(response_headers.get(Http::LowerCaseString("expires")));
}

LookupResult LookupRequest::makeLookupResult(Http::HeaderMapPtr&& response_headers,
                                             uint64_t content_length) const {
  // TODO(toddmgreer) Implement all HTTP caching semantics.
  ASSERT(response_headers);
  LookupResult result;
  if (!isFresh(*response_headers)) {
    result.cache_entry_status = CacheEntryStatus::RequiresValidation;
    return result;
  }

  result.headers = std::move(response_headers);
  result.content_length = content_length;
  if (adjustByteRangeSet(result.response_ranges, content_length)) {
    result.cache_entry_status = CacheEntryStatus::Ok;
  } else {
    result.cache_entry_status = CacheEntryStatus::UnsatisfiableRange;
  }
  return result;
}

// Adjusts response_ranges to fit a cached response of size content_length.
// Returns true if response_ranges is satisfiable (empty is considered
// satisfiable, as it denotes the entire body).
// TODO(toddmgreer) Merge/reorder ranges where appropriate.
bool LookupRequest::adjustByteRangeSet(std::vector<AdjustedByteRange>& response_ranges,
                                       uint64_t content_length) const {
  if (request_range_spec_.empty()) {
    // No range header, so the request can proceed.
    return true;
  }

  if (content_length == 0) {
    // There is a range header, but it's unsatisfiable.
    return false;
  }

  for (const RawByteRange& spec : request_range_spec_) {
    if (spec.isSuffix()) {
      // spec is a suffix-byte-range-spec
      if (spec.suffixLength() >= content_length) {
        // All bytes are being requested, so we may as well send a normal '200
        // OK' response.
        response_ranges.clear();
        return true;
      }
      response_ranges.emplace_back(content_length - spec.suffixLength(), content_length - 1);
    } else {
      // spec is a byte-range-spec
      if (spec.firstBytePos() >= content_length) {
        // This range is unsatisfiable, so skip it.
        continue;
      }
      response_ranges.emplace_back(spec.firstBytePos(),
                                   std::min(spec.lastBytePos(), content_length - 1));
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
