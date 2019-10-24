#include "extensions/filters/http/adaptive_concurrency/adaptive_concurrency_filter.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "common/common/assert.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/adaptive_concurrency/concurrency_controller/concurrency_controller.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {

AdaptiveConcurrencyFilterConfig::AdaptiveConcurrencyFilterConfig(
    const envoy::config::filter::http::adaptive_concurrency::v2alpha::AdaptiveConcurrency&
        proto_config,
    Runtime::Loader& runtime, std::string stats_prefix, Stats::Scope&, TimeSource& time_source)
    : stats_prefix_(std::move(stats_prefix)), time_source_(time_source),
      adaptive_concurrency_feature_(proto_config.enabled(), runtime) {}

AdaptiveConcurrencyFilter::AdaptiveConcurrencyFilter(
    AdaptiveConcurrencyFilterConfigSharedPtr config, ConcurrencyControllerSharedPtr controller)
    : config_(std::move(config)), controller_(std::move(controller)) {}

Http::FilterHeadersStatus AdaptiveConcurrencyFilter::decodeHeaders(Http::HeaderMap&, bool) {
  // In addition to not sampling if the filter is disabled, health checks should also not be sampled
  // by the concurrency controller since they may potentially bias the sample aggregate to lower
  // latency measurements.
  if (!config_->filterEnabled() || decoder_callbacks_->streamInfo().healthCheck()) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (controller_->forwardingDecision() == ConcurrencyController::RequestForwardingAction::Block) {
    decoder_callbacks_->sendLocalReply(Http::Code::ServiceUnavailable, "", nullptr, absl::nullopt,
                                       "reached concurrency limit");
    return Http::FilterHeadersStatus::StopIteration;
  }

  // When the deferred_sample_task_ object is destroyed, the time difference between its destruction
  // and the request start time is measured as the request latency. This value is sampled by the
  // concurrency controller either when encoding is complete or during destruction of this filter
  // object.
  deferred_sample_task_ =
      std::make_unique<Cleanup>([this, rq_start_time = config_->timeSource().monotonicTime()]() {
        const auto now = config_->timeSource().monotonicTime();
        const std::chrono::nanoseconds rq_latency = now - rq_start_time;
        controller_->recordLatencySample(rq_latency);
      });

  return Http::FilterHeadersStatus::Continue;
}

void AdaptiveConcurrencyFilter::encodeComplete() { deferred_sample_task_.reset(); }

void AdaptiveConcurrencyFilter::onDestroy() {
  if (deferred_sample_task_) {
    // The sampling task hasn't been destroyed yet, so this implies we did not complete encoding.
    // Let's stop the sampling from happening and perform request cleanup inside the controller.
    //
    // TODO (tonya11en): Return some RAII handle from the concurrency controller that performs this
    // logic as part of its lifecycle.
    deferred_sample_task_->cancel();
    controller_->cancelLatencySample();
  }
}

} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
