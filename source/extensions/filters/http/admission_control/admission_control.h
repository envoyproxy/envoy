#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/time.h"
#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.h"
#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/filter_config.h"
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
#define ALL_ADMISSION_CONTROL_STATS(COUNTER) COUNTER(rq_rejected)

/**
 * Wrapper struct for admission control filter stats. @see stats_macros.h
 */
struct AdmissionControlStats {
  ALL_ADMISSION_CONTROL_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Thread-local object to track request counts and successes over a rolling time window. Request
 * data for the time window is kept recent via a circular buffer that phases out old request/success
 * counts when recording new samples.
 *
 * The lookback window for request samples is accurate up to a hard-coded 1-second granularity.
 * TODO (tonya11en): Allow the granularity to be configurable.
 */
class ThreadLocalController : public ThreadLocal::ThreadLocalObject {
public:
  ThreadLocalController(TimeSource& time_source, std::chrono::seconds sampling_window);
  virtual void recordSuccess() { recordRequest(true); }
  virtual void recordFailure() { recordRequest(false); }

  virtual uint32_t requestTotalCount() {
    maybeUpdateHistoricalData();
    return global_data_.requests;
  }
  virtual uint32_t requestSuccessCount() {
    maybeUpdateHistoricalData();
    return global_data_.successes;
  }

private:
  struct RequestData {
    RequestData() : requests(0), successes(0) {}
    uint32_t requests;
    uint32_t successes;
  };

  void recordRequest(const bool success);

  // Potentially remove any stale samples and record sample aggregates to the historical data.
  void maybeUpdateHistoricalData();

  TimeSource& time_source_;
  std::deque<std::pair<MonotonicTime, RequestData>> historical_data_;

  // Request data aggregated for the whole lookback window.
  RequestData global_data_;

  // The rolling time window size.
  std::chrono::seconds sampling_window_;
};

/**
 * Configuration for the admission control filter.
 */
class AdmissionControlFilterConfig {
public:
  using AdmissionControlProto =
      envoy::extensions::filters::http::admission_control::v3alpha::AdmissionControl;
  AdmissionControlFilterConfig(const AdmissionControlProto& proto_config, Runtime::Loader& runtime,
                               TimeSource& time_source, Runtime::RandomGenerator& random,
                               Stats::Scope& scope, ThreadLocal::SlotPtr&& tls);
  virtual ~AdmissionControlFilterConfig() {}

  virtual ThreadLocalController& getController() const {
    return tls_->getTyped<ThreadLocalController>();
  }

  Runtime::Loader& runtime() const { return runtime_; }
  Runtime::RandomGenerator& random() const { return random_; }
  bool filterEnabled() const { return admission_control_feature_.enabled(); }
  TimeSource& timeSource() const { return time_source_; }
  Stats::Scope& scope() const { return scope_; }
  double aggression() const;

private:
  Runtime::Loader& runtime_;
  TimeSource& time_source_;
  Runtime::RandomGenerator& random_;
  Stats::Scope& scope_;
  const ThreadLocal::SlotPtr tls_;
  Runtime::FeatureFlag admission_control_feature_;
  std::unique_ptr<Runtime::Double> aggression_;
};

using AdmissionControlFilterConfigSharedPtr = std::shared_ptr<const AdmissionControlFilterConfig>;

/**
 * A filter that probabilistically rejects requests based on upstream success-rate.
 */
class AdmissionControlFilter : public Http::PassThroughFilter,
                               Logger::Loggable<Logger::Id::filter> {
public:
  AdmissionControlFilter(AdmissionControlFilterConfigSharedPtr config,
                         const std::string& stats_prefix);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool end_stream) override;

private:
  static AdmissionControlStats generateStats(Stats::Scope& scope, const std::string& prefix) {
    return {ALL_ADMISSION_CONTROL_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }

  bool shouldRejectRequest() const;

  AdmissionControlFilterConfigSharedPtr config_;
  AdmissionControlStats stats_;
  std::unique_ptr<Cleanup> deferred_sample_task_;
};

} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
