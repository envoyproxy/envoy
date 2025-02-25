#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/adaptive_concurrency/controller/gradient_controller.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {
namespace Controller {

PinnedGradientControllerConfig::PinnedGradientControllerConfig(
    const envoy::extensions::filters::http::adaptive_concurrency::v3::
        PinnedGradientControllerConfig& proto_config,
    Runtime::Loader& runtime)
    : GradientControllerConfig::GradientControllerConfig(
          std::chrono::milliseconds(DurationUtil::durationToMilliseconds(
              proto_config.concurrency_limit_params().concurrency_update_interval())),
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config.concurrency_limit_params(),
                                          max_concurrency_limit, 1000),
          PROTOBUF_PERCENT_TO_DOUBLE_OR_DEFAULT(proto_config, sample_aggregate_percentile, 50),
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config, min_concurrency, 3),
          0.0, // let user provide padding by tuning pinned minRTT
          runtime),
      min_rtt_(
          std::chrono::milliseconds(DurationUtil::durationToMilliseconds(proto_config.min_rtt()))) {
}

PinnedGradientController::PinnedGradientController(PinnedGradientControllerConfig config,
                                                   Event::Dispatcher& dispatcher,
                                                   const std::string& stats_prefix,
                                                   Stats::Scope& scope, TimeSource& time_source)
    : GradientController(config, dispatcher, stats_prefix, scope, time_source),
      config_(std::move(config)) {
  enableSamplingTimer();
  stats().min_rtt_msecs_.set(
      std::chrono::duration_cast<std::chrono::milliseconds>(config_.minRTT()).count());
}

} // namespace Controller
} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
