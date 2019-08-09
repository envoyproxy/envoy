#include <chrono>

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

 switch (proto_config.sample_aggregate_percentile()) {
   case envoy::config::filter::http::adaptive_concurrency::v2alpha::GradientControllerConfig::P50:
     sample_aggregate_percentile_ = SampleAggregatePercentile::P50;
     break;
   case envoy::config::filter::http::adaptive_concurrency::v2alpha::GradientControllerConfig::P75:
     sample_aggregate_percentile_ = SampleAggregatePercentile::P75;
     break;
   case envoy::config::filter::http::adaptive_concurrency::v2alpha::GradientControllerConfig::P90:
     sample_aggregate_percentile_ = SampleAggregatePercentile::P90;
     break;
   case envoy::config::filter::http::adaptive_concurrency::v2alpha::GradientControllerConfig::P95:
     sample_aggregate_percentile_ = SampleAggregatePercentile::P95;
     break;
   case envoy::config::filter::http::adaptive_concurrency::v2alpha::GradientControllerConfig::P99:
     sample_aggregate_percentile_ = SampleAggregatePercentile::P99;
     break;
   default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

GradientController::GradientController(GradientControllerConfigSharedPtr config,
                                       Event::Dispatcher& dispatcher,
                                       Runtime::Loader& ,
                                       std::string ,
                                       Stats::Scope& ) :

                                       config_(config),
                                       dispatcher_(dispatcher),
//                                       runtime_(runtime),
//                                       stats_prefix_(stats_prefix),
//                                       scope_(scope),
                                       recalculating_min_rtt_(true),
                                       num_rq_outstanding_(0),
                                       concurrency_limit_(config_->starting_concurrency_limit()) {
  min_rtt_calc_timer_ = dispatcher_.createTimer([this]() -> void {
      absl::MutexLock ml(&limit_mtx_);
      updateMinRTT();
  });

  sample_reset_timer_= dispatcher_.createTimer([this]() -> void {
    resetSampleWindow();
  });

  // Start the sample reset timer upon leaving scope.
  auto defer_sample_reset_timer = Cleanup([this](){
    sample_reset_timer_->enableTimer(config_->sample_rtt_calc_interval());
  });

  // There's no need to start the minRTT recalculation timer while it is in the recalculation
  // window, the worker threads will trigger it upon reaching the request threshold.
  beginMinRTTRecalcWindow();
}

GradientController::~GradientController() {
  min_rtt_calc_timer_.reset();
  sample_reset_timer_.reset();
}

void GradientController::updateMinRTT() {
  if (!recalculating_min_rtt_.load()) {
    beginMinRTTRecalcWindow();

    // Throw away any latency samples from before the recalculation window as it may not represent
    // the minRTT.
    latency_sample_hist_.clear();
  }

  if (!minRTTRequestThresholdReached()) {
    // There have not been enough requests accumulated for the calculation. The calculation will
    // rely on a worker thread to reschedule this task once the desired request count threshold
    // for the calculation has been reached.
    return;
  }

  // Reset the timer upon leaving scope.
  auto defer = Cleanup([this](){
    min_rtt_calc_timer_->enableTimer(config_->min_rtt_calc_interval());
  });

  min_rtt_ = processLatencySamplesAndClear();
  recalculating_min_rtt_.store(false);
}

void GradientController::beginMinRTTRecalcWindow() {
  // Set the minRTT flag to indicate we're gathering samples to update the value. This will
  // prevent the sample window from resetting until enough requests are gathered to complete the
  // recalculation.
  concurrency_limit_ = 1;
  recalculating_min_rtt_.store(true);
}

void GradientController::resetSampleWindow() {
  // Reset the timer upon leaving scope.
  auto defer = Cleanup([this](){
    sample_reset_timer_->enableTimer(config_->sample_rtt_calc_interval());
  });

  if (recalculating_min_rtt_.load()) {
    return;
  }

  absl::MutexLock ml(&limit_mtx_);
  if (latency_sample_hist_.empty()) {
    return;
  }

  // TODO @tallen assuming the processing is just looking up in histogram. It's a vector for now and
  // it's unacceptable to perform quantile operations on a vector while holding locks.
  sample_rtt_ = processLatencySamplesAndClear();
  concurrency_limit_ = calculateNewLimit();
}

std::chrono::microseconds GradientController::processLatencySamplesAndClear() {
  // TODO @tallen
  // this function MUST not stay. figure out the histogram situation.
  std::sort(latency_sample_hist_.begin(), latency_sample_hist_.end());
  const std::chrono::microseconds median = std::chrono::microseconds(latency_sample_hist_[latency_sample_hist_.size() / 2]);
  latency_sample_hist_.clear();
  return median;
}

int GradientController::calculateNewLimit() {
  const double gradient =
    std::min(config_->max_gradient(), double(min_rtt_.count()) / sample_rtt_.count());
  const double limit = concurrency_limit_ * gradient;
  const double burst_headroom = sqrt(limit);
  const auto clamp = [](int min, int max, int val) {
    return std::max(min, std::min(max, val));
  };
  return clamp(1, config_->max_concurrency_limit(), limit + burst_headroom);
}

RequestForwardingAction GradientController::forwardingDecision() {
  while (true) {
    const auto curr_outstanding = num_rq_outstanding_.load();
    if (curr_outstanding < concurrency_limit_.load()) {
      if (num_rq_outstanding.compare_exchange_weak(curr_outstanding, curr_outstanding + 1)) {
        return RequestForwardingAction::Forward;
      }

      // Another thread swooped in and modified num_rq_outstanding between the comparison and
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
  num_rq_outstanding_--;

  absl::MutexLock ml(&limit_mtx_);
  latency_sample_hist_.emplace_back(latency_usec);

  if (recalculating_min_rtt_.load() && minRTTRequestThresholdReached()) {
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

