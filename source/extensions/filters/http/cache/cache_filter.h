#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/cache/v3/cache.pb.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/cache/cache_filter_logging_info.h"
#include "source/extensions/filters/http/cache/cache_headers_utils.h"
#include "source/extensions/filters/http/cache/cache_insert_queue.h"
#include "source/extensions/filters/http/cache/http_cache.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

enum class FilterState {
  Initial,

  // Cache lookup found a cached response that requires validation.
  ValidatingCachedResponse,

  // Cache lookup found a fresh cached response and it is being added to the encoding stream.
  DecodeServingFromCache,

  // A cached response was successfully validated and it is being added to the encoding stream
  EncodeServingFromCache,

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

/**
 * A filter that caches responses and attempts to satisfy requests from cache.
 */
class CacheFilter : public Http::PassThroughFilter,
                    public Logger::Loggable<Logger::Id::cache_filter>,
                    public std::enable_shared_from_this<CacheFilter> {
public:
  CacheFilter(const envoy::extensions::filters::http::cache::v3::CacheConfig& config,
              const std::string& stats_prefix, Stats::Scope& scope, TimeSource& time_source,
              std::shared_ptr<HttpCache> http_cache);
  // Http::StreamFilterBase
  void onDestroy() override;
  void onStreamComplete() override;
  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& buffer, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;

  static LookupStatus resolveLookupStatus(absl::optional<CacheEntryStatus> cache_entry_status,
                                          FilterState filter_state);

private:
  // Utility functions; make any necessary checks and call the corresponding lookup_ functions
  void getHeaders(Http::RequestHeaderMap& request_headers);
  void getBody();
  void getTrailers();

  // Callbacks for HttpCache to call when headers/body/trailers are ready.
  void onHeaders(LookupResult&& result, Http::RequestHeaderMap& request_headers);
  void onBody(Buffer::InstancePtr&& body);
  void onTrailers(Http::ResponseTrailerMapPtr&& trailers);

  // Set required state in the CacheFilter for handling a cache hit.
  void handleCacheHit();

  // Set up the required state in the CacheFilter for handling a range
  // request.
  void handleCacheHitWithRangeRequest();

  // Set required state in the CacheFilter for handling a cache hit when
  // validation is required.
  void handleCacheHitWithValidation(Envoy::Http::RequestHeaderMap& request_headers);

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
  // Adds a cache lookup result to the response encoding stream.
  // Can be called during decoding if a valid cache hit is found,
  // or during encoding if a cache entry was validated successfully.
  void encodeCachedResponse();

  // Precondition: finished adding a response from cache to the response encoding stream.
  // Updates filter_state_ and continues the encoding stream if necessary.
  void finalizeEncodingCachedResponse();

  // The result of this request's cache lookup.
  LookupStatus lookupStatus() const;

  // The final status of the insert operation or header update, or decision not
  // to insert or update. If the request or insert is ongoing, assumes it's
  // being cancelled.
  InsertStatus insertStatus() const;

  // insert_queue_ ownership may be passed to the queue itself during
  // CacheFilter::onDestroy, allowing the insert queue to outlive the filter
  // while the necessary cache write operations complete.
  std::unique_ptr<CacheInsertQueue> insert_queue_;
  TimeSource& time_source_;
  std::shared_ptr<HttpCache> cache_;
  LookupContextPtr lookup_;
  LookupResultPtr lookup_result_;

  // Tracks what body bytes still need to be read from the cache. This is
  // currently only one Range, but will expand when full range support is added. Initialized by
  // onHeaders for Range Responses, otherwise initialized by encodeCachedResponse.
  std::vector<AdjustedByteRange> remaining_ranges_;

  // TODO(#12901): The allow list could be constructed only once directly from the config, instead
  // of doing it per-request. A good example of such config is found in the gzip filter:
  // source/extensions/filters/http/gzip/gzip_filter.h.
  // Stores the allow list rules that decide if a header can be varied upon.
  VaryAllowList vary_allow_list_;

  // True if the response has trailers.
  // TODO(toddmgreer): cache trailers.
  bool response_has_trailers_ = false;

  // True if a request allows cache inserts according to:
  // https://httpwg.org/specs/rfc7234.html#response.cacheability
  bool request_allows_inserts_ = false;

  FilterState filter_state_ = FilterState::Initial;

  bool is_head_request_ = false;
  // The status of the insert operation or header update, or decision not to insert or update.
  // If it's too early to determine the final status, this is empty.
  absl::optional<InsertStatus> insert_status_;
};

using CacheFilterSharedPtr = std::shared_ptr<CacheFilter>;
using CacheFilterWeakPtr = std::weak_ptr<CacheFilter>;

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
