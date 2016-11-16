#pragma once

#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats_macros.h"

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
struct FaultFilterConfig {
  FaultFilterConfig(uint64_t abort_enabled, uint64_t abort_code, uint64_t delay_enabled,
                    uint64_t delay_duration,
                    const std::vector<Router::ConfigUtility::HeaderData> fault_filter_headers,
                    Runtime::Loader& runtime, const std::string& stat_prefix, Stats::Store& stats)
      : abort_enabled_(abort_enabled), abort_code_(abort_code), delay_enabled_(delay_enabled),
        delay_duration_(delay_duration), fault_filter_headers_(fault_filter_headers),
        runtime_(runtime), stats_{ALL_FAULT_FILTER_STATS(POOL_COUNTER_PREFIX(stats, stat_prefix))} {
  }

  uint64_t abort_enabled_;  // 0-100
  uint64_t abort_code_;     // HTTP or gRPC return codes
  uint64_t delay_enabled_;  // 0-100
  uint64_t delay_duration_; // in milliseconds
  const std::vector<Router::ConfigUtility::HeaderData> fault_filter_headers_;
  Runtime::Loader& runtime_;
  FaultFilterStats stats_;
};

typedef std::shared_ptr<const FaultFilterConfig> FaultFilterConfigPtr;

/**
 * A filter that is capable of faulting an entire request before dispatching it upstream.
 */
class FaultFilter : public StreamDecoderFilter {
public:
  FaultFilter(FaultFilterConfigPtr config);
  ~FaultFilter();

  static FaultFilterStats generateStats(const std::string& prefix, Stats::Store& store);

  FilterHeadersStatus decodeHeaders(HeaderMap& headers, bool end_stream) override;
  FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  FilterTrailersStatus decodeTrailers(HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) override;

private:
  void onResetStream();
  void resetInternalState();
  void postDelayInjection();

  FaultFilterConfigPtr config_;
  StreamDecoderFilterCallbacks* callbacks_{};
  Event::TimerPtr delay_timer_;
};

} // Http
