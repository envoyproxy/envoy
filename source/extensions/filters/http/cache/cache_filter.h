#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/cache/v3alpha/cache.pb.h"
#include "envoy/http/header_map.h"

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
  // Utility functions; make any necessary checks and call the corresponding lookup_ functions
  void inline getHeaders();
  void inline getBody();
  void inline getTrailers();

  // Callbacks for HttpCache to call when headers/body/trailers are ready
  void onHeaders(LookupResult&& result);
  void onBody(Buffer::InstancePtr&& body);
  void onTrailers(Http::ResponseTrailerMapPtr&& trailers);

  // Precondition: lookup_result_ points to a cache lookup result that requires validation
  // Checks if a cached entry should be updated with a 304 response
  bool shouldUpdateCachedEntry(const Http::ResponseHeaderMap& response_headers) const;

  // Precondition: request_headers_ points to the RequestHeadersMap of the current request
  //               lookup_result_ points to a cache lookup result that requires validation
  // Should only be called during onHeaders as it modifies RequestHeaderMap
  // Adds required conditional headers for cache validation to the request headers
  // according to the present cache lookup result headers
  void injectValidationHeaders();

  // Precondition: lookup_result_ points to a fresh or validated cache look up result
  // Adds a cache lookup result to the response encoding stream
  // Can be called during decoding if a valid cache hit is found
  // or during encoding if a cache entry was validated successfully
  void encodeCachedResponse();

  // Precondition: finished adding a response from cache to the response encoding stream
  // Updates the encode_cached_response_state_ and continue encoding filter iteration if necessary
  void finishedEncodingCachedResponse();

  TimeSource& time_source_;
  HttpCache& cache_;
  LookupContextPtr lookup_;
  InsertContextPtr insert_;
  LookupResultPtr lookup_result_;

  // Used exclusively to store a reference to the request header map passed to decodeHeaders to be
  // used in onHeaders afterwards the pointer must not be used and is set back to null
  Http::RequestHeaderMap* request_headers_ = nullptr;

  // Tracks what body bytes still need to be read from the cache. This is currently only one Range,
  // but will expand when full range support is added. Initialized by encodeCachedResponse.
  std::vector<AdjustedByteRange> remaining_body_;

  // True if the response has trailers.
  // TODO(toddmgreer): cache trailers.
  bool response_has_trailers_ = false;

  // True if a request allows cache inserts according to:
  // https://httpwg.org/specs/rfc7234.html#response.cacheability
  bool request_allows_inserts_ = false;

  // True if the CacheFilter is injected validation headers and should check for 304 responses
  bool validating_cache_entry_ = false;

  // True if CacheFilter::encodeHeaders & CacheFilter::encodeData should be skipped
  bool skip_encoding_ = false;

  // Used for coordinating between decodeHeaders and onHeaders.
  enum class GetHeadersState { Initial, FinishedGetHeadersCall, GetHeadersResultUnusable };
  GetHeadersState get_headers_state_ = GetHeadersState::Initial;

  // Used for coordinating between encodeHeaders/encodeData and onBody/onTrailers
  enum class EncodeCachedResponseState { Initial, FinishedEncoding, IterationStopped };
  EncodeCachedResponseState encode_cached_response_state_ = EncodeCachedResponseState::Initial;

  enum class FilterState { Initial, Decoding, Encoding };
  FilterState filter_state_ = FilterState::Initial;
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
