#include "source/extensions/filters/http/adaptive_concurrency/controller/ewma_controller.h"

#include <algorithm>
#include <cmath>

#include "source/common/common/assert.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {
namespace Controller {

absl::StatusOr<std::shared_ptr<EwmaController>>
EwmaController::create(const Config& config, Event::Dispatcher& dispatcher,
                       const std::string& stats_prefix, Stats::Scope& scope,
                       TimeSource& time_source) {
  const uint32_t minimum = PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, min_concurrency_limit, 3);
  const uint32_t maximum = PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_concurrency_limit, 1000);
  const uint32_t initial = PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, initial_concurrency, minimum);
  if (initial < minimum || initial > maximum) {
    return absl::InvalidArgumentError("adaptive_concurrency: EWMA requires min_concurrency_limit "
                                      "<= initial_concurrency <= max_concurrency_limit");
  }
  return std::shared_ptr<EwmaController>(new EwmaController(
      config, dispatcher, stats_prefix, scope, time_source, minimum, initial, maximum));
}

EwmaController::EwmaController(const Config& config, Event::Dispatcher& dispatcher,
                               const std::string& stats_prefix, Stats::Scope& scope,
                               TimeSource& time_source, uint32_t minimum, uint32_t initial,
                               uint32_t maximum)
    : time_source_(time_source),
      stats_{ALL_EWMA_CONTROLLER_STATS(POOL_COUNTER_PREFIX(scope, stats_prefix),
                                       POOL_GAUGE_PREFIX(scope, stats_prefix))},
      interval_(PROTOBUF_GET_MS_OR_DEFAULT(config, concurrency_update_interval, 1000)),
      half_life_(config.has_half_life()
                     ? std::chrono::microseconds(
                           Protobuf::util::TimeUtil::DurationToMicroseconds(config.half_life()))
                     : std::chrono::seconds(300)),
      warmup_windows_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, warmup_windows, 10)),
      percentile_(PROTOBUF_PERCENT_TO_DOUBLE_OR_DEFAULT(config, sample_aggregate_percentile, 50) /
                  100.0),
      relative_buffer_(PROTOBUF_PERCENT_TO_DOUBLE_OR_DEFAULT(config, buffer, 25) / 100.0),
      absolute_buffer_us_(
          Protobuf::util::TimeUtil::DurationToMicroseconds(config.min_latency_delta())),
      minimum_(minimum), maximum_(maximum), limit_(initial), samples_(hist_fast_alloc(), hist_free),
      window_start_(time_source.monotonicTime()), estimated_limit_(initial),
      timer_(dispatcher.createTimer([this]() {
        updateWindow();
        timer_->enableTimer(interval_);
      })) {
  stats_.concurrency_limit_.set(initial);
  timer_->enableTimer(interval_);
}

RequestForwardingAction EwmaController::forwardingDecision() {
  const auto limit = concurrencyLimit();
  auto current = outstanding_.load(std::memory_order_relaxed);
  while (current < limit) {
    if (outstanding_.compare_exchange_weak(current, current + 1, std::memory_order_relaxed)) {
      return RequestForwardingAction::Forward;
    }
  }
  stats_.rq_blocked_.inc();
  return RequestForwardingAction::Block;
}

void EwmaController::recordLatencySample(MonotonicTime rq_send_time) {
  const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
      time_source_.monotonicTime() - rq_send_time);
  const auto inflight = outstanding_.fetch_sub(1, std::memory_order_relaxed);
  ASSERT(inflight > 0);
  absl::MutexLock lock(mutex_);
  hist_insert(samples_.get(), elapsed.count(), 1);
  peak_inflight_ = std::max(peak_inflight_, inflight);
}

void EwmaController::cancelLatencySample() {
  ASSERT(outstanding_.load(std::memory_order_relaxed) > 0);
  outstanding_.fetch_sub(1, std::memory_order_relaxed);
}

void EwmaController::updateWindow() {
  const auto now = time_source_.monotonicTime();
  const std::chrono::duration<double> elapsed = now - window_start_;
  window_start_ = now;

  double sample;
  uint32_t peak;
  {
    absl::MutexLock lock(mutex_);
    if (hist_sample_count(samples_.get()) == 0) {
      return;
    }
    hist_approx_quantile(samples_.get(), &percentile_, 1, &sample);
    hist_clear(samples_.get());
    peak = peak_inflight_;
    peak_inflight_ = 0;
  }

  sample = std::max(1.0, sample); // Keep sub-microsecond RTTs finite in the ratio below.
  if (warmup_count_ < warmup_windows_) {
    baseline_us_ += (sample - baseline_us_) / ++warmup_count_;
  } else {
    const double alpha = -std::expm1(-std::log(2.0) * elapsed / half_life_);
    baseline_us_ += alpha * (sample - baseline_us_);
    // After a brownout, do not wait several half-lives to unlearn an inflated baseline.
    if (baseline_us_ > 2.0 * sample) {
      baseline_us_ *= 0.95;
    }
  }
  stats_.baseline_rtt_msecs_.set(baseline_us_ / 1000.0);
  stats_.sample_rtt_msecs_.set(sample / 1000.0);
  const double buffer = std::max(baseline_us_ * relative_buffer_, absolute_buffer_us_);
  const double gradient = std::clamp((baseline_us_ + buffer) / sample, 0.5, 1.0);
  stats_.gradient_.set(gradient * 1000);

  // Sparse traffic still teaches the baseline, but does not justify changing the admission limit.
  if (peak < estimated_limit_ / 2.0) {
    return;
  }
  // Keep the queue allowance below the 50% reduction available at the minimum gradient,
  // even at small limits. At estimates of 16 or more this is Gradient2's usual allowance.
  const double headroom = std::min(4.0, estimated_limit_ * 0.25);
  const double target = estimated_limit_ * gradient + headroom;
  estimated_limit_ = std::clamp(estimated_limit_ + 0.2 * (target - estimated_limit_),
                                static_cast<double>(minimum_), static_cast<double>(maximum_));
  limit_.store(static_cast<uint32_t>(estimated_limit_), std::memory_order_relaxed);
  stats_.concurrency_limit_.set(concurrencyLimit());
}

} // namespace Controller
} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
