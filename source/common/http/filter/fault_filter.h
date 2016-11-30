#pragma once

#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats_macros.h"

#include "common/json/json_loader.h"
#include "common/router/config_impl.h"

namespace Http {

/**
 * All stats for the fault filter. @see stats_macros.h
 */
// clang-format off
#define ALL_FAULT_FILTER_STATS(COUNTER)                                                           \
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
                    const std::string& stat_prefix, Stats::Store& stats);

  const std::vector<Router::ConfigUtility::HeaderData>& filterHeaders() {
    return fault_filter_headers_;
  }
  uint64_t abortPercent() { return abort_percent_; }
  uint64_t delayPercent() { return fixed_delay_percent_; }
  uint64_t delayDuration() { return fixed_duration_ms_; }
  uint64_t abortCode() { return http_status_; }
  Runtime::Loader& runtime() { return runtime_; }
  FaultFilterStats& stats() { return stats_; }

private:
  static FaultFilterStats generateStats(const std::string& prefix, Stats::Store& store);

  uint64_t abort_percent_{};       // 0-100
  uint64_t http_status_{};         // HTTP or gRPC return codes
  uint64_t fixed_delay_percent_{}; // 0-100
  uint64_t fixed_duration_ms_{};   // in milliseconds
  std::vector<Router::ConfigUtility::HeaderData> fault_filter_headers_;
  Runtime::Loader& runtime_;
  FaultFilterStats stats_;
};

typedef std::shared_ptr<FaultFilterConfig> FaultFilterConfigPtr;

/**
 * A filter that is capable of faulting an entire request before dispatching it upstream.
 */
class FaultFilter : public StreamDecoderFilter {
public:
  FaultFilter(FaultFilterConfigPtr config);
  ~FaultFilter();

  FilterHeadersStatus decodeHeaders(HeaderMap& headers, bool end_stream) override;
  FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  FilterTrailersStatus decodeTrailers(HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) override;

private:
  void onResetStream();
  void resetTimerState();
  void postDelayInjection();
  void abortWithHTTPStatus();

  FaultFilterConfigPtr config_;
  StreamDecoderFilterCallbacks* callbacks_{};
  Event::TimerPtr delay_timer_;
};

} // Http
