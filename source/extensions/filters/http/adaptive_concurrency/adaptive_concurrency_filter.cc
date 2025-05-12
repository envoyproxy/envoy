#include "source/extensions/filters/http/adaptive_concurrency/adaptive_concurrency_filter.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/adaptive_concurrency/v3/adaptive_concurrency.pb.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/adaptive_concurrency/controller/controller.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {

namespace {
Http::Code toErrorCode(uint64_t status) {
  const auto code = static_cast<Http::Code>(status);
  if (code >= Http::Code::BadRequest) {
    return code;
  }
  return Http::Code::ServiceUnavailable;
}
} // namespace

AdaptiveConcurrencyFilterConfig::AdaptiveConcurrencyFilterConfig(
    const envoy::extensions::filters::http::adaptive_concurrency::v3::AdaptiveConcurrency&
        proto_config,
    Runtime::Loader& runtime, std::string stats_prefix, Stats::Scope&, TimeSource& time_source)
    : stats_prefix_(std::move(stats_prefix)), time_source_(time_source),
      adaptive_concurrency_feature_(proto_config.enabled(), runtime),
      concurrency_limit_exceeded_status_(
          toErrorCode(proto_config.concurrency_limit_exceeded_status().code())) {}

AdaptiveConcurrencyFilter::AdaptiveConcurrencyFilter(
    AdaptiveConcurrencyFilterConfigSharedPtr config, ConcurrencyControllerSharedPtr controller)
    : config_(std::move(config)), controller_(std::move(controller)) {}

Http::FilterHeadersStatus AdaptiveConcurrencyFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  // In addition to not sampling if the filter is disabled, health checks should also not be sampled
  // by the concurrency controller since they may potentially bias the sample aggregate to lower
  // latency measurements.
  if (!config_->filterEnabled() || decoder_callbacks_->streamInfo().healthCheck()) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (controller_->forwardingDecision() == Controller::RequestForwardingAction::Block) {
    decoder_callbacks_->sendLocalReply(config_->concurrencyLimitExceededStatus(),
                                       "reached concurrency limit", nullptr, absl::nullopt,
                                       "reached_concurrency_limit");
    return Http::FilterHeadersStatus::StopIteration;
  }

  // When the deferred_sample_task_ object is destroyed, the request start time is sampled. This
  // occurs either when encoding is complete or during destruction of this filter object.
  const auto now = config_->timeSource().monotonicTime();
  deferred_sample_task_ =
      std::make_unique<Cleanup>([this, now]() { controller_->recordLatencySample(now); });

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
