#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/cache/v3alpha/cache.pb.h"

#include "common/common/logger.h"

#include "extensions/filters/http/cache/cache_headers_utils.h"
#include "extensions/filters/http/cache/http_cache.h"
#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

/**
 * A filter that caches responses and attempts to satisfy requests from cache.
 */
class CacheFilter : public Http::PassThroughFilter,
                    public Logger::Loggable<Logger::Id::cache_filter> {
public:
  CacheFilter(const envoy::extensions::filters::http::cache::v3alpha::CacheConfig& config,
              const std::string& stats_prefix, Stats::Scope& scope, TimeSource& time_source,
              HttpCache& http_cache);
  // Http::StreamFilterBase
  void onDestroy() override;
  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& buffer, bool end_stream) override;

private:
  // Utility functions: make any necessary checks and call the corresponding lookup_ functions.
  void getBody();
  void getTrailers();

  // Callbacks for HttpCache to call when headers/body/trailers are ready.
  void onHeaders(LookupResult&& result, Http::RequestHeaderMap& request_headers);
  void onBody(Buffer::InstancePtr&& body);
  void onTrailers(Http::ResponseTrailerMapPtr&& trailers);

  // Precondition: lookup_result_ points to a cache lookup result that requires validation.
  //               filter_state_ is ValidatingCachedResponse.
  // Serves a validated cached response after updating it with a 304 response.
  void processSuccessfulValidation(Http::ResponseHeaderMap& response_headers);

  // Precondition: lookup_result_ points to a cache lookup result that requires validation.
  //               filter_state_ is ValidatingCachedResponse.
  // Checks if a cached entry should be updated with a 304 response.
  bool shouldUpdateCachedEntry(const Http::ResponseHeaderMap& response_headers) const;

  // Precondition: lookup_result_ points to a cache lookup result that requires validation.
  // Should only be called during onHeaders as it modifies RequestHeaderMap.
  // Adds required conditional headers for cache validation to the request headers
  // according to the present cache lookup result headers.
  void injectValidationHeaders(Http::RequestHeaderMap& request_headers);

  // Precondition: lookup_result_ points to a fresh or validated cache look up result.
  //               filter_state_ is ValidatingCachedResponse.
  // Adds a cache lookup result to the response encoding stream.
  // Can be called during decoding if a valid cache hit is found,
  // or during encoding if a cache entry was validated successfully.
  void encodeCachedResponse();

  // Precondition: finished adding a response from cache to the response encoding stream.
  // Updates filter_state_ and continues the encoding stream if necessary.
  void finalizeEncodingCachedResponse();

  TimeSource& time_source_;
  HttpCache& cache_;
  LookupContextPtr lookup_;
  InsertContextPtr insert_;
  LookupResultPtr lookup_result_;

  // Tracks what body bytes still need to be read from the cache. This is currently only one Range,
  // but will expand when full range support is added. Initialized by encodeCachedResponse.
  std::vector<AdjustedByteRange> remaining_body_;

  // True if the response has trailers.
  // TODO(toddmgreer): cache trailers.
  bool response_has_trailers_ = false;

  // True if a request allows cache inserts according to:
  // https://httpwg.org/specs/rfc7234.html#response.cacheability
  bool request_allows_inserts_ = false;

  enum class FilterState {
    Initial,

    // CacheFilter::decodeHeaders called lookup->getHeaders() but onHeaders was not called yet
    // (lookup result not ready) -- the decoding stream should be stopped until the cache lookup
    // result is ready.
    WaitingForCacheLookup,

    // CacheFilter::encodeHeaders called encodeCachedResponse() but encoding the cached response is
    // not finished yet -- the encoding stream should be stopped until it is finished.
    WaitingForCacheBody,

    // Cache lookup did not find a cached response for this request.
    NoCachedResponseFound,

    // Cache lookup found a cached response that requires validation.
    ValidatingCachedResponse,

    // Cache lookup found a fresh cached response and it is being added to the encoding stream.
    DecodeServingFromCache,

    // The cached response was successfully added to the encoding stream (either during decoding or
    // encoding).
    ResponseServedFromCache
  };
  FilterState filter_state_ = FilterState::Initial;
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
