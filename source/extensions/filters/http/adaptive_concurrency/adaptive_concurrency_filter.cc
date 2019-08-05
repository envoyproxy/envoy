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
    Runtime::Loader& runtime, std::string stats_prefix, Stats::Scope& scope,
    TimeSource& time_source)
    : runtime_(runtime), stats_prefix_(std::move(stats_prefix)), scope_(scope),
      time_source_(time_source) {
  // TODO (tonya11en): Remove these noop calls when stats/runtime values are
  // implemented.
  //
  // Calling for test coverage.
  runtime_.snapshot();
  scope_.constSymbolTable();
}

AdaptiveConcurrencyFilter::AdaptiveConcurrencyFilter(
    AdaptiveConcurrencyFilterConfigSharedPtr config, ConcurrencyControllerSharedPtr controller)
    : config_(std::move(config)), controller_(std::move(controller)) {}

AdaptiveConcurrencyFilter::~AdaptiveConcurrencyFilter() = default;

Http::FilterHeadersStatus AdaptiveConcurrencyFilter::decodeHeaders(Http::HeaderMap&, bool) {
  if (!forwarding_action_) {
    forwarding_action_ = std::make_unique<ConcurrencyController::RequestForwardingAction>(
        controller_->forwardingDecision());
  }

  if (*forwarding_action_ == ConcurrencyController::RequestForwardingAction::Block) {
    // TODO (tonya11en): Remove filler words.
    decoder_callbacks_->sendLocalReply(Http::Code::ServiceUnavailable, "filler words", nullptr,
                                       absl::nullopt, "more filler words");
    return Http::FilterHeadersStatus::StopIteration;
  }

  return Http::FilterHeadersStatus::Continue;
}

void AdaptiveConcurrencyFilter::decodeComplete() {
  ASSERT(forwarding_action_ != nullptr);
  if (*forwarding_action_ == ConcurrencyController::RequestForwardingAction::MustForward) {
    rq_start_time_ = config_->timeSource().monotonicTime();
  }
}

void AdaptiveConcurrencyFilter::encodeComplete() {
  const std::chrono::nanoseconds rq_latency =
      config_->timeSource().monotonicTime() - rq_start_time_;
  controller_->recordLatencySample(rq_latency);
}

} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
