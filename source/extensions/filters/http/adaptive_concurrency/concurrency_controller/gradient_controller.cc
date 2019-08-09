#include <chrono>

#include "extensions/filters/http/adaptive_concurrency/concurrency_controller/concurrency_controller.h"
#include "common/protobuf/protobuf.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {
namespace ConcurrencyController {

  GradientControllerConfig::GradientControllerConfig(
      const envoy::config::filter::http::adaptive_concurrency::v2alpha::GradientControllerConfig& proto_config) :
    // @tallen need to convert proto to actual class here.
   min_rtt_calc_interval_(
       std::chrono::milliseconds(
         DurationUtil::durationToMilliseconds(
           proto_config.min_rtt_calc_params().interval()))),
   sample_rtt_calc_interval_(
       std::chrono::milliseconds(
         DurationUtil::durationToMilliseconds(
           proto_config.concurrency_limit_params().concurrency_update_interval()))),
   max_limit_(proto_config.concurrency_limit_params().max_concurrency_limit())
  {

GradientController::GradientController(
    Event::Dispatcher& dispatcher, 
    Runtime::Loader& runtime, 
    std::string stats_prefix, 
    Stats::Scope& scope,
    TimeSource& time_source) :
  dispatcher_(dispatcher),
  runtime_(runtime),
  stats_prefix_(stats_prefix),
  scope_(scope),
  time_source_(time_source) { 

min_rtt_calc_timer_
sample_reset_timer_
}

RequestForwardingAction GradientController::forwardingDecision() {

}

void GradientController::recordLatencySample(const std::chrono::nanoseconds& rq_latency) {

}

} // namespace ConcurrencyController
} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

