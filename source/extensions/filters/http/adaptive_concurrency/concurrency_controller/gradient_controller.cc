#include <chrono>

#include "extensions/filters/http/adaptive_concurrency/concurrency_controller/concurrency_controller.h"
#include "envoy/runtime/runtime.h"
#include "common/protobuf/utility.h"
#include "extensions/filters/http/adaptive_concurrency/concurrency_controller/gradient_controller.h"
#include "envoy/config/filter/http/adaptive_concurrency/v2alpha/adaptive_concurrency.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/stats/stats.h"
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
  max_concurrency_limit_(PROTOBUF_GET_WRAPPED_REQUIRED(proto_config.concurrency_limit_params(), max_concurrency_limit)) { 

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
                                       Runtime::Loader& runtime, 
                                       std::string stats_prefix, 
                                       Stats::Scope& scope,
                                       TimeSource& time_source) :

                                       config_(config),
                                       dispatcher_(dispatcher),
                                       runtime_(runtime),
                                       stats_prefix_(stats_prefix),
                                       scope_(scope),
                                       time_source_(time_source),
                                       num_rq_outstanding_(0),
                                       concurrency_limit_(config_->starting_concurrency_limit())
//                                       latency_sample_hist_(/* TODO @tallen */)
                                       { 

  min_rtt_calc_timer_ = dispatcher_.createTimer([this]() -> void { calculateMinRTT(); });
  sample_reset_timer_= dispatcher_.createTimer([this]() -> void { updateConcurrencyLimit(); });

  calculateMinRTT();
  updateConcurrencyLimit();
}

GradientController::~GradientController() {
  min_rtt_calc_timer_.reset();
  sample_reset_timer_.reset();
}

void GradientController::calculateMinRTT() {

}

void GradientController::updateConcurrencyLimit() {

}

RequestForwardingAction GradientController::forwardingDecision() {
  absl::MutexLock ml(&limit_mtx_);
  if (num_rq_outstanding_ < concurrency_limit_) {
    ++num_rq_outstanding_;
    return RequestForwardingAction::Forward;
  }
  return RequestForwardingAction::Block;
}

void GradientController::recordLatencySample(const std::chrono::nanoseconds& rq_latency) {
  if (!min_rtt_calculation_mtx_.ReaderTryLock()) {
    // Failing to grab the read lock means there is an on-going
    // min_rtt_recalculation.
    recordLatencySampleForMinRTT(rq_latency);
    return;
  }

  {
    absl::MutexLock ml(&limit_mtx_);
    const uint32_t latency_usec = std::chrono::duration_cast<std::chrono::microseconds>(rq_latency).count();
    //    @tallen
//    latency_sample_hist_.recordValue(latency_usec);
    latency_sample_hist_.emplace_back(latency_usec);
    --num_rq_outstanding_;
  }

  min_rtt_calculation_mtx_.ReaderUnlock();
}

void GradientController::recordLatencySampleForMinRTT(const std::chrono::nanoseconds& /*rq_latency*/) {
}

} // namespace ConcurrencyController
} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

