#include <chrono>
#include <atomic>

#include "extensions/filters/http/adaptive_concurrency/concurrency_controller/concurrency_controller.h"
#include "envoy/runtime/runtime.h"
#include "common/protobuf/utility.h"
#include "extensions/filters/http/adaptive_concurrency/concurrency_controller/gradient_controller.h"
#include "envoy/config/filter/http/adaptive_concurrency/v2alpha/adaptive_concurrency.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/stats/stats.h"
#include "common/common/cleanup.h"
#include "common/protobuf/protobuf.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {
namespace ConcurrencyController {

GradientControllerConfig::GradientControllerConfig(
  const envoy::config::filter::http::adaptive_concurrency::v2alpha::GradientControllerConfig& proto_config) :
  proto_config_(proto_config),
  min_rtt_calc_interval_(
      std::chrono::milliseconds(
        DurationUtil::durationToMilliseconds(
          proto_config.min_rtt_calc_params().interval()))),
  sample_rtt_calc_interval_(
      std::chrono::milliseconds(
        DurationUtil::durationToMilliseconds(
          proto_config.concurrency_limit_params().concurrency_update_interval()))),
  max_concurrency_limit_(PROTOBUF_GET_WRAPPED_REQUIRED(proto_config.concurrency_limit_params(), max_concurrency_limit)),
  min_rtt_aggregate_request_count_(PROTOBUF_GET_WRAPPED_REQUIRED(proto_config.min_rtt_calc_params(), request_count)),
  max_gradient_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config.concurrency_limit_params(), max_gradient, 2.0)) {

}

double GradientControllerConfig::sample_aggregate_percentile() const {
 switch (proto_config_.sample_aggregate_percentile()) {
   case envoy::config::filter::http::adaptive_concurrency::v2alpha::GradientControllerConfig::P50:
     return 0.5;
   case envoy::config::filter::http::adaptive_concurrency::v2alpha::GradientControllerConfig::P75:
     return 0.75;
   case envoy::config::filter::http::adaptive_concurrency::v2alpha::GradientControllerConfig::P90:
     return 0.9;
   case envoy::config::filter::http::adaptive_concurrency::v2alpha::GradientControllerConfig::P95:
     return 0.95;
   case envoy::config::filter::http::adaptive_concurrency::v2alpha::GradientControllerConfig::P99:
     return 0.99;
   default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

GradientController::GradientController(GradientControllerConfigSharedPtr config,
                                       Event::Dispatcher& dispatcher,
                                       Runtime::Loader& ,
                                       std::string ,
                                       Stats::Scope&) :

                                       config_(config),
                                       dispatcher_(dispatcher),
//                                       runtime_(runtime),
//                                       stats_prefix_(stats_prefix),
//                                       scope_(scope),
                                       recalculating_min_rtt_(true),
                                       num_rq_outstanding_(0),
                                       concurrency_limit_(1),
                                       latency_sample_hist_(hist_fast_alloc()) {
  min_rtt_calc_timer_ = dispatcher_.createTimer([this]() -> void {
    absl::MutexLock ml(&update_window_mtx_);
    setMinRTTSamplingWindow();
  });

  sample_reset_timer_= dispatcher_.createTimer([this]() -> void {
    absl::MutexLock ml(&update_window_mtx_);
    resetSampleWindow();
  });

  // Start the sample reset timer upon leaving scope.
  auto defer_sample_reset_timer = Cleanup([this](){
    sample_reset_timer_->enableTimer(config_->sample_rtt_calc_interval());
  });
}

GradientController::~GradientController() {
  min_rtt_calc_timer_.reset();
  sample_reset_timer_.reset();
  hist_free(latency_sample_hist_);
}

void GradientController::setMinRTTSamplingWindow() {
  // Set the minRTT flag to indicate we're gathering samples to update the value. This will
  // prevent the sample window from resetting until enough requests are gathered to complete the
  // recalculation.
  concurrency_limit_.store(1);
  recalculating_min_rtt_.store(true);

  // Throw away any latency samples from before the recalculation window as it may not represent
  // the minRTT.
  absl::MutexLock ml(&latency_sample_mtx_);
  hist_clear(latency_sample_hist_);
}

void GradientController::updateMinRTT() {
  ASSERT(recalculating_min_rtt_.load());

  // Reset the timer to ensure the next minRTT sampling window upon leaving scope.
  auto defer = Cleanup([this](){
    min_rtt_calc_timer_->enableTimer(config_->min_rtt_calc_interval());
  });

  absl::MutexLock ml(&latency_sample_mtx_);
  min_rtt_ = processLatencySamplesAndClear();
  recalculating_min_rtt_.store(false);
}

void GradientController::resetSampleWindow() {
  // Reset the timer upon leaving scope.
  auto defer = Cleanup([this](){
    sample_reset_timer_->enableTimer(config_->sample_rtt_calc_interval());
  });

  // The sampling window must not be reset while sampling for the new minRTT value.
  if (recalculating_min_rtt_.load()) {
    return;
  }

  absl::MutexLock ml(&latency_sample_mtx_);
  if (hist_sample_count(latency_sample_hist_) == 0) {
    return;
  }

  sample_rtt_ = processLatencySamplesAndClear();
  concurrency_limit_.store(calculateNewLimit());
}

std::chrono::microseconds GradientController::processLatencySamplesAndClear() {
  const double quantile[1] = {config_->sample_aggregate_percentile()};
  double ans[1];
  hist_approx_quantile(latency_sample_hist_, quantile, 1, ans);
  hist_clear(latency_sample_hist_);
  return std::chrono::microseconds(static_cast<int>(ans[0]));
}

int GradientController::calculateNewLimit() {
  const double gradient =
    std::min(config_->max_gradient(), double(min_rtt_.count()) / sample_rtt_.count());
  const double limit = concurrency_limit_.load() * gradient;
  const double burst_headroom = sqrt(limit);
  const auto clamp = [](int min, int max, int val) {
    return std::max(min, std::min(max, val));
  };
  return clamp(1, config_->max_concurrency_limit(), limit + burst_headroom);
}

RequestForwardingAction GradientController::forwardingDecision() {
  while (true) {
    int curr_outstanding = num_rq_outstanding_.load();
    if (curr_outstanding < concurrency_limit_.load()) {
      if (num_rq_outstanding_.compare_exchange_weak(curr_outstanding, curr_outstanding + 1)) {
        return RequestForwardingAction::Forward;
      }

      // Another thread swooped in and modified num_rq_outstanding_ between the comparison and
      // attempt at the increment.
      continue;
    }

    // Concurrency limit is reached.
    break;
  }

  return RequestForwardingAction::Block;
}

void GradientController::recordLatencySample(const std::chrono::nanoseconds& rq_latency) {
  const uint32_t latency_usec = std::chrono::duration_cast<std::chrono::microseconds>(rq_latency).count();
  --num_rq_outstanding_;

  int sample_count;
  {
    absl::MutexLock ml(&latency_sample_mtx_);
    hist_insert(latency_sample_hist_, latency_usec, 1);
    sample_count = hist_sample_count(latency_sample_hist_);
  }

  if (recalculating_min_rtt_.load() && sample_count >= config_->min_rtt_aggregate_request_count()) {
    // This sample has pushed the request count over the request count requirement for the minRTT
    // recalculation. It must now be finished.
    updateMinRTT();
  }
}

void GradientController::recordLatencySampleForMinRTT(const std::chrono::nanoseconds& /*rq_latency*/) {
}

} // namespace ConcurrencyController
} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

