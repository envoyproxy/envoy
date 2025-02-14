#pragma once

#include <memory>
#include <vector>

#include "envoy/extensions/filters/http/cache/v3/cache.pb.h"

#include "source/common/common/cancel_wrapper.h"
#include "source/common/common/logger.h"
#include "source/extensions/filters/http/cache/active_cache.h"
#include "source/extensions/filters/http/cache/cache_headers_utils.h"
#include "source/extensions/filters/http/cache/stats.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

class CacheFilterConfig : public CacheableResponseChecker, public CacheFilterStatsProvider {
public:
  CacheFilterConfig(const envoy::extensions::filters::http::cache::v3::CacheConfig& config,
                    std::shared_ptr<ActiveCache> active_cache,
                    Server::Configuration::CommonFactoryContext& context);

  // Implements CacheableResponseChecker::isCacheableResponse.
  bool isCacheableResponse(const Http::ResponseHeaderMap& headers) const override;
  // The allow list rules that decide if a header can be varied upon.
  const VaryAllowList& varyAllowList() const { return vary_allow_list_; }
  TimeSource& timeSource() const { return time_source_; }
  const Http::AsyncClient::StreamOptions& upstreamOptions() const { return upstream_options_; }
  Upstream::ClusterManager& clusterManager() const { return cluster_manager_; }
  bool ignoreRequestCacheControlHeader() const { return ignore_request_cache_control_header_; }
  ActiveCache& activeCache() const { return *active_cache_; }
  bool hasCache() const { return active_cache_ != nullptr; }
  CacheFilterStats& stats() const override { return active_cache_->stats(); }

private:
  const VaryAllowList vary_allow_list_;
  TimeSource& time_source_;
  const bool ignore_request_cache_control_header_;
  Upstream::ClusterManager& cluster_manager_;
  Http::AsyncClient::StreamOptions upstream_options_;
  std::shared_ptr<ActiveCache> active_cache_;
  CacheFilterStatsPtr stats_;
};

/**
 * A filter that caches responses and attempts to satisfy requests from cache.
 */
class CacheFilter : public Http::PassThroughFilter,
                    public Http::DownstreamWatermarkCallbacks,
                    public Logger::Loggable<Logger::Id::cache_filter> {
public:
  CacheFilter(std::shared_ptr<const CacheFilterConfig> config);
  // Http::StreamFilterBase
  void onDestroy() override;
  // Http::StreamDecoderFilter
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;

  // Http::DownstreamWatermarkCallbacks
  void onAboveWriteBufferHighWatermark() override;
  void onBelowWriteBufferLowWatermark() override;

private:
  using CancelFunction = CancelWrapper::CancelFunction;
  // Gets the cluster name for the current route, if there is one.
  absl::optional<absl::string_view> clusterName();
  // Gets an AsyncClient for the given cluster, or nullopt if there is no upstream.
  OptRef<Http::AsyncClient> asyncClient(absl::string_view cluster_name);

  // In the event that there is no matching route when attempting to fetch asyncClient,
  // send a 404 local response.
  void sendNoRouteResponse();

  // In the event that there is no available cluster when attempting to fetch asyncClient,
  // send a 503 local response.
  void sendNoClusterResponse(absl::string_view cluster_name);

  // Utility functions; make any necessary checks and call the corresponding lookup_ functions
  void getHeaders(Http::RequestHeaderMap& request_headers);
  void getBody();
  void getTrailers();

  void onLookupResult(ActiveLookupResultPtr lookup_result);
  void onHeaders(Http::ResponseHeaderMapPtr headers, EndStream end_stream);
  // Returns true if getBody should be called again.
  bool onBody(Buffer::InstancePtr&& body, EndStream end_stream);
  void onTrailers(Http::ResponseTrailerMapPtr&& trailers, EndStream end_stream);
  CacheFilterStats& stats() const { return config_->stats(); }

  void finalizeEncodingCachedResponse();

  std::shared_ptr<HttpCache> cache_;
  ActiveLookupResultPtr lookup_result_;
  bool is_partial_response_ = false;

  // Tracks what body bytes still need to be read from the cache. This is
  // currently only one Range, but will expand when full range support is added. Initialized by
  // onHeaders for Range Responses, otherwise initialized by encodeCachedResponse.
  std::vector<AdjustedByteRange> remaining_ranges_;

  const std::shared_ptr<const CacheFilterConfig> config_;

  // True if a request allows cache inserts according to:
  // https://httpwg.org/specs/rfc7234.html#response.cacheability
  bool request_allows_inserts_ = false;

  bool is_destroyed_ = false;

  bool is_head_request_ = false;
  // If this is populated it should be called from onDestroy.
  CancelFunction cancel_in_flight_callback_;

  int downstream_watermarked_ = 0;
  // To avoid a potential recursion stack-overflow, the onBody function
  // does not call getBody again directly but instead returns true if
  // we *should* call getBody again, allowing it to be a loop rather
  // than recursion.
  enum class GetBodyLoop { InCallback, Again, Idle } get_body_loop_;
  bool get_body_on_unblocked_ = false;
};

using CacheFilterSharedPtr = std::shared_ptr<CacheFilter>;
using CacheFilterWeakPtr = std::weak_ptr<CacheFilter>;

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
