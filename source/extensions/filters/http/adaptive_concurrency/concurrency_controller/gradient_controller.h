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
#define ALL_GRADIENT_CONTROLLER_STATS(GAUGE)                                                       \
  GAUGE(concurrency_limit, NeverImport)                                                            \
  GAUGE(gradient, NeverImport)                                                                     \
  GAUGE(burst_queue_size, NeverImport)                                                             \
  GAUGE(min_rtt_msecs, NeverImport)

/**
 * Wrapper struct for gradient controller stats. @see stats_macros.h
 */
struct GradientControllerStats {
  ALL_GRADIENT_CONTROLLER_STATS(GENERATE_GAUGE_STRUCT)
};

class GradientControllerConfig : public Logger::Loggable<Logger::Id::filter> {
public:
  GradientControllerConfig(
      const envoy::config::filter::http::adaptive_concurrency::v2alpha::GradientControllerConfig&
          proto_config);

  std::chrono::milliseconds minRTTCalcInterval() const { return min_rtt_calc_interval_; }
  std::chrono::milliseconds sampleRTTCalcInterval() const { return sample_rtt_calc_interval_; }
  uint32_t maxConcurrencyLimit() const { return max_concurrency_limit_; }
  uint32_t minRTTAggregateRequestCount() const { return min_rtt_aggregate_request_count_; }
  double maxGradient() const { return max_gradient_; }
  double sampleAggregatePercentile() const { return sample_aggregate_percentile_; }

private:
  // The measured request round-trip time under ideal conditions.
  const std::chrono::milliseconds min_rtt_calc_interval_;

  // The measured sample round-trip time from the previous time window.
  const std::chrono::milliseconds sample_rtt_calc_interval_;

  // The maximum allowed concurrency value.
  const uint32_t max_concurrency_limit_;

  // The number of requests to aggregate/sample during the minRTT recalculation.
  const uint32_t min_rtt_aggregate_request_count_;

  // The maximum value the gradient may take.
  const double max_gradient_;

  // The percentile value considered when processing samples.
  const double sample_aggregate_percentile_;
};
using GradientControllerConfigSharedPtr = std::shared_ptr<GradientControllerConfig>;

/**
 * A concurrency controller that implements a variation of the Gradient algorithm described in:
 *
 * https://medium.com/@NetflixTechBlog/performance-under-load-3e6fa9a60581
 *
 * This is used to control the allowed request concurrency limit in the adaptive concurrency control
 * filter.
 *
 * The algorithm:
 * ==============
 * An ideal round-trip time (minRTT) is measured periodically by only allowing a single outstanding
 * request at a time and measuring the round-trip time to the upstream. This information is then
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
 * unconfigurable and is set to the square-root of the new limit.
 *
 * Sampling:
 * =========
 * The controller makes use of latency samples to either determine the minRTT or the sampleRTT which
 * is used to periodically update the concurrency limit. Each calculation occurs at separate
 * configurable frequencies and they may not occur at the same time. To prevent this, there exists a
 * concept of mutually exclusive sampling windows.
 *
 * When the gradient controller is instantiated, it starts inside of a minRTT calculation window
 * (indicated by inMinRTTSamplingWindow() returning true) and the concurrency limit is pinned to 1.
 * This window lasts until the configured number of requests is received, the minRTT value is
 * updated, and the minRTT value is set by a single worker thread. To prevent sampleRTT calculations
 * from triggering during this window, the update window mutex is held. Since it's necessary for a
 * worker thread to know which update window update window mutex is held for, they check the state
 * of inMinRTTSamplingWindow() after each sample. When the minRTT calculation is complete, a timer
 * is set to trigger the next minRTT sampling window by the worker thread who updates the minRTT
 * value.
 *
 * If the controller is not in a minRTT sampling window, it's possible that the controller is in a
 * sampleRTT calculation window. In this, all of the latency samples are consolidated into a
 * configurable quantile value to represent the measured latencies. This quantile value sets
 * sampleRTT and the concurrency limit is updated as described in the algorithm section above.
 *
 * When not in a sampling window, the controller is simply servicing the adaptive concurrency filter
 * via the public functions.
 *
 * Locking:
 * ========
 * There are 2 mutually exclusive calculation windows, so the sample mutation mutex is held to
 * prevent the overlap of these windows. It is necessary for a worker thread to know specifically if
 * the controller is inside of a minRTT recalculation window during the recording of a latency
 * sample, so this extra bit of information is stored in inMinRTTSamplingWindow().
 */
class GradientController : public ConcurrencyController {
public:
  GradientController(GradientControllerConfig config, Event::Dispatcher& dispatcher,
                     Runtime::Loader& runtime, const std::string& stats_prefix,
                     Stats::Scope& scope);

  // ConcurrencyController.
  RequestForwardingAction forwardingDecision() override;
  void recordLatencySample(std::chrono::nanoseconds rq_latency) override;
  void cancelLatencySample() override;
  uint32_t concurrencyLimit() const override { return concurrency_limit_.load(); }

private:
  static GradientControllerStats generateStats(Stats::Scope& scope,
                                               const std::string& stats_prefix);
  void updateMinRTT();
  std::chrono::microseconds processLatencySamplesAndClear()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(sample_mutation_mtx_);
  uint32_t calculateNewLimit() ABSL_EXCLUSIVE_LOCKS_REQUIRED(sample_mutation_mtx_);
  void enterMinRTTSamplingWindow();
  bool inMinRTTSamplingWindow() const { return deferred_limit_value_.load() > 0; }
  void resetSampleWindow() ABSL_EXCLUSIVE_LOCKS_REQUIRED(sample_mutation_mtx_);
  void updateConcurrencyLimit(const uint32_t new_limit) {
    std::cout << "@tallen updating limit to " << new_limit << std::endl;
    concurrency_limit_.store(new_limit);
    stats_.concurrency_limit_.set(concurrency_limit_.load());
  }

  const GradientControllerConfig config_;
  Event::Dispatcher& dispatcher_;
  Stats::Scope& scope_;
  GradientControllerStats stats_;

  // Protects data related to latency sampling and RTT values. In addition to protecting the latency
  // sample histogram, the mutex ensures that the minRTT calculation window and the sample window
  // (where the new concurrency limit is determined) do not overlap.
  absl::Mutex sample_mutation_mtx_;

  // Stores the value of the concurrency limit prior to entering the minRTT update window. If this
  // is non-zero, then we are actively in the minRTT sampling window.
  std::atomic<uint32_t> deferred_limit_value_;

  // Stores the expected upstream latency value under ideal conditions. This is the numerator in the
  // gradient value explained above.
  std::chrono::nanoseconds min_rtt_;
  std::chrono::nanoseconds sample_rtt_ ABSL_GUARDED_BY(sample_mutation_mtx_);

  // Tracks the count of requests that have been forwarded whose replies have
  // not been sampled yet. Atomicity is required because this variable is used to make the
  // forwarding decision without locking.
  std::atomic<uint32_t> num_rq_outstanding_;

  // Stores the current concurrency limit. Atomicity is required because this variable is used to
  // make the forwarding decision without locking.
  std::atomic<uint32_t> concurrency_limit_;

  // Stores all sampled latencies and provides percentile estimations when using the sampled data to
  // calculate a new concurrency limit.
  std::unique_ptr<histogram_t, decltype(&hist_free)>
      latency_sample_hist_ ABSL_GUARDED_BY(sample_mutation_mtx_);

  Event::TimerPtr min_rtt_calc_timer_;
  Event::TimerPtr sample_reset_timer_;
};
using GradientControllerSharedPtr = std::shared_ptr<GradientController>;

} // namespace ConcurrencyController
} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
