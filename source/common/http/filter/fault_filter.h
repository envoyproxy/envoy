#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats_macros.h"

#include "common/router/config_impl.h"

namespace Envoy {
namespace Http {

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
 * Configuration for the fault filter.
 */
class FaultFilterConfig {
public:
  FaultFilterConfig(const Json::Object& json_config, Runtime::Loader& runtime,
                    const std::string& stats_prefix, Stats::Store& stats);

  const std::vector<Router::ConfigUtility::HeaderData>& filterHeaders() {
    return fault_filter_headers_;
  }
  uint64_t abortPercent() { return abort_percent_; }
  uint64_t delayPercent() { return fixed_delay_percent_; }
  uint64_t delayDuration() { return fixed_duration_ms_; }
  uint64_t abortCode() { return http_status_; }
  const std::string& upstreamCluster() { return upstream_cluster_; }
  Runtime::Loader& runtime() { return runtime_; }
  FaultFilterStats& stats() { return stats_; }
  bool matchDownstreamCluster() { return match_downstream_cluster_; }
  std::unordered_set<std::string>& downstreamNodes() { return downstream_nodes_; }
  const std::string& statsPrefix() { return stats_prefix_; }
  Stats::Store& statsStore() { return store_; }

private:
  static FaultFilterStats generateStats(const std::string& prefix, Stats::Store& store);

  uint64_t abort_percent_{};       // 0-100
  uint64_t http_status_{};         // HTTP or gRPC return codes
  uint64_t fixed_delay_percent_{}; // 0-100
  uint64_t fixed_duration_ms_{};   // in milliseconds
  std::string upstream_cluster_;   // restrict faults to specific upstream cluster
  std::vector<Router::ConfigUtility::HeaderData> fault_filter_headers_;
  bool match_downstream_cluster_{}; // By default do not match against downstream cluster.
  std::unordered_set<std::string> downstream_nodes_{}; // Inject failures for specific downstream
                                                       // nodes. If not set then inject for all.
  Runtime::Loader& runtime_;
  FaultFilterStats stats_;
  std::string stats_prefix_;
  Stats::Store& store_;
};

typedef std::shared_ptr<FaultFilterConfig> FaultFilterConfigSharedPtr;

/**
 * A filter that is capable of faulting an entire request before dispatching it upstream.
 */
class FaultFilter : public StreamDecoderFilter {
public:
  FaultFilter(FaultFilterConfigSharedPtr config);
  ~FaultFilter();

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  FilterHeadersStatus decodeHeaders(HeaderMap& headers, bool end_stream) override;
  FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  FilterTrailersStatus decodeTrailers(HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) override;

private:
  void recordAbortsInjectedStats();
  void recordDelaysInjectedStats();
  void resetTimerState();
  void postDelayInjection();
  void abortWithHTTPStatus();
  bool matchesTargetUpstreamCluster();
  bool matchesDownstreamNodes(const HeaderMap& headers);

  FaultFilterConfigSharedPtr config_;
  StreamDecoderFilterCallbacks* callbacks_{};
  Event::TimerPtr delay_timer_;
  std::string downstream_cluster_{};

  std::string delay_percent_key_{"fault.http.delay.fixed_delay_percent"};
  std::string abort_percent_key_{"fault.http.abort.abort_percent"};
  std::string delay_duration_key_{"fault.http.delay.fixed_duration_ms"};
  std::string abort_http_status_key_{"fault.http.abort.http_status"};
};

} // Http
} // Envoy
