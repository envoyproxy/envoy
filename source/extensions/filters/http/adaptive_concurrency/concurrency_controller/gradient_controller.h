#pragma once

#include <chrono>
#include <vector>

#include "envoy/config/filter/http/adaptive_concurrency/v2alpha/adaptive_concurrency.pb.h"
#include "envoy/config/filter/http/adaptive_concurrency/v2alpha/adaptive_concurrency.pb.validate.h"
#include "envoy/event/dispatcher.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats_macros.h"

#include "extensions/filters/http/adaptive_concurrency/concurrency_controller/concurrency_controller.h"

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "circllhist.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {
namespace ConcurrencyController {

/**
 * All stats for the gradient controller.
 */
// clang-format off
#define ALL_GRADIENT_CONTROLLER_STATS(COUNTER, GAUGE) \
  GAUGE(concurrency_limit, Accumulate)  \
  GAUGE(gradient, Accumulate)  \
  GAUGE(burst_queue_size, Accumulate)  \
  GAUGE(rq_outstanding, Accumulate)  \
  GAUGE(min_rtt_msecs, Accumulate)
// clang-format on

/**
 * Wrapper struct for gradient controller stats. @see stats_macros.h
 */
struct GradientControllerStats {
  ALL_GRADIENT_CONTROLLER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

class GradientControllerConfig {
public:
  GradientControllerConfig(
      const envoy::config::filter::http::adaptive_concurrency::v2alpha::GradientControllerConfig&
          proto_config);

  // Accessors.
  std::chrono::milliseconds min_rtt_calc_interval() const { return min_rtt_calc_interval_; }
  std::chrono::milliseconds sample_rtt_calc_interval() const { return sample_rtt_calc_interval_; }
  uint64_t max_concurrency_limit() const { return max_concurrency_limit_; }
  int min_rtt_aggregate_request_count() const { return min_rtt_aggregate_request_count_; }
  double max_gradient() const { return max_gradient_; }
  double sample_aggregate_percentile() const { return sample_aggregate_percentile_; }

private:
  // The measured request round-trip time under ideal conditions.
  std::chrono::milliseconds min_rtt_calc_interval_;

  // The measured sample round-trip time from the previous time window.
  std::chrono::milliseconds sample_rtt_calc_interval_;

  // The maximum allowed concurrency value.
  uint64_t max_concurrency_limit_;

  // The number of requests to aggregate/sample during the minRTT recalculation.
  int min_rtt_aggregate_request_count_;

  // The maximum value the gradient may take.
  double max_gradient_;

  // The percentile value considered when processing samples.
  double sample_aggregate_percentile_;
};
typedef std::shared_ptr<GradientControllerConfig> GradientControllerConfigSharedPtr;

/**
 * A concurrency controller that implements the a variation of the Gradient algorithm to control the
 * allowed concurrency window.
 *
 * An ideal round-trip time (minRTT) is measured periodically by only allowing a single outstanding
 * request at a time and measuring the round-trip time to the listener. This information is then
 * used in the calculation of a number called the gradient, using time-sampled latencies
 * (sampleRTT):
 *
 *     gradient = minRTT / sampleRTT
 *
 * This gradient value has a useful property, such that it decreases as the sampled latencies
 * increase. The value is then used to periodically update the concurrency limit via:
 *
 *     limit = old_limit * gradient
 *     new_limit = limit + headroom
 *
 * The headroom value allows for request bursts and is also the driving factor behind increasing the
 * concurrency limit when the sampleRTT is in the same ballpark as the minRTT. This value must be
 * present in the calculation, since it forces the concurrency limit to increase until there is a
 * deviation from the minRTT latency. In its absence, the concurrency limit could remain stagnant at
 * an unnecessarily small value if sampleRTT ~= minRTT. Therefore, the headroom value is
 * unconfigurable and takes the form of sqrt(limit).
 */
class GradientController : public ConcurrencyController {
public:
  GradientController(GradientControllerConfigSharedPtr config, Event::Dispatcher& dispatcher,
                     Runtime::Loader& runtime, std::string stats_prefix, Stats::Scope& scope);

  ~GradientController();

  // ConcurrencyController.
  RequestForwardingAction forwardingDecision() override;
  void recordLatencySample(const std::chrono::nanoseconds& rq_latency) override;
  int concurrencyLimit() const override { return concurrency_limit_.load(); }

private:
  static GradientControllerStats generateStats(Stats::Scope& scope,
                                               const std::string& stats_prefix);
  void updateMinRTT();
  std::chrono::microseconds processLatencySamplesAndClear()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(latency_sample_mtx_);
  int calculateNewLimit() ABSL_EXCLUSIVE_LOCKS_REQUIRED(update_window_mtx_);
  void setMinRTTSamplingWindow() ABSL_EXCLUSIVE_LOCKS_REQUIRED(update_window_mtx_);
  void resetSampleWindow() ABSL_EXCLUSIVE_LOCKS_REQUIRED(update_window_mtx_);

  GradientControllerConfigSharedPtr config_;
  Event::Dispatcher& dispatcher_;
  Stats::Scope& scope_;
  GradientControllerStats stats_;

  absl::Mutex update_window_mtx_;
  std::chrono::nanoseconds min_rtt_;
  std::atomic<bool> recalculating_min_rtt_;
  absl::Mutex latency_sample_mtx_ ABSL_ACQUIRED_AFTER(update_window_mtx_);
  std::chrono::nanoseconds sample_rtt_ ABSL_GUARDED_BY(update_window_mtx_);

  std::atomic<int> num_rq_outstanding_;
  std::atomic<int> concurrency_limit_;

  histogram_t* latency_sample_hist_ ABSL_GUARDED_BY(latency_sample_mtx_);

  Event::TimerPtr min_rtt_calc_timer_;
  Event::TimerPtr sample_reset_timer_;
};
typedef std::shared_ptr<GradientController> GradientControllerSharedPtr;

} // namespace ConcurrencyController
} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
