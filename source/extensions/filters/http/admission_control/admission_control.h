#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/time.h"
#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.h"
#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/cleanup.h"
#include "common/runtime/runtime_protos.h"

#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {

/**
 * All stats for the admission control filter.
 */
#define ALL_ADMISSION_CONTROL_STATS(COUNTER, GAUGE)                                                \
  COUNTER(rq_rejected)                                                                             \
  GAUGE(success_rate_pct, Accumulate)

/**
 * Wrapper struct for admission control filter stats. @see stats_macros.h
 */
struct AdmissionControlStats {
  ALL_ADMISSION_CONTROL_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Configuration for the admission control filter.
 */
class AdmissionControlFilterConfig {
public:
  AdmissionControlFilterConfig(
      const envoy::extensions::filters::http::admission_control::v3alpha::AdmissionControl&
          proto_config,
      Runtime::Loader& runtime, std::string stats_prefix, Stats::Scope& scope,
      TimeSource& time_source);

  Runtime::Loader& runtime() { return runtime_; }
  bool filterEnabled() const { return admission_control_feature_.enabled(); }
  TimeSource& timeSource() const { return time_source_; }
  std::chrono::seconds samplingWindow() const { return sampling_window_; }
  double aggression() const { return aggression_; }
  uint32_t minRequestSamples() const { return min_request_samples_; }

private:
  Runtime::Loader& runtime_;
  const std::string stats_prefix_;
  TimeSource& time_source_;
  Runtime::FeatureFlag admission_control_feature_;
  std::chrono::seconds sampling_window_;
  double aggression_;
  uint32_t min_request_samples_;
};

using AdmissionControlFilterConfigSharedPtr = std::shared_ptr<const AdmissionControlFilterConfig>;

/**
 * Thread-local object to request counts and successes over a rolling time window. Request data for
 * the time window is kept recent via a circular buffer that phases out old request/success counts
 * when recording new samples.
 *
 * The lookback window for request samples is accurate up to a hard-coded 1-second granularity.
 * TODO (tonya11en): Allow the granularity to be configurable.
 */
class AdmissionControlState {
public:
  AdmissionControlState(TimeSource& time_source, AdmissionControlFilterConfigSharedPtr config,
                        Runtime::RandomGenerator& random);
  void recordRequest(const bool success);
  bool shouldRejectRequest();

private:
  struct RequestData {
    uint32_t requests;
    uint32_t successes;
  };

  void maybeUpdateHistoricalData();

  TimeSource& time_source_;
  Runtime::RandomGenerator& random_;
  AdmissionControlFilterConfigSharedPtr config_;
  std::deque<std::pair<MonotonicTime, RequestData>> historical_data_;

  // Request data for the current time range.
  RequestData local_data_;

  // Request data aggregated for the whole lookback window.
  RequestData global_data_;
};

using AdmissionControlStateSharedPtr = std::shared_ptr<AdmissionControlState>;

/**
 * A filter that probabilistically rejects requests based on upstream success-rate.
 */
class AdmissionControlFilter : public Http::PassThroughFilter,
                               Logger::Loggable<Logger::Id::filter> {
public:
  AdmissionControlFilter(AdmissionControlFilterConfigSharedPtr config,
                         AdmissionControlStateSharedPtr state);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap&, bool) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap& headers, bool end_stream) override;

private:
  AdmissionControlFilterConfigSharedPtr config_;
  // TODO @tallen thread local
  AdmissionControlStateSharedPtr state_;
};

} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
