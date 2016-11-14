#pragma once

#include "envoy/http/filter.h"
#include "envoy/stats/stats_macros.h"

#include <random>

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
  FaultFilterStats stats_;
  uint32_t delay_duration_;    // in milliseconds
  uint32_t delay_probability_; // 0-100
  uint32_t abort_code_;        // HTTP or gRPC return codes
  uint32_t abort_probability_; // 0-100
  std::vector<FaultFilterHeaders> fault_filter_headers_;
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
  std::random_device rd_;
  std::mt19937 generator_;
  std::uniform_int_distribution<uint32_t> prob_dist_{
      std::uniform_int_distribution<uint32_t>(1, 100)};
};
} // Http
