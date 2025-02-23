#include "source/extensions/filters/http/adaptive_concurrency/controller/gradient_controller.h"

#include <atomic>
#include <chrono>

#include "envoy/common/random_generator.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/filters/http/adaptive_concurrency/v3/adaptive_concurrency.pb.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats.h"

#include "source/common/common/cleanup.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/adaptive_concurrency/controller/controller.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {
namespace Controller {

DynamicGradientControllerConfig::DynamicGradientControllerConfig(
    const envoy::extensions::filters::http::adaptive_concurrency::v3::GradientControllerConfig&
        proto_config,
    Runtime::Loader& runtime)
    : GradientControllerConfig::GradientControllerConfig(
          std::chrono::milliseconds(DurationUtil::durationToMilliseconds(
              proto_config.concurrency_limit_params().concurrency_update_interval())),
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config.concurrency_limit_params(),
                                          max_concurrency_limit, 1000),
          PROTOBUF_PERCENT_TO_DOUBLE_OR_DEFAULT(proto_config, sample_aggregate_percentile, 50),
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config.min_rtt_calc_params(), min_concurrency, 3),
          PROTOBUF_PERCENT_TO_DOUBLE_OR_DEFAULT(proto_config.min_rtt_calc_params(), buffer, 25),
          runtime),
      min_rtt_calc_interval_(std::chrono::milliseconds(
          DurationUtil::durationToMilliseconds(proto_config.min_rtt_calc_params().interval()))),
      jitter_pct_(
          PROTOBUF_PERCENT_TO_DOUBLE_OR_DEFAULT(proto_config.min_rtt_calc_params(), jitter, 15)),
      min_rtt_aggregate_request_count_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config.min_rtt_calc_params(), request_count, 50)) {}

GradientController::GradientController(GradientControllerConfig& config,
                                       Event::Dispatcher& dispatcher,
                                       const std::string& stats_prefix, Stats::Scope& scope,
                                       TimeSource& time_source)
    : config_(config), dispatcher_(dispatcher), scope_(scope),
      stats_(GradientControllerStats::generateStats(scope_, stats_prefix)),
      time_source_(time_source), num_rq_outstanding_(0),
      concurrency_limit_(config_.minConcurrency()),
      latency_sample_hist_(hist_fast_alloc(), hist_free) {
  sample_reset_timer_ = dispatcher_.createTimer([this]() -> void {
    if (!shouldSampleRTT()) {
      return;
    }

    {
      absl::MutexLock ml(&sample_mutation_mtx_);
      resetSampleWindow();
    }

    enableSamplingTimer();
  });
  stats_.concurrency_limit_.set(config_.minConcurrency());
}

DynamicGradientController::DynamicGradientController(DynamicGradientControllerConfig config,
                                                     Event::Dispatcher& dispatcher,
                                                     const std::string& stats_prefix,
                                                     Stats::Scope& scope,
                                                     Random::RandomGenerator& random,
                                                     TimeSource& time_source)
    : GradientController(config, dispatcher, stats_prefix, scope, time_source),
      config_(std::move(config)), random_(random), deferred_limit_value_(0) {
  min_rtt_calc_timer_ =
      this->dispatcher().createTimer([this]() -> void { enterMinRTTSamplingWindow(); });

  enterMinRTTSamplingWindow();
  enableSamplingTimer();
}

GradientControllerStats GradientControllerStats::generateStats(Stats::Scope& scope,
                                                               const std::string& stats_prefix) {
  return {ALL_GRADIENT_CONTROLLER_STATS(POOL_COUNTER_PREFIX(scope, stats_prefix),
                                        POOL_GAUGE_PREFIX(scope, stats_prefix))};
}

void GradientController::clearSamples() { hist_clear(latency_sample_hist_.get()); }

void DynamicGradientController::enterMinRTTSamplingWindow() {
  // There a potential race condition where setting the minimum concurrency multiple times in a row
  // resets the minRTT sampling timer and triggers the calculation immediately. This could occur
  // after the minRTT sampling window has already been entered, so we can simply return here knowing
  // the desired action is already being performed.
  if (inMinRTTSamplingWindow()) {
    return;
  }

  absl::MutexLock ml(&sampleMutationMutex());

  stats().min_rtt_calculation_active_.set(1);

  // Set the minRTT flag to indicate we're gathering samples to update the value. This will
  // prevent the sample window from resetting until enough requests are gathered to complete the
  // recalculation.
  deferred_limit_value_.store(DynamicGradientController::concurrencyLimit());
  updateConcurrencyLimit(config_.minConcurrency());

  // Throw away any latency samples from before the recalculation window as it may not represent
  // the minRTT.
  clearSamples();

  min_rtt_epoch_ = timeSource().monotonicTime();
}

uint32_t GradientController::numSamples() { return hist_sample_count(latency_sample_hist_.get()); }

void DynamicGradientController::recalculateAfterSample() {
  // Here, we recalculate the minRTT when it is in minRTT sampling window and
  // number of samples is greater than or equal to the minRTTAggregateRequestCount.
  if (!inMinRTTSamplingWindow() || numSamples() < config_.minRTTAggregateRequestCount()) {
    return;
  }

  min_rtt_ = processLatencySamplesAndClear();
  stats().min_rtt_msecs_.set(
      std::chrono::duration_cast<std::chrono::milliseconds>(min_rtt_).count());
  updateConcurrencyLimit(deferred_limit_value_.load());
  deferred_limit_value_.store(0);
  stats().min_rtt_calculation_active_.set(0);

  min_rtt_calc_timer_->enableTimer(
      applyJitter(config_.minRTTCalcInterval(), config_.jitterPercent()));
  enableSamplingTimer();
}

std::chrono::milliseconds DynamicGradientController::applyJitter(std::chrono::milliseconds interval,
                                                                 double jitter_pct) const {
  if (jitter_pct == 0) {
    return interval;
  }

  const uint32_t jitter_range_ms = std::ceil(interval.count() * jitter_pct);
  return std::chrono::milliseconds(interval.count() + (random_.random() % jitter_range_ms));
}

void GradientController::resetSampleWindow() {
  // The sampling window must not be reset while sampling for the new minRTT value.
  ASSERT(shouldSampleRTT());

  if (hist_sample_count(latency_sample_hist_.get()) == 0) {
    return;
  }

  sample_rtt_ = processLatencySamplesAndClear();
  stats_.sample_rtt_msecs_.set(
      std::chrono::duration_cast<std::chrono::milliseconds>(sample_rtt_).count());
  updateConcurrencyLimit(calculateNewLimit());
}

std::chrono::microseconds GradientController::processLatencySamplesAndClear() {
  const std::array<double, 1> quantile{config_.sampleAggregatePercentile()};
  std::array<double, 1> calculated_quantile;
  hist_approx_quantile(latency_sample_hist_.get(), quantile.data(), 1, calculated_quantile.data());
  hist_clear(latency_sample_hist_.get());
  return std::chrono::microseconds(static_cast<int>(calculated_quantile[0]));
}

uint32_t GradientController::calculateNewLimit() {
  ASSERT(sample_rtt_.count() > 0);

  // Calculate the gradient value, ensuring it's clamped between 0.5 and 2.0.
  // This prevents extreme changes in the concurrency limit between each sample
  // window.
  const auto min_rtt = minRTT();
  const auto buffered_min_rtt = min_rtt.count() + min_rtt.count() * config_.minRTTBufferPercent();
  const double raw_gradient = static_cast<double>(buffered_min_rtt) / sample_rtt_.count();
  const double gradient = std::max<double>(0.5, std::min<double>(2.0, raw_gradient));

  // Scale the value by 1000 when reporting it to maintain the granularity of its details
  // See: https://github.com/envoyproxy/envoy/issues/31695
  stats_.gradient_.set(gradient * 1000);

  const double limit = concurrencyLimit() * gradient;
  const double burst_headroom = sqrt(limit);
  stats_.burst_queue_size_.set(burst_headroom);

  // The final concurrency value factors in the burst headroom and must be clamped to keep the value
  // in the range [configured_min, configured_max].
  const uint32_t new_limit = limit + burst_headroom;
  return std::max<uint32_t>(config_.minConcurrency(),
                            std::min<uint32_t>(config_.maxConcurrencyLimit(), new_limit));
}

RequestForwardingAction GradientController::forwardingDecision() {
  // Note that a race condition exists here which would allow more outstanding requests than the
  // concurrency limit bounded by the number of worker threads. After loading num_rq_outstanding_
  // and before loading concurrency_limit_, another thread could potentially swoop in and modify
  // num_rq_outstanding_, causing us to move forward with stale values and increment
  // num_rq_outstanding_.
  //
  // TODO (tonya11en): Reconsider using a CAS loop here.
  if (num_rq_outstanding_.load() < concurrencyLimit()) {
    ++num_rq_outstanding_;
    return RequestForwardingAction::Forward;
  }
  stats_.rq_blocked_.inc();
  return RequestForwardingAction::Block;
}

void GradientController::recordLatencySample(MonotonicTime rq_send_time) {
  ASSERT(num_rq_outstanding_.load() > 0);
  --num_rq_outstanding_;

  if (!shouldRecordSample(rq_send_time)) {
    return;
  }

  const std::chrono::microseconds rq_latency =
      std::chrono::duration_cast<std::chrono::microseconds>(time_source_.monotonicTime() -
                                                            rq_send_time);
  synchronizer_.syncPoint("pre_hist_insert");
  {
    absl::MutexLock ml(&sample_mutation_mtx_);
    hist_insert(latency_sample_hist_.get(), rq_latency.count(), 1);
    recalculateAfterSample();
  }
}

void GradientController::cancelLatencySample() {
  ASSERT(num_rq_outstanding_.load() > 0);
  --num_rq_outstanding_;
}

void GradientController::updateConcurrencyLimit(const uint32_t new_limit) {
  const auto old_limit = concurrency_limit_.load();
  concurrency_limit_.store(new_limit);
  stats_.concurrency_limit_.set(concurrency_limit_.load());

  if (shouldSampleRTT() && old_limit == config_.minConcurrency() &&
      new_limit == config_.minConcurrency()) {
    ++consecutive_min_concurrency_set_;
  } else {
    consecutive_min_concurrency_set_ = 0;
  }
  processConcurrencyLimitUpdate(consecutive_min_concurrency_set_);
}

void DynamicGradientController::processConcurrencyLimitUpdate(int consecutive_min_concurrency_set) {
  // If the concurrency limit is being set to the minimum value for the 5th consecutive sample
  // window while not in the middle of a minRTT measurement, this might be indicative of an
  // inaccurate minRTT measurement. Since the limit is already where it needs to be for a minRTT
  // measurement, we should measure it again.
  //
  // There is a possibility that the minRTT measurement begins before we are able to
  // cancel/re-enable the timer below and triggers overlapping minRTT windows. To protect against
  // this, there is an explicit check when entering the minRTT measurement that ensures there is
  // only a single minRTT measurement active at a time.
  if (consecutive_min_concurrency_set >= 5) {
    min_rtt_calc_timer_->enableTimer(std::chrono::milliseconds(0));
  }
}

} // namespace Controller
} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
