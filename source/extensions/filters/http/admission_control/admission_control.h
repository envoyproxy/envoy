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
#define ALL_ADMISSION_CONTROL_STATS(COUNTER, GAUGE)
  COUNTER(rq_rejected) \
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
  uint32_t samplingWindowSeconds() const { return sampling_window_seconds_; }
  double aggression() const { return aggression_; }
  uint32_t minRequestSamples() const { return min_request_samples_; }

private:
  Runtime::Loader& runtime_;
  const std::string stats_prefix_;
  TimeSource& time_source_;
  Runtime::FeatureFlag admission_control_feature_;
  uint32_t sampling_window_seconds_;
  double aggression_;
  uint32_t min_request_samples_;
};

using AdmissionControlFilterConfigSharedPtr =
    std::shared_ptr<const AdmissionControlFilterConfig>;

/**
 * A filter that probabilistically rejects requests based on upstream success-rate.
 */
class AdmissionControlFilter : public Http::PassThroughFilter,
                                  Logger::Loggable<Logger::Id::filter> {
public:
  AdmissionControlFilter(AdmissionControlFilterConfigSharedPtr config);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap&, bool) override;

  // Http::StreamEncoderFilter
  void encodeComplete() override;
  void onDestroy() override;

  bool shouldRejectRequest() const;

private:
  struct SuccessRateAggregate {
    uint32_t total_rq;
    uint32_t total_success;
  }

  AdmissionControlFilterConfigSharedPtr config_;
  std::unique_ptr<Cleanup> deferred_sample_task_;
  std::queue<std::pair<std::chrono::seconds, SuccessRateAggregate>> sr_aggregates_;
  std::atomic<uint32_t> current_total_rq;
  std::atomic<uint32_t> current_total_success;


};

} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
