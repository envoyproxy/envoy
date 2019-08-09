#pragma once

#include <chrono>

#include "envoy/config/filter/http/adaptive_concurrency/v2alpha/adaptive_concurrency.pb.h"
#include "envoy/config/filter/http/adaptive_concurrency/v2alpha/adaptive_concurrency.pb.validate.h"
#include "envoy/runtime/runtime.h"
#include "envoy/event/dispatcher.h"
#include "extensions/filters/http/adaptive_concurrency/concurrency_controller/concurrency_controller.h"
//@tallen
//#include "include/envoy/stats/histogram.h"

#include "absl/synchronization/mutex.h"
#include "absl/base/thread_annotations.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {
namespace ConcurrencyController {

enum class SampleAggregatePercentile {
  P50,
  P75,
  P90,
  P95,
  P99
};

class GradientControllerConfig {
public:
  GradientControllerConfig(
    const envoy::config::filter::http::adaptive_concurrency::v2alpha::GradientControllerConfig&
      proto_config);

  std::chrono::milliseconds min_rtt_calc_interval() const { return min_rtt_calc_interval_; }
  std::chrono::milliseconds sample_rtt_calc_interval() const { return sample_rtt_calc_interval_; }
  uint64_t max_concurrency_limit() const { return max_concurrency_limit_; }
  int starting_concurrency_limit() const { return starting_concurrency_limit_; }

private:
  // The measured request round-trip time under ideal conditions.
  std::chrono::milliseconds min_rtt_calc_interval_;

  // The measured sample round-trip time from the previous time window.
  std::chrono::milliseconds sample_rtt_calc_interval_;

  // The maximum allowed concurrency value.
  uint64_t max_concurrency_limit_;

  // Initial value for the concurrency limit.
  int starting_concurrency_limit_;

  // The percentile considered when aggregating latency samples.
  SampleAggregatePercentile sample_aggregate_percentile_;
};
typedef std::shared_ptr<GradientControllerConfig> GradientControllerConfigSharedPtr;

/**
 * A concurrency controller that implements the a variation of the Gradient algorithm to control the
 * allowed concurrency window.
 *
 * An ideal round-trip time (minRTT) is measured periodically (every min_rtt_calc_period_) by only
 * allowing a single outstanding request at a time and measuring the latency values. This is then
 * used in the calculation of a gradient value when the concurrency value is not fixed at 1, using
 * time-sampled latencies (sampleRTT):
 *
 *     gradient = minRTT / sampleRTT
 *
 * This gradient value is then used to periodically update the concurrency limit via:
 *
 *     limit = old_limit * gradient
 *     new_limit = limit + headroom
 *
 * The headroom value allows for request bursts and is configurable. The default is set to
 * sqrt(limit).
 *
 * TODO (tonya11en) when implemented, mention:
 *   - What is runtime configurable.
 *   - Pinning headroom to specific values.
 */
class GradientController : public ConcurrencyController {
public:
  GradientController(GradientControllerConfigSharedPtr config,
                     Event::Dispatcher& dispatcher,
                     Runtime::Loader& runtime,
                     std::string stats_prefix,
                     Stats::Scope& scope,
                     TimeSource& time_source);

  ~GradientController();

  // ConcurrencyController.
  RequestForwardingAction forwardingDecision() override;
  void recordLatencySample(const std::chrono::nanoseconds& rq_latency) override;

private:
  void recordLatencySampleForMinRTT(const std::chrono::nanoseconds& rq_latency);
  void calculateMinRTT();
  void updateConcurrencyLimit();
  uint32_t processLatencySamples() ABSL_EXCLUSIVE_LOCKS_REQUIRED(min_rtt_calculation_mtx_, limit_mtx_);

  GradientControllerConfigSharedPtr config_;
  Event::Dispatcher& dispatcher_;
  Runtime::Loader& runtime_;
  const std::string stats_prefix_;
  Stats::Scope& scope_;
  TimeSource& time_source_;

  std::chrono::nanoseconds min_rtt_ ABSL_GUARDED_BY(min_rtt_calculation_mtx_);
  absl::Mutex min_rtt_calculation_mtx_;
  absl::Mutex limit_mtx_;
  std::chrono::nanoseconds sample_rtt_ ABSL_GUARDED_BY(/*min_rtt_calculation_mtx_,*/ limit_mtx_);
  int num_rq_outstanding_ ABSL_GUARDED_BY(limit_mtx_);
  int concurrency_limit_ ABSL_GUARDED_BY(limit_mtx_);

  // TODO @tallen figure out the deal with this histogram and stats sinks.
  //Stats::Histogram latency_samples_;
  std::vector<uint32_t> latency_sample_hist_;

  Event::TimerPtr min_rtt_calc_timer_;
  Event::TimerPtr sample_reset_timer_;

};

} // namespace ConcurrencyController
} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
