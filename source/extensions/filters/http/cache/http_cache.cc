#include "extensions/filters/http/cache/http_cache.h"

#include <algorithm>
#include <ostream>
#include <vector>

#include "envoy/http/codes.h"

#include "common/http/header_utility.h"
#include "common/http/headers.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/cache/http_cache_utils.h"

#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
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
  case CacheEntryStatus::NotSatisfiableRange:
    return os << "NotSatisfiableRange";
  case CacheEntryStatus::SatisfiableRange:
    return os << "SatisfiableRange";
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
  // TODO(toddmgreer): handle the resultant vector<AdjustedByteRange> in CacheFilter::onOkHeaders.
  // Range Requests are only valid for GET requests
  if (request_headers.getMethodValue() == Http::Headers::get().MethodValues.Get) {
    // TODO(cbdm): using a constant limit of 10 ranges, could make this into a parameter
    const int RangeSpecifierLimit = 10;
    request_range_spec_ = RangeRequests::parseRanges(request_headers, RangeSpecifierLimit);
  }
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

std::vector<RawByteRange> RangeRequests::parseRanges(const Http::RequestHeaderMap& request_headers,
                                                     uint64_t max_byte_range_specs) {
  // Makes sure we have a GET request, as Range headers are only valid with this type of request.
  const absl::string_view method = request_headers.getMethodValue();
  ASSERT(method == Http::Headers::get().MethodValues.Get);

  // Multiple instances of range headers are invalid.
  // https://tools.ietf.org/html/rfc7230#section-3.2.2
  std::vector<absl::string_view> range_headers;
  Http::HeaderUtility::getAllOfHeader(request_headers, Http::Headers::get().Range.get(),
                                      range_headers);

  absl::string_view header_value;
  if (range_headers.size() == 1) {
    header_value = range_headers.front();
  } else {
    if (range_headers.size() > 1) {
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

    absl::optional<uint64_t> first = HttpCacheUtils::readAndRemoveLeadingDigits(cur_range);

    if (!absl::ConsumePrefix(&cur_range, "-")) {
      ENVOY_LOG(debug,
                "Invalid format for range header: missing range-end. Ignoring range header.");
      return {};
    }

    absl::optional<uint64_t> last = HttpCacheUtils::readAndRemoveLeadingDigits(cur_range);

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
      first = UINT64_MAX;
    }

    // Handle optional range-end (e.g., 123-).
    if (!last) {
      last = UINT64_MAX;
    }

    if (first != UINT64_MAX && first > last) {
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
