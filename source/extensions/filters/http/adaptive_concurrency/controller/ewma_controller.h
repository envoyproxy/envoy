#pragma once

#include <atomic>
#include <chrono>
#include <memory>

#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/filters/http/adaptive_concurrency/v3/adaptive_concurrency.pb.h"
#include "envoy/stats/stats_macros.h"

#include "source/extensions/filters/http/adaptive_concurrency/controller/controller.h"

#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "circllhist.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {
namespace Controller {

#define ALL_EWMA_CONTROLLER_STATS(COUNTER, GAUGE)                                                  \
  COUNTER(rq_blocked)                                                                              \
  GAUGE(concurrency_limit, NeverImport)                                                            \
  GAUGE(baseline_rtt_msecs, NeverImport)                                                           \
  GAUGE(sample_rtt_msecs, NeverImport)                                                             \
  GAUGE(gradient, NeverImport)

struct EwmaControllerStats {
  ALL_EWMA_CONTROLLER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

// A probe-free Gradient2-style controller. Workers sample completions; only the dispatcher's
// window timer updates the baseline and the fractional limit estimate.
class EwmaController : public ConcurrencyController {
public:
  using Config = envoy::extensions::filters::http::adaptive_concurrency::v3::EwmaControllerConfig;
  static absl::StatusOr<std::shared_ptr<EwmaController>>
  create(const Config& config, Event::Dispatcher& dispatcher, const std::string& stats_prefix,
         Stats::Scope& scope, TimeSource& time_source);

  RequestForwardingAction forwardingDecision() override;
  void recordLatencySample(MonotonicTime rq_send_time) override;
  void cancelLatencySample() override;
  uint32_t concurrencyLimit() const override { return limit_.load(std::memory_order_relaxed); }

private:
  EwmaController(const Config& config, Event::Dispatcher& dispatcher,
                 const std::string& stats_prefix, Stats::Scope& scope, TimeSource& time_source,
                 uint32_t minimum, uint32_t initial, uint32_t maximum);
  void updateWindow();

  TimeSource& time_source_;
  EwmaControllerStats stats_;
  const std::chrono::milliseconds interval_;
  const std::chrono::duration<double> half_life_;
  const uint32_t warmup_windows_;
  const double percentile_;
  const double relative_buffer_;
  const double absolute_buffer_us_;
  const uint32_t minimum_;
  const uint32_t maximum_;
  std::atomic<uint32_t> limit_;
  std::atomic<uint32_t> outstanding_{0};

  absl::Mutex mutex_;
  std::unique_ptr<histogram_t, decltype(&hist_free)> samples_ ABSL_GUARDED_BY(mutex_);
  uint32_t peak_inflight_ ABSL_GUARDED_BY(mutex_) = 0;

  MonotonicTime window_start_;
  uint32_t warmup_count_ = 0;
  double baseline_us_ = 0;
  double estimated_limit_;
  Event::TimerPtr timer_;
};

} // namespace Controller
} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
