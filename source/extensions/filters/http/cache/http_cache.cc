#include "source/extensions/filters/http/cache/http_cache.h"

#include <algorithm>
#include <ostream>
#include <vector>

#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"

#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/cache/cache_custom_headers.h"
#include "source/extensions/filters/http/cache/cache_headers_utils.h"
#include "source/extensions/cache/cache_policy/cache_policy.h"

#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

LookupRequest::LookupRequest(const Http::RequestHeaderMap& request_headers,
                             RequestCacheControl&& request_cache_control, CachePolicy& cache_policy,
                             SystemTime timestamp, const VaryAllowList& vary_allow_list)
    : request_headers_(Http::createHeaderMap<Http::RequestHeaderMapImpl>(request_headers)),
      request_cache_control_(std::move(request_cache_control)), cache_policy_(cache_policy),
      key_(cache_policy_.createCacheKey(*request_headers_)), vary_allow_list_(vary_allow_list),
      timestamp_(timestamp) {
  // These ASSERTs check prerequisites. A request without these headers can't be
  // looked up in cache; CacheFilter doesn't create LookupRequests for such
  // requests.
  ASSERT(request_headers_->Path(),
         "Can't form cache lookup key for malformed Http::RequestHeaderMap "
         "with null Path.");
  ASSERT(request_headers_->ForwardedProto(),
         "Can't form cache lookup key for malformed Http::RequestHeaderMap "
         "with null ForwardedProto.");
  ASSERT(request_headers_->Host(),
         "Can't form cache lookup key for malformed Http::RequestHeaderMap "
         "with null Host.");
  ASSERT(request_headers.ForwardedProto());
  const Http::HeaderString& forwarded_proto = request_headers.ForwardedProto()->value();
  const auto& scheme_values = Http::Headers::get().SchemeValues;

  ASSERT(forwarded_proto == scheme_values.Http || forwarded_proto == scheme_values.Https);
}

// Unless this API is still alpha, calls to stableHashKey() must always return
// the same result, or a way must be provided to deal with a complete cache
// flush.
size_t stableHashKey(const Key& key) { return MessageUtil::hash(key); }

LookupResult LookupRequest::makeLookupResult(Http::ResponseHeaderMapPtr&& response_headers,
                                             ResponseMetadata& metadata, uint64_t content_length,
                                             bool has_trailers) const {
  // TODO(toddmgreer): Implement all HTTP caching semantics.
  ASSERT(response_headers);
  LookupResult result;

  ResponseCacheControl response_cache_control(*response_headers);
  CacheEntryUsability usability = cache_policy_.computeCacheEntryUsability(
      requestHeaders(), *response_headers, request_cache_control_, response_cache_control,
      content_length, metadata, timestamp_);

  response_headers->setInline(CacheCustomHeaders::age(), std::to_string(usability.age.count()));

  result.cache_entry_status_ = usability.status;
  result.headers_ = std::move(response_headers);
  result.content_length_ = content_length;
  result.range_details_ = RangeUtils::createRangeDetails(requestHeaders(), content_length);
  result.has_trailers_ = has_trailers;

  return result;
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
