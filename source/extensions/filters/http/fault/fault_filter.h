#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "envoy/api/v2/route/route.pb.h"
#include "envoy/config/filter/http/fault/v2/fault.pb.h"
#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats_macros.h"

#include "common/router/config_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Fault {

/**
 * All stats for the fault filter. @see stats_macros.h
 */
// clang-format off
#define ALL_FAULT_FILTER_STATS(COUNTER)                                                            \
  COUNTER(delays_injected)                                                                         \
  COUNTER(aborts_injected)
// clang-format on

/**
 * Wrapper struct for connection manager stats. @see stats_macros.h
 */
struct FaultFilterStats {
  ALL_FAULT_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the fault filter that can be specified per route
 */
struct PerRouteFaultFilterConfig : public Router::RouteSpecificFilterConfig {
  PerRouteFaultFilterConfig(const envoy::config::filter::http::fault::v2::HTTPFault& fault);

  uint64_t abort_percent_{};       // 0-100
  uint64_t http_status_{};         // HTTP or gRPC return codes
  uint64_t fixed_delay_percent_{}; // 0-100
  uint64_t fixed_duration_ms_{};   // in milliseconds
  std::string upstream_cluster_;   // restrict faults to specific upstream cluster
  std::vector<Router::ConfigUtility::HeaderData> fault_filter_headers_;
  std::unordered_set<std::string> downstream_nodes_{}; // Inject failures for specific downstream
};

typedef std::shared_ptr<const PerRouteFaultFilterConfig> PerRouteFaultFilterConfigConstSharedPtr;

/**
 * Configuration for the fault filter.
 */
class FaultFilterConfig {
public:
  FaultFilterConfig(const envoy::config::filter::http::fault::v2::HTTPFault& fault,
                    Runtime::Loader& runtime, const std::string& stats_prefix, Stats::Scope& scope);

  const std::vector<Router::ConfigUtility::HeaderData>& filterHeaders() {
    return filter_config_->fault_filter_headers_;
  }
  uint64_t abortPercent() { return filter_config_->abort_percent_; }
  uint64_t delayPercent() { return filter_config_->fixed_delay_percent_; }
  uint64_t delayDuration() { return filter_config_->fixed_duration_ms_; }
  uint64_t abortCode() { return filter_config_->http_status_; }
  const std::string& upstreamCluster() { return filter_config_->upstream_cluster_; }
  Runtime::Loader& runtime() { return runtime_; }
  FaultFilterStats& stats() { return stats_; }
  const std::unordered_set<std::string>& downstreamNodes() {
    return filter_config_->downstream_nodes_;
  }
  const std::string& statsPrefix() { return stats_prefix_; }
  Stats::Scope& scope() { return scope_; }
  bool emptyFilterConfig() { return filter_config_ == nullptr; }

  void updateFilterConfig(const PerRouteFaultFilterConfig* config) {
    filter_config_ = config != nullptr ? config : filter_config_;
  }

private:
  static FaultFilterStats generateStats(const std::string& prefix, Stats::Scope& scope);

  const PerRouteFaultFilterConfig* filter_config_;
  PerRouteFaultFilterConfig local_config_;
  Runtime::Loader& runtime_;
  FaultFilterStats stats_;
  const std::string stats_prefix_;
  Stats::Scope& scope_;
};

typedef std::shared_ptr<FaultFilterConfig> FaultFilterConfigSharedPtr;

/**
 * A filter that is capable of faulting an entire request before dispatching it upstream.
 */
class FaultFilter : public Http::StreamDecoderFilter {
public:
  FaultFilter(FaultFilterConfigSharedPtr config);
  ~FaultFilter();

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

private:
  void recordAbortsInjectedStats();
  void recordDelaysInjectedStats();
  void resetTimerState();
  void postDelayInjection();
  void abortWithHTTPStatus();
  bool matchesTargetUpstreamCluster();
  bool matchesDownstreamNodes(const Http::HeaderMap& headers);

  bool isAbortEnabled();
  bool isDelayEnabled();
  absl::optional<uint64_t> delayDuration();
  uint64_t abortHttpStatus();

  FaultFilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  Event::TimerPtr delay_timer_;
  std::string downstream_cluster_{};
  bool stream_destroyed_{};

  std::string downstream_cluster_delay_percent_key_{};
  std::string downstream_cluster_abort_percent_key_{};
  std::string downstream_cluster_delay_duration_key_{};
  std::string downstream_cluster_abort_http_status_key_{};

  const static std::string DELAY_PERCENT_KEY;
  const static std::string ABORT_PERCENT_KEY;
  const static std::string DELAY_DURATION_KEY;
  const static std::string ABORT_HTTP_STATUS_KEY;
};

} // namespace Fault
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
