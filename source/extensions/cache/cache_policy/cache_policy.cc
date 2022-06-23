#include "source/extensions/cache/cache_policy/cache_policy.h"

#include <stdint.h>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "envoy/registry/registry.h"

#include "source/common/http/utility.h"
#include "source/extensions/filters/http/cache/cache_custom_headers.h"
#include "source/extensions/filters/http/cache/key.pb.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace Cache {

using ::Envoy::Extensions::HttpFilters::Cache::RequestCacheControl;
using ::Envoy::Extensions::HttpFilters::Cache::ResponseCacheControl;
using ::Envoy::Extensions::HttpFilters::Cache::CacheCustomHeaders;
using ::Envoy::Extensions::HttpFilters::Cache::VaryAllowList;
using ::Envoy::Extensions::HttpFilters::Cache::CacheEntryStatus;
using ::Envoy::Extensions::HttpFilters::Cache::ResponseMetadata;
using ::Envoy::Extensions::HttpFilters::Cache::Key;

Key CachePolicyImpl::createCacheKey(const Http::RequestHeaderMap& request_headers) {
  ASSERT(request_headers.ForwardedProto(),
         "Can't form cache lookup key for malformed Http::RequestHeaderMap "
         "with null ForwardedProto.");

  Key key;
  // TODO (capoferro): Get cluster name from config
  key.set_cluster_name("envoy");
  key.set_host(std::string(request_headers.Host()->value().getStringView()));

  absl::string_view path_and_query = request_headers.Path()->value().getStringView();
  size_t query_offset = path_and_query.find('?');
  key.set_path(std::string(path_and_query.substr(0, query_offset)));
  key.set_query(std::string(path_and_query.substr(query_offset + 1, -1)));
  const Envoy::Http::HeaderString& forwarded_proto = request_headers.ForwardedProto()->value();
  const auto& scheme_values = Envoy::Http::Headers::get().SchemeValues;
  if (forwarded_proto == scheme_values.Http) {
    key.set_scheme(Key::HTTP);
  } else if (forwarded_proto == scheme_values.Https) {
    key.set_scheme(Key::HTTPS);
  }
  return key;
}

bool CachePolicyImpl::requestCacheable(const Http::RequestHeaderMap& request_headers,
                                       const RequestCacheControl& request_cache_control) {
  const absl::string_view method = request_headers.getMethodValue();
  const absl::string_view forwarded_proto = request_headers.getForwardedProtoValue();
  const Http::HeaderValues& header_values = Http::Headers::get();

  // Check if the request contains any conditional headers.
  // For now, requests with conditional headers bypass the CacheFilter.
  // This behavior does not cause any incorrect results, but may reduce the
  // cache effectiveness. If needed to be handled properly refer to:
  // https://httpwg.org/specs/rfc7234.html#validation.received
  for (auto conditional_header : conditionalHeaders()) {
    if (!request_headers.get(*conditional_header).empty()) {
      return false;
    }
  }

  // TODO(toddmgreer): Also serve HEAD requests from cache.
  // Cache-related headers are checked in HttpCache::LookupRequest.
  if (!(request_headers.Path() && request_headers.Host() &&
        !request_headers.getInline(CacheCustomHeaders::authorization()) &&
        (method == header_values.MethodValues.Get) &&
        (forwarded_proto == header_values.SchemeValues.Http ||
         forwarded_proto == header_values.SchemeValues.Https))) {
    return false;
  }

  return !request_cache_control.no_store_;
}

// TODO (kiehl): accept vary_allow_list in constructor and don't take it as
// parameter?
bool CachePolicyImpl::responseCacheable(
    [[maybe_unused]] const Http::RequestHeaderMap& request_headers,
    [[maybe_unused]] const Http::ResponseHeaderMap& response_headers,
    const ResponseCacheControl& response_cache_control, const VaryAllowList& vary_allow_list) {
  // Only cache responses with enough data to calculate freshness lifetime as
  // per: https://httpwg.org/specs/rfc7234.html#calculating.freshness.lifetime.
  // Either:
  //    "no-cache" cache-control directive (requires revalidation anyway).
  //    "max-age" or "s-maxage" cache-control directives.
  //    Both "Expires" and "Date" headers.
  const bool has_validation_data =
      response_cache_control.must_validate_ || response_cache_control.max_age_.has_value() ||
      (response_headers.Date() && response_headers.getInline(CacheCustomHeaders::expires()));

  return !response_cache_control.no_store_ &&
         cacheableStatusCodes().contains((response_headers.getStatusValue())) &&
         has_validation_data && vary_allow_list.allowsHeaders(response_headers);
}

CacheEntryUsability CachePolicyImpl::computeCacheEntryUsability(
    [[maybe_unused]] const Http::RequestHeaderMap& request_headers,
    const Http::ResponseHeaderMap& cached_response_headers,
    const RequestCacheControl& request_cache_control,
    const ResponseCacheControl& cached_response_cache_control,
    [[maybe_unused]] const uint64_t content_length, const ResponseMetadata& cached_metadata,
    SystemTime now) {
  CacheEntryUsability result;

  result.age =
      HttpFilters::Cache::CacheHeadersUtils::calculateAge(cached_response_headers, cached_metadata.response_time_, now);

  result.status = CacheEntryStatus::Ok;
  if (requiresValidation(request_cache_control, cached_response_cache_control,
                         cached_response_headers, result.age)) {
    result.status = CacheEntryStatus::RequiresValidation;
  }

  return result;
}

bool CachePolicyImpl::requiresValidation(const RequestCacheControl& request_cache_control,
                                         const ResponseCacheControl& cached_response_cache_control,
                                         const Http::ResponseHeaderMap& response_headers,
                                         Seconds response_age) const {
  const bool request_max_age_exceeded = request_cache_control.max_age_.has_value() &&
                                        request_cache_control.max_age_.value() < response_age;
  if (cached_response_cache_control.must_validate_ || request_cache_control.must_validate_ ||
      request_max_age_exceeded) {
    // Either the request or response explicitly require validation, or a
    // request max-age requirement is not satisfied.
    return true;
  }

  // CachePolicyImpl::responseCacheable() guarantees that any cached
  // response satisfies this.
  ASSERT(cached_response_cache_control.max_age_.has_value() ||
             (response_headers.getInline(CacheCustomHeaders::expires()) && response_headers.Date()),
         "Cache entry does not have valid expiration data.");

  SystemTime::duration freshness_lifetime;
  if (cached_response_cache_control.max_age_.has_value()) {
    freshness_lifetime = cached_response_cache_control.max_age_.value();
  } else {
    const SystemTime expires_value =
        HttpFilters::Cache::CacheHeadersUtils::httpTime(response_headers.getInline(CacheCustomHeaders::expires()));
    const SystemTime date_value = HttpFilters::Cache::CacheHeadersUtils::httpTime(response_headers.Date());
    freshness_lifetime = expires_value - date_value;
  }

  if (response_age > freshness_lifetime) {
    // Response is stale, requires validation if
    // the response does not allow being served stale,
    // or the request max-stale directive does not allow it.
    const bool allowed_by_max_stale =
        request_cache_control.max_stale_.has_value() &&
        request_cache_control.max_stale_.value() > response_age - freshness_lifetime;
    return cached_response_cache_control.no_stale_ || !allowed_by_max_stale;
  } else {
    // Response is fresh, requires validation only if there is an unsatisfied
    // min-fresh requirement.
    const bool min_fresh_unsatisfied =
        request_cache_control.min_fresh_.has_value() &&
        request_cache_control.min_fresh_.value() > freshness_lifetime - response_age;
    return min_fresh_unsatisfied;
  }
}

const absl::flat_hash_set<absl::string_view>& CachePolicyImpl::cacheableStatusCodes() const {
  // As defined by:
  // https://tools.ietf.org/html/rfc7231#section-6.1,
  // https://tools.ietf.org/html/rfc7538#section-3,
  // https://tools.ietf.org/html/rfc7725#section-3
  // TODO(yosrym93): the list of cacheable status codes should be configurable.
  CONSTRUCT_ON_FIRST_USE(absl::flat_hash_set<absl::string_view>, "200", "203", "204", "206", "300",
                         "301", "308", "404", "405", "410", "414", "451", "501");
}

const std::vector<const Http::LowerCaseString*>& CachePolicyImpl::conditionalHeaders() const {
  // As defined by: https://httpwg.org/specs/rfc7232.html#preconditions.
  CONSTRUCT_ON_FIRST_USE(
      std::vector<const Http::LowerCaseString*>, &Http::CustomHeaders::get().IfMatch,
      &Http::CustomHeaders::get().IfNoneMatch, &Http::CustomHeaders::get().IfModifiedSince,
      &Http::CustomHeaders::get().IfUnmodifiedSince, &Http::CustomHeaders::get().IfRange);
}

} // namespace Cache
} // namespace Extensions
} // namespace Envoy
