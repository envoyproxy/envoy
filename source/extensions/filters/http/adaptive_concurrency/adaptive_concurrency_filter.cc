#include "extensions/filters/http/adaptive_concurrency/adaptive_concurrency_filter.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "common/common/assert.h"

#include "extensions/filters/http/adaptive_concurrency/concurrency_controller/concurrency_controller.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {

AdaptiveConcurrencyFilterConfig::AdaptiveConcurrencyFilterConfig(
    const envoy::config::filter::http::adaptive_concurrency::v2alpha::AdaptiveConcurrency&,
    Runtime::Loader&, std::string stats_prefix, Stats::Scope&, TimeSource& time_source)
    : stats_prefix_(std::move(stats_prefix)), time_source_(time_source) {}

AdaptiveConcurrencyFilter::AdaptiveConcurrencyFilter(
    AdaptiveConcurrencyFilterConfigSharedPtr config, ConcurrencyControllerSharedPtr controller)
    : config_(std::move(config)), controller_(std::move(controller)) {}

Http::FilterHeadersStatus AdaptiveConcurrencyFilter::decodeHeaders(Http::HeaderMap&, bool) {
  if (controller_->forwardingDecision() == ConcurrencyController::RequestForwardingAction::Block) {
    // TODO (tonya11en): Remove filler words.
    decoder_callbacks_->sendLocalReply(Http::Code::ServiceUnavailable, "filler words", nullptr,
                                       absl::nullopt, "more filler words");
    return Http::FilterHeadersStatus::StopIteration;
  }

  rq_start_time_ = config_->timeSource().monotonicTime();
  return Http::FilterHeadersStatus::Continue;
}

void AdaptiveConcurrencyFilter::encodeComplete() {
  const auto rq_latency = config_->timeSource().monotonicTime() - rq_start_time_;
  controller_->recordLatencySample(rq_latency);
}

} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
