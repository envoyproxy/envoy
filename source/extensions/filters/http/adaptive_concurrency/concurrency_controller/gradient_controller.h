#pragma once

#include <chrono>

#include "envoy/config/filter/http/adaptive_concurrency/v2alpha/adaptive_concurrency.pb.h"
#include "envoy/config/filter/http/adaptive_concurrency/v2alpha/adaptive_concurrency.pb.validate.h"
#include "extensions/filters/http/adaptive_concurrency/concurrency_controller/concurrency_controller.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {
namespace ConcurrencyController {

class GradientControllerConfig {
public:
  GradientControllerConfig(
    const envoy::config::filter::http::adaptive_concurrency::v2alpha::GradientControllerConfig& proto_config);

  // accessors.
  std::chrono::nanoseconds min_rtt() const { return min_rtt_;}

private:
  // The measured request round-trip time under ideal conditions.
  std::chrono::milliseconds min_rtt_calc_interval_;

  // The measured sample round-trip time from the previous time window.
  std::chrono::milliseconds sample_rtt_calc_interval_;

  int32_t max_limit_;
};

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
  GradientController(AdaptiveConcurrencyFilterConfigSharedPtr config_);

  // ConcurrencyController.
  RequestForwardingAction forwardingDecision() override;
  void recordLatencySample(const std::chrono::nanoseconds& rq_latency) override;

private:
  Runtime::Loader& runtime_;
  const std::string stats_prefix_;
  Stats::Scope& scope_;
  TimeSource& time_source_;

  std::chrono::nanoseconds min_rtt_;
  absl::Mutex min_rtt_calc_mtx_;
  std::chrono::nanoseconds sample_rtt_;
  int32_t concurrency_headroom_; // @tallen may not even need this..

  TimerPtr min_rtt_calc_timer_;
  TimerPtr sample_reset_timer_;

};

} // namespace ConcurrencyController
} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
