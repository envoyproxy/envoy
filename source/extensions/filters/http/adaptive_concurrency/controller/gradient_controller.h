#pragma once

#include <chrono>
#include <vector>

#include "envoy/common/random_generator.h"
#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/filters/http/adaptive_concurrency/v3/adaptive_concurrency.pb.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/thread_synchronizer.h"
#include "source/extensions/filters/http/adaptive_concurrency/controller/controller.h"

#include "absl/base/thread_annotations.h"
#include "absl/strings/numbers.h"
#include "absl/synchronization/mutex.h"
#include "circllhist.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {
namespace Controller {

/**
 * All stats for the gradient controller.
 */
#define ALL_GRADIENT_CONTROLLER_STATS(COUNTER, GAUGE)                                              \
  COUNTER(rq_blocked)                                                                              \
  GAUGE(burst_queue_size, NeverImport)                                                             \
  GAUGE(concurrency_limit, NeverImport)                                                            \
  GAUGE(gradient, NeverImport)                                                                     \
  GAUGE(min_rtt_calculation_active, Accumulate)                                                    \
  GAUGE(min_rtt_msecs, NeverImport)                                                                \
  GAUGE(sample_rtt_msecs, NeverImport)

/**
 * Wrapper struct for gradient controller stats. @see stats_macros.h
 */
struct GradientControllerStats {
  ALL_GRADIENT_CONTROLLER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)

  static GradientControllerStats generateStats(Stats::Scope& scope,
                                               const std::string& stats_prefix);
};

class GradientControllerConfig {
public:
  GradientControllerConfig(std::chrono::milliseconds sample_rtt_calc_interval,
                           uint64_t max_concurrency_limit, double sample_aggregate_percentile,
                           uint64_t min_concurrency, double min_rtt_buffer_pct,
                           Runtime::Loader& runtime)
      : runtime_(runtime), sample_rtt_calc_interval_(sample_rtt_calc_interval),
        max_concurrency_limit_(max_concurrency_limit),
        sample_aggregate_percentile_(sample_aggregate_percentile),
        min_concurrency_(min_concurrency), min_rtt_buffer_pct_(min_rtt_buffer_pct) {}
  virtual ~GradientControllerConfig() = default;

  std::chrono::milliseconds sampleRTTCalcInterval() const {
    const auto ms = runtime_.snapshot().getInteger(RuntimeKeys::get().SampleRTTCalcIntervalKey,
                                                   sample_rtt_calc_interval_.count());
    return std::chrono::milliseconds(ms);
  }

  uint32_t maxConcurrencyLimit() const {
    return runtime_.snapshot().getInteger(RuntimeKeys::get().MaxConcurrencyLimitKey,
                                          max_concurrency_limit_);
  }

  // The percentage is normalized to the range [0.0, 1.0].
  double sampleAggregatePercentile() const {
    const double val = runtime_.snapshot().getDouble(
        RuntimeKeys::get().SampleAggregatePercentileKey, sample_aggregate_percentile_);
    return std::max(0.0, std::min(val, 100.0)) / 100.0;
  }

  uint32_t minConcurrency() const {
    return runtime_.snapshot().getInteger(RuntimeKeys::get().MinConcurrencyKey, min_concurrency_);
  }

  // The percentage is normalized to the range [0.0, 1.0].
  double minRTTBufferPercent() const {
    const double val = runtime_.snapshot().getDouble(RuntimeKeys::get().MinRTTBufferPercentKey,
                                                     min_rtt_buffer_pct_);
    return std::max(0.0, std::min(val, 100.0)) / 100.0;
  }

  Runtime::Loader& runtime() const { return runtime_; }

protected:
  class RuntimeKeyValues {
  public:
    const std::string MinRTTCalcIntervalKey =
        "adaptive_concurrency.gradient_controller.min_rtt_calc_interval_ms";
    const std::string SampleRTTCalcIntervalKey =
        "adaptive_concurrency.gradient_controller.sample_rtt_calc_interval_ms";
    const std::string MaxConcurrencyLimitKey =
        "adaptive_concurrency.gradient_controller.max_concurrency_limit";
    const std::string MinRTTAggregateRequestCountKey =
        "adaptive_concurrency.gradient_controller.min_rtt_aggregate_request_count";
    const std::string SampleAggregatePercentileKey =
        "adaptive_concurrency.gradient_controller.sample_aggregate_percentile";
    const std::string JitterPercentKey = "adaptive_concurrency.gradient_controller.jitter";
    const std::string MinConcurrencyKey =
        "adaptive_concurrency.gradient_controller.min_concurrency";
    const std::string MinRTTBufferPercentKey =
        "adaptive_concurrency.gradient_controller.min_rtt_buffer";
  };

  using RuntimeKeys = ConstSingleton<RuntimeKeyValues>;

private:
  Runtime::Loader& runtime_;
  const std::chrono::milliseconds sample_rtt_calc_interval_;
  const uint64_t max_concurrency_limit_;
  const double sample_aggregate_percentile_;
  const uint64_t min_concurrency_;
  // The amount added to the measured minRTT as a hedge against natural variability in latency.
  const double min_rtt_buffer_pct_;
};

class DynamicGradientControllerConfig : public Logger::Loggable<Logger::Id::filter>,
                                        public GradientControllerConfig {
public:
  DynamicGradientControllerConfig(
      const envoy::extensions::filters::http::adaptive_concurrency::v3::GradientControllerConfig&
          proto_config,
      Runtime::Loader& runtime);

  std::chrono::milliseconds minRTTCalcInterval() const {
    const auto ms = GradientControllerConfig::runtime().snapshot().getInteger(
        GradientControllerConfig::RuntimeKeys::get().MinRTTCalcIntervalKey,
        min_rtt_calc_interval_.count());
    return std::chrono::milliseconds(ms);
  }

  uint32_t minRTTAggregateRequestCount() const {
    return GradientControllerConfig::runtime().snapshot().getInteger(
        GradientControllerConfig::RuntimeKeys::get().MinRTTAggregateRequestCountKey,
        min_rtt_aggregate_request_count_);
  }

  // The percentage is normalized to the range [0.0, 1.0].
  double jitterPercent() const {
    const double val = GradientControllerConfig::runtime().snapshot().getDouble(
        GradientControllerConfig::RuntimeKeys::get().JitterPercentKey, jitter_pct_);
    return std::max(0.0, std::min(val, 100.0)) / 100.0;
  }

private:
  // The measured request round-trip time under ideal conditions.
  const std::chrono::milliseconds min_rtt_calc_interval_;

  // Randomized time delta added to the start of the minRTT calculation window.
  const double jitter_pct_;

  // The number of requests to aggregate/sample during the minRTT recalculation.
  const uint32_t min_rtt_aggregate_request_count_;
};
using DynamicGradientControllerConfigSharedPtr = std::shared_ptr<DynamicGradientControllerConfig>;

class PinnedGradientControllerConfig : public Logger::Loggable<Logger::Id::filter>,
                                       public GradientControllerConfig {
public:
  PinnedGradientControllerConfig(const envoy::extensions::filters::http::adaptive_concurrency::v3::
                                     PinnedGradientControllerConfig& proto_config,
                                 Runtime::Loader& runtime);

  std::chrono::nanoseconds minRTT() const { return min_rtt_; }

private:
  // The fixed minRTT value.
  const std::chrono::nanoseconds min_rtt_;
};
using PinnedGradientControllerConfigSharedPtr = std::shared_ptr<PinnedGradientControllerConfig>;

class GradientController : public ConcurrencyController {
public:
  GradientController(GradientControllerConfig& config, Event::Dispatcher& dispatcher,
                     const std::string& stats_prefix, Stats::Scope& scope, TimeSource& time_source);

  // ConcurrencyController
  RequestForwardingAction forwardingDecision() override;
  void recordLatencySample(MonotonicTime rq_send_time) override;
  void cancelLatencySample() override;
  uint32_t concurrencyLimit() const override { return concurrency_limit_.load(); }

  // Used in unit tests to validate worker thread interactions.
  Thread::ThreadSynchronizer& synchronizer() { return synchronizer_; }

protected:
  virtual bool shouldSampleRTT() const { return true; }
  virtual bool shouldRecordSample(MonotonicTime /*MonotonicTime*/) const { return true; }
  virtual void recalculateAfterSample() {}
  virtual std::chrono::nanoseconds minRTT() const = 0;
  virtual void processConcurrencyLimitUpdate(int /*consecutive_min_concurrency_set*/) {}

  absl::Mutex& sampleMutationMutex() { return sample_mutation_mtx_; }
  Event::Dispatcher& dispatcher() { return dispatcher_; }
  GradientControllerStats& stats() { return stats_; }
  TimeSource& timeSource() { return time_source_; }
  void updateConcurrencyLimit(const uint32_t new_limit)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(sample_mutation_mtx_);
  std::chrono::microseconds processLatencySamplesAndClear()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(sample_mutation_mtx_);
  uint32_t numSamples() ABSL_EXCLUSIVE_LOCKS_REQUIRED(sample_mutation_mtx_);
  void clearSamples() ABSL_EXCLUSIVE_LOCKS_REQUIRED(sample_mutation_mtx_);
  void enableSamplingTimer() { sample_reset_timer_->enableTimer(config_.sampleRTTCalcInterval()); }

private:
  uint32_t calculateNewLimit() ABSL_EXCLUSIVE_LOCKS_REQUIRED(sample_mutation_mtx_);
  void resetSampleWindow() ABSL_EXCLUSIVE_LOCKS_REQUIRED(sample_mutation_mtx_);

  GradientControllerConfig config_;
  Event::Dispatcher& dispatcher_;
  Stats::Scope& scope_;
  GradientControllerStats stats_;
  TimeSource& time_source_;

  // Protects data related to latency sampling and RTT values. In addition to protecting the latency
  // sample histogram, the mutex ensures that the minRTT calculation window and the sample window
  // (where the new concurrency limit is determined) do not overlap.
  absl::Mutex sample_mutation_mtx_;

  // Stores the aggregated sampled latencies for use in the gradient calculation.
  std::chrono::nanoseconds sample_rtt_ ABSL_GUARDED_BY(sample_mutation_mtx_);

  // Tracks the count of requests that have been forwarded whose replies have
  // not been sampled yet. Atomicity is required because this variable is used to make the
  // forwarding decision without locking.
  std::atomic<uint32_t> num_rq_outstanding_;

  // Stores the current concurrency limit. Atomicity is required because this variable is used to
  // make the forwarding decision without locking.
  std::atomic<uint32_t> concurrency_limit_;

  // Tracks the number of consecutive times that the concurrency limit is set to the minimum. This
  // is used to determine whether the controller should trigger an additional minRTT measurement
  // after remaining at the minimum limit for too long.
  uint32_t consecutive_min_concurrency_set_ ABSL_GUARDED_BY(sample_mutation_mtx_);

  // Stores all sampled latencies and provides percentile estimations when using the sampled data to
  // calculate a new concurrency limit.
  std::unique_ptr<histogram_t, decltype(&hist_free)>
      latency_sample_hist_ ABSL_GUARDED_BY(sample_mutation_mtx_);

  Event::TimerPtr sample_reset_timer_;

  // Used for testing only.
  Thread::ThreadSynchronizer synchronizer_;
};

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
 * An ideal round-trip time (minRTT) is measured periodically by only allowing a small number of
 * outstanding requests at a time and measuring the round-trip time to the upstream. This
 * information is then used in the calculation of a number called the gradient, using time-sampled
 * latencies (sampleRTT):
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
 * (indicated by inMinRTTSamplingWindow() returning true) and the concurrency limit is pinned to the
 * configured min_concurrency. This window lasts until the configured number of requests is
 * received, the minRTT value is updated, and the minRTT value is set by a single worker thread. To
 * prevent sampleRTT calculations from triggering during this window, the update window mutex is
 * held. Since it's necessary for a worker thread to know which update window update window mutex is
 * held for, they check the state of inMinRTTSamplingWindow() after each sample. When the minRTT
 * calculation is complete, a timer is set to trigger the next minRTT sampling window by the worker
 * thread who updates the minRTT value.
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
class DynamicGradientController : public GradientController {
public:
  DynamicGradientController(DynamicGradientControllerConfig config, Event::Dispatcher& dispatcher,
                            const std::string& stats_prefix, Stats::Scope& scope,
                            Random::RandomGenerator& random, TimeSource& time_source);

  // True if there is a minRTT sampling window active.
  bool inMinRTTSamplingWindow() const { return deferred_limit_value_.load() > 0; }

  // GradientController.
  bool shouldSampleRTT() const override {
    // The minRTT sampling window started since the sample reset timer was enabled last. Since the
    // minRTT value is being calculated, let's not initiate a new sample to avoid blocking the
    // dispatcher thread and rely on it being re-triggered again as part of the minRTT calculation.
    return !inMinRTTSamplingWindow();
  }
  bool shouldRecordSample(MonotonicTime rq_send_time) const override {
    // Disregard samples from requests started in the previous minRTT window
    // by only sampling requests after the window finished.
    return rq_send_time >= min_rtt_epoch_;
  }
  void processConcurrencyLimitUpdate(int consecutive_min_concurrency_set) override;
  std::chrono::nanoseconds minRTT() const override { return min_rtt_; }
  void recalculateAfterSample();

private:
  void enterMinRTTSamplingWindow();
  std::chrono::milliseconds applyJitter(std::chrono::milliseconds interval,
                                        double jitter_pct) const;

  const DynamicGradientControllerConfig config_;
  Random::RandomGenerator& random_;

  // Stores the value of the concurrency limit prior to entering the minRTT update window. If this
  // is non-zero, then we are actively in the minRTT sampling window.
  std::atomic<uint32_t> deferred_limit_value_;

  // Stores the expected upstream latency value under ideal conditions with the added buffer to
  // account for variable latencies. This is the numerator in the gradient value.
  std::chrono::nanoseconds min_rtt_;

  // We will disregard sampling any requests admitted before this timestamp to prevent sampling
  // requests admitted before the start of a minRTT window and potentially skewing the minRTT.
  MonotonicTime min_rtt_epoch_;

  Event::TimerPtr min_rtt_calc_timer_;
};
using DynamicGradientControllerSharedPtr = std::shared_ptr<DynamicGradientController>;

/**
 * A concurrency controller similar to `DynamicGradientController`, except minRTT is fixed.
 */
class PinnedGradientController : public GradientController {
public:
  PinnedGradientController(PinnedGradientControllerConfig config, Event::Dispatcher& dispatcher,
                           const std::string& stats_prefix, Stats::Scope& scope,
                           TimeSource& time_source);

  // GradientController.
  std::chrono::nanoseconds minRTT() const override { return config_.minRTT(); }

private:
  const PinnedGradientControllerConfig config_;
};
using PinnedGradientControllerSharedPtr = std::shared_ptr<PinnedGradientController>;

} // namespace Controller
} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
