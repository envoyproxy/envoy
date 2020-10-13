#include "extensions/filters/http/cache/http_cache.h"

#include <algorithm>
#include <ostream>
#include <vector>

#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"

#include "common/http/header_utility.h"
#include "common/http/headers.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/cache/cache_headers_utils.h"
#include "extensions/filters/http/cache/inline_headers_handles.h"

#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

LookupRequest::LookupRequest(const Http::RequestHeaderMap& request_headers, SystemTime timestamp,
                             const VaryHeader& vary_allow_list)
    : timestamp_(timestamp) {
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

  initializeRequestCacheControl(request_headers);
  // TODO(toddmgreer): Let config determine whether to include forwarded_proto, host, and
  // query params.
  // TODO(toddmgreer): get cluster name.
  if (request_headers.getMethodValue() == Http::Headers::get().MethodValues.Get) {
    // TODO(cbdm): using a constant limit of 1 range since we don't support multi-part responses nor
    // coalesce multiple overlapping ranges. Could make this into a parameter based on config.
    const int RangeSpecifierLimit = 1;
    request_range_spec_ = RangeRequests::parseRanges(request_headers, RangeSpecifierLimit);
  }
  key_.set_cluster_name("cluster_name_goes_here");
  key_.set_host(std::string(request_headers.getHostValue()));
  key_.set_path(std::string(request_headers.getPathValue()));
  key_.set_clear_http(forwarded_proto == scheme_values.Http);

  vary_headers_ = vary_allow_list.possibleVariedHeaders(request_headers);
}

// Unless this API is still alpha, calls to stableHashKey() must always return
// the same result, or a way must be provided to deal with a complete cache
// flush. localHashKey however, can be changed at will.
size_t stableHashKey(const Key& key) { return MessageUtil::hash(key); }
size_t localHashKey(const Key& key) { return stableHashKey(key); }

void LookupRequest::initializeRequestCacheControl(const Http::RequestHeaderMap& request_headers) {
  const absl::string_view cache_control =
      request_headers.getInlineValue(request_cache_control_handle.handle());
  const absl::string_view pragma = request_headers.getInlineValue(pragma_handle.handle());

  if (!cache_control.empty()) {
    request_cache_control_ = RequestCacheControl(cache_control);
  } else {
    // According to: https://httpwg.org/specs/rfc7234.html#header.pragma,
    // when Cache-Control header is missing, "Pragma:no-cache" is equivalent to
    // "Cache-Control:no-cache". Any other directives are ignored.
    request_cache_control_.must_validate_ = RequestCacheControl(pragma).must_validate_;
  }
}

bool LookupRequest::requiresValidation(const Http::ResponseHeaderMap& response_headers,
                                       SystemTime::duration response_age) const {
  // TODO(yosrym93): Store parsed response cache-control in cache instead of parsing it on every
  // lookup.
  const absl::string_view cache_control =
      response_headers.getInlineValue(response_cache_control_handle.handle());
  const ResponseCacheControl response_cache_control(cache_control);

  const bool request_max_age_exceeded = request_cache_control_.max_age_.has_value() &&
                                        request_cache_control_.max_age_.value() < response_age;
  if (response_cache_control.must_validate_ || request_cache_control_.must_validate_ ||
      request_max_age_exceeded) {
    // Either the request or response explicitly require validation, or a request max-age
    // requirement is not satisfied.
    return true;
  }

  // CacheabilityUtils::isCacheableResponse(..) guarantees that any cached response satisfies this.
  ASSERT(response_cache_control.max_age_.has_value() ||
             (response_headers.getInline(expires_handle.handle()) && response_headers.Date()),
         "Cache entry does not have valid expiration data.");

  SystemTime::duration freshness_lifetime;
  if (response_cache_control.max_age_.has_value()) {
    freshness_lifetime = response_cache_control.max_age_.value();
  } else {
    const SystemTime expires_value =
        CacheHeadersUtils::httpTime(response_headers.getInline(expires_handle.handle()));
    const SystemTime date_value = CacheHeadersUtils::httpTime(response_headers.Date());
    freshness_lifetime = expires_value - date_value;
  }

  if (response_age > freshness_lifetime) {
    // Response is stale, requires validation if
    // the response does not allow being served stale,
    // or the request max-stale directive does not allow it.
    const bool allowed_by_max_stale =
        request_cache_control_.max_stale_.has_value() &&
        request_cache_control_.max_stale_.value() > response_age - freshness_lifetime;
    return response_cache_control.no_stale_ || !allowed_by_max_stale;
  } else {
    // Response is fresh, requires validation only if there is an unsatisfied min-fresh requirement.
    const bool min_fresh_unsatisfied =
        request_cache_control_.min_fresh_.has_value() &&
        request_cache_control_.min_fresh_.value() > freshness_lifetime - response_age;
    return min_fresh_unsatisfied;
  }
}

LookupResult LookupRequest::makeLookupResult(Http::ResponseHeaderMapPtr&& response_headers,
                                             ResponseMetadata&& metadata,
                                             uint64_t content_length) const {
  // TODO(toddmgreer): Implement all HTTP caching semantics.
  ASSERT(response_headers);
  LookupResult result;

  // Assumption: Cache lookup time is negligible. Therefore, now == timestamp_
  const Seconds age =
      CacheHeadersUtils::calculateAge(*response_headers, metadata.response_time_, timestamp_);
  response_headers->setInline(age_handle.handle(), std::to_string(age.count()));

  result.cache_entry_status_ = requiresValidation(*response_headers, age)
                                   ? CacheEntryStatus::RequiresValidation
                                   : CacheEntryStatus::Ok;
  result.headers_ = std::move(response_headers);
  result.content_length_ = content_length;
  if (!adjustByteRangeSet(result.response_ranges_, request_range_spec_, content_length)) {
    result.cache_entry_status_ = CacheEntryStatus::NotSatisfiableRange;
  } else if (!result.response_ranges_.empty()) {
    result.cache_entry_status_ = CacheEntryStatus::SatisfiableRange;
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
      // spec is a suffix-byte-range-spec.
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

std::vector<RawByteRange> RangeRequests::parseRanges(const Http::RequestHeaderMap& request_headers,
                                                     uint64_t max_byte_range_specs) {
  // Makes sure we have a GET request, as Range headers are only valid with this type of request.
  const absl::string_view method = request_headers.getMethodValue();
  ASSERT(method == Http::Headers::get().MethodValues.Get);

  // Multiple instances of range headers are invalid.
  // https://tools.ietf.org/html/rfc7230#section-3.2.2
  const auto range_header = request_headers.get(Http::Headers::get().Range);

  absl::string_view header_value;
  if (range_header.size() == 1) {
    header_value = range_header[0]->value().getStringView();
  } else {
    if (range_header.size() > 1) {
      ENVOY_LOG(debug, "Multiple range headers provided in request. Ignoring all range headers.");
    }
    return {};
  }

  if (!absl::ConsumePrefix(&header_value, "bytes=")) {
    ENVOY_LOG(debug, "Invalid range header. range-unit not correctly specified, only 'bytes' "
                     "supported. Ignoring range header.");
    return {};
  }

  std::vector<absl::string_view> ranges =
      absl::StrSplit(header_value, absl::MaxSplits(',', max_byte_range_specs));
  if (ranges.size() > max_byte_range_specs) {
    ENVOY_LOG(debug,
              "There are more ranges than allowed by the byte range parse limit ({}). Ignoring "
              "range header.",
              max_byte_range_specs);
    return {};
  }

  std::vector<RawByteRange> parsed_ranges;
  for (absl::string_view cur_range : ranges) {
    absl::optional<uint64_t> first = CacheHeadersUtils::readAndRemoveLeadingDigits(cur_range);

    if (!absl::ConsumePrefix(&cur_range, "-")) {
      ENVOY_LOG(debug,
                "Invalid format for range header: missing range-end. Ignoring range header.");
      return {};
    }

    absl::optional<uint64_t> last = CacheHeadersUtils::readAndRemoveLeadingDigits(cur_range);

    if (!cur_range.empty()) {
      ENVOY_LOG(debug,
                "Unexpected characters after byte range in range header. Ignoring range header.");
      return {};
    }

    if (!first && !last) {
      ENVOY_LOG(debug, "Invalid format for range header: missing first-byte-pos AND last-byte-pos; "
                       "at least one of them is required. Ignoring range header.");
      return {};
    }

    // Handle suffix range (e.g., -123).
    if (!first) {
      first = std::numeric_limits<uint64_t>::max();
    }

    // Handle optional range-end (e.g., 123-).
    if (!last) {
      last = std::numeric_limits<uint64_t>::max();
    }

    if (first != std::numeric_limits<uint64_t>::max() && first > last) {
      ENVOY_LOG(debug, "Invalid format for range header: range-start and range-end out of order. "
                       "Ignoring range header.");
      return {};
    }

    parsed_ranges.push_back(RawByteRange(first.value(), last.value()));
  }

  return parsed_ranges;
}
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
