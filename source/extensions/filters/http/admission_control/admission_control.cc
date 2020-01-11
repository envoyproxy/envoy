#include "extensions/filters/http/admission_control/admission_control_filter.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.h"

#include "common/common/assert.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {

static constexpr double defaultAggression = 1.5;
static constexpr uint32_t defaultSamplingWindowSeconds = 120;
static constexpr uint32_t defaultMinRequestSamples = 100;

AdmissionControlFilterConfig::AdmissionControlFilterConfig(
    const envoy::extensions::filters::http::admission_control::v3alpha::AdmissionControl&
        proto_config,
    Runtime::Loader& runtime, std::string stats_prefix, Stats::Scope&, TimeSource& time_source)
    : stats_prefix_(std::move(stats_prefix)), time_source_(time_source),
      admission_control_feature_(proto_config.enabled(), runtime),
      sampling_window_seconds_(proto_config.has_sampling_window() ?
          DurationUtil::durationToSeconds(proto_config.sampling_window()) : defaultSamplingWindowSeconds),
      aggression_(
          PROTOBUF_PERCENT_TO_DOUBLE_OR_DEFAULT(proto_config, aggression, defaultAggression)),
      min_request_samples_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config, min_request_samples, defaultMinRequestSamples))
      {}

AdmissionControlFilter::AdmissionControlFilter(AdmissionControlFilterConfigSharedPtr config)
    : config_(std::move(config)) {}

Http::FilterHeadersStatus AdmissionControlFilter::decodeHeaders(Http::HeaderMap&, bool) {
  if (!config_->filterEnabled() ||
      decoder_callbacks_->streamInfo().healthCheck() ||
      total_rq_count_.load() < config->minRequestSamples()) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (shouldRejectRequest()) {
    decoder_callbacks_->sendLocalReply(Http::Code::ServiceUnavailable, "", nullptr, absl::nullopt,   
                                       "throttling request");                                 
    return Http::FilterHeadersStatus::StopIteration;
  }

  return Http::FilterHeadersStatus::Continue;
}

bool AdmissionControlFilter::shouldRejectRequest() const {
  const double rq_count = total_rq_count_.load();
  const double rq_success_count = total_success_count_.load();
  

  // TODO @tallen: cleanup types and random thing
  const double rejection_pct = 100 * std::max<double>(0,
    (rq_count - config_->aggression() * rq_success_count) / (rq_count + 1));

  return config_->runtime().snapshot().featureEnabled("", rejection_probability);
}

void AdmissionControlFilter::encodeComplete() { deferred_sample_task_.reset(); }

void AdmissionControlFilter::onDestroy() {
  if (deferred_sample_task_) {
    // The sampling task hasn't been destroyed yet, so this implies we did not complete encoding.
    // Let's stop the sampling from happening.
    deferred_sample_task_->cancel();
  }
}

} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
