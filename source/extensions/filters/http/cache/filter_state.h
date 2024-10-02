#pragma once

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

enum class FilterState {
  Initial,

  // Cache lookup found a cached response that requires validation.
  ValidatingCachedResponse,

  // Cache lookup found a fresh or validated cached response and it is being added to the encoding
  // stream.
  ServingFromCache,

  // The cached response was successfully added to the encoding stream (either during decoding or
  // encoding).
  ResponseServedFromCache,

  // The filter won't serve a response from the cache, whether because the request wasn't cacheable,
  // there was no response in cache, the response in cache couldn't be served, or the request was
  // terminated before the cached response could be written. This may be set during decoding or
  // encoding.
  NotServingFromCache,

  // CacheFilter::onDestroy has been called, the filter will be destroyed soon. Any triggered
  // callbacks should be ignored.
  Destroyed
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
