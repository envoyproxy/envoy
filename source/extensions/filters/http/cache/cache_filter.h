#pragma once

#include <memory>
#include <vector>

#include "envoy/extensions/filters/http/cache/v3/cache.pb.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/cache/cache_filter_logging_info.h"
#include "source/extensions/filters/http/cache/cache_headers_utils.h"
#include "source/extensions/filters/http/cache/filter_state.h"
#include "source/extensions/filters/http/cache/http_cache.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

class UpstreamRequest;

class CacheFilterConfig {
public:
  CacheFilterConfig(const envoy::extensions::filters::http::cache::v3::CacheConfig& config,
                    Server::Configuration::CommonFactoryContext& context);

  // The allow list rules that decide if a header can be varied upon.
  const VaryAllowList& varyAllowList() const { return vary_allow_list_; }
  TimeSource& timeSource() const { return time_source_; }
  const Http::AsyncClient::StreamOptions& upstreamOptions() const { return upstream_options_; }
  Upstream::ClusterManager& clusterManager() const { return cluster_manager_; }
  bool ignoreRequestCacheControlHeader() const { return ignore_request_cache_control_header_; }

private:
  const VaryAllowList vary_allow_list_;
  TimeSource& time_source_;
  const bool ignore_request_cache_control_header_;
  Upstream::ClusterManager& cluster_manager_;
  Http::AsyncClient::StreamOptions upstream_options_;
};

/**
 * A filter that caches responses and attempts to satisfy requests from cache.
 */
class CacheFilter : public Http::PassThroughFilter,
                    public Logger::Loggable<Logger::Id::cache_filter>,
                    public std::enable_shared_from_this<CacheFilter> {
public:
  CacheFilter(std::shared_ptr<const CacheFilterConfig> config,
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

  static LookupStatus resolveLookupStatus(absl::optional<CacheEntryStatus> cache_entry_status,
                                          FilterState filter_state);

private:
  // For a cache miss that may be cacheable, the upstream request is sent outside of the usual
  // filter chain so that the request can continue even if the downstream client disconnects.
  void sendUpstreamRequest(Http::RequestHeaderMap& request_headers);

  // In the event that there is no matching route when attempting to sendUpstreamRequest,
  // send a 404 locally.
  void sendNoRouteResponse();

  // In the event that there is no available cluster when attempting to sendUpstreamRequest,
  // send a 503 locally.
  void sendNoClusterResponse(absl::string_view cluster_name);

  // Called by UpstreamRequest if it is reset before CacheFilter is destroyed.
  // CacheFilter must make no more calls to upstream_request_ once this has been called.
  void onUpstreamRequestReset();

  // Called by UpstreamRequest if it finishes without reset before CacheFilter is destroyed.
  // CacheFilter must make no more calls to upstream_request_ once this has been called.
  void onUpstreamRequestComplete();

  // Utility functions; make any necessary checks and call the corresponding lookup_ functions
  void getHeaders(Http::RequestHeaderMap& request_headers);
  void getBody();
  void getTrailers();

  // Callbacks for HttpCache to call when headers/body/trailers are ready.
  void onHeaders(LookupResult&& result, Http::RequestHeaderMap& request_headers, bool end_stream);
  void onBody(Buffer::InstancePtr&& body, bool end_stream);
  void onTrailers(Http::ResponseTrailerMapPtr&& trailers);

  // Set required state in the CacheFilter for handling a cache hit.
  void handleCacheHit(bool end_stream_after_headers);

  // Set up the required state in the CacheFilter for handling a range
  // request.
  void handleCacheHitWithRangeRequest();

  // Set required state in the CacheFilter for handling a cache hit when
  // validation is required.
  void handleCacheHitWithValidation(Envoy::Http::RequestHeaderMap& request_headers);

  // Precondition: lookup_result_ points to a cache lookup result that requires validation.
  // Should only be called during onHeaders as it modifies RequestHeaderMap.
  // Adds required conditional headers for cache validation to the request headers
  // according to the present cache lookup result headers.
  void injectValidationHeaders(Http::RequestHeaderMap& request_headers);

  // Precondition: lookup_result_ points to a fresh or validated cache look up result.
  // Adds a cache lookup result to the response encoding stream.
  // Can be called during decoding if a valid cache hit is found,
  // or during encoding if a cache entry was validated successfully.
  //
  // When validating, headers should be set to the merged values from the validation
  // response and the lookup_result_; if unset, the headers from the lookup_result_ are used.
  void encodeCachedResponse(bool end_stream_after_headers);

  // Precondition: finished adding a response from cache to the response encoding stream.
  // Updates filter_state_ and continues the encoding stream if necessary.
  void finalizeEncodingCachedResponse();

  // The result of this request's cache lookup.
  LookupStatus lookupStatus() const;

  // The final status of the insert operation or header update, or decision not
  // to insert or update. If the request or insert is ongoing, assumes it's
  // being cancelled.
  InsertStatus insertStatus() const;

  // upstream_request_ belongs to the object itself, so that it can be disconnected
  // from the filter and still complete the cache-write in the event that the
  // downstream disconnects. The filter and the UpstreamRequest must communicate to
  // each other their separate destruction-triggers.
  // When CacheFilter is destroyed first it should call
  // upstream_request_->disconnectFilter()
  // and if upstream_request_ is destroyed first, it will call onUpstreamRequestReset.
  UpstreamRequest* upstream_request_ = nullptr;
  std::shared_ptr<HttpCache> cache_;
  LookupContextPtr lookup_;
  LookupResultPtr lookup_result_;
  absl::optional<CacheEntryStatus> cache_entry_status_;

  // Tracks what body bytes still need to be read from the cache. This is
  // currently only one Range, but will expand when full range support is added. Initialized by
  // onHeaders for Range Responses, otherwise initialized by encodeCachedResponse.
  std::vector<AdjustedByteRange> remaining_ranges_;

  const std::shared_ptr<const CacheFilterConfig> config_;

  // True if a request allows cache inserts according to:
  // https://httpwg.org/specs/rfc7234.html#response.cacheability
  bool request_allows_inserts_ = false;

  FilterState filter_state_ = FilterState::Initial;

  bool is_head_request_ = false;
  // This toggle is used to detect callbacks being called directly and not posted.
  bool callback_called_directly_ = false;
  // The status of the insert operation or header update, or decision not to insert or update.
  // If it's too early to determine the final status, this is empty.
  absl::optional<InsertStatus> insert_status_;

  friend class UpstreamRequest;
};

using CacheFilterSharedPtr = std::shared_ptr<CacheFilter>;
using CacheFilterWeakPtr = std::weak_ptr<CacheFilter>;

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
