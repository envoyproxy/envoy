#pragma once

#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats_macros.h"

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
 * HTTP request headers.
 */
struct FaultFilterHeaders {
  FaultFilterHeaders(const Http::LowerCaseString& name, const std::string& value)
      : name_(name), value_(value) {}

  const Http::LowerCaseString name_;
  const std::string value_;
};

/**
 * Configuration for the fault filter.
 */
struct FaultFilterConfig {
  FaultFilterConfig(const std::string& stat_prefix, Stats::Store& stats,
                    Runtime::RandomGenerator& random, uint64_t abort_code,
                    uint64_t abort_probability, uint64_t delay_probability,
                    std::chrono::milliseconds delay_duration,
                    std::vector<FaultFilterHeaders> fault_filter_headers)
      : random_(random), modulo_base_(100), abort_code_(abort_code),
        abort_probability_(abort_probability), delay_probability_(delay_probability),
        delay_duration_(delay_duration), fault_filter_headers_(fault_filter_headers),
        stats_{ALL_FAULT_FILTER_STATS(POOL_COUNTER_PREFIX(stats, stat_prefix))} {}

  Runtime::RandomGenerator& random_;
  uint64_t modulo_base_; // % of traffic to inject faults on, in increments of 1%. Hardcoded to 100,
                         // until we need higher precision.
  uint64_t abort_code_;  // HTTP or gRPC return codes
  uint64_t abort_probability_;               // 0-100
  uint64_t delay_probability_;               // 0-100
  std::chrono::milliseconds delay_duration_; // in milliseconds
  std::vector<FaultFilterHeaders> fault_filter_headers_;
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
  bool matches(const Http::HeaderMap& headers) const;

  FaultFilterConfigPtr config_;
  StreamDecoderFilterCallbacks* callbacks_{};
  Event::TimerPtr delay_timer_;
};
} // Http
