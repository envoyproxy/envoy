#include "extensions/filters/http/admission_control/admission_control.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.h"
#include "envoy/runtime/runtime.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/http/utility.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {

static constexpr double defaultAggression = 1.5;
static constexpr std::chrono::seconds defaultSamplingWindow{120};
static constexpr uint32_t defaultMinRequestSamples = 100;

AdmissionControlFilterConfig::AdmissionControlFilterConfig(
    const envoy::extensions::filters::http::admission_control::v3alpha::AdmissionControl&
        proto_config,
    Runtime::Loader& runtime, std::string stats_prefix, Stats::Scope&, TimeSource& time_source)
    : runtime_(runtime), stats_prefix_(std::move(stats_prefix)), time_source_(time_source),
      admission_control_feature_(proto_config.enabled(), runtime),
      sampling_window_(proto_config.has_sampling_window()
                           ? DurationUtil::durationToSeconds(proto_config.sampling_window())
                           : defaultSamplingWindow.count()),
      aggression_(PROTOBUF_PERCENT_TO_DOUBLE_OR_DEFAULT(proto_config, aggression_coefficient,
                                                        defaultAggression)),
      min_request_samples_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config, min_request_samples,
                                                           defaultMinRequestSamples)) {}

AdmissionControlFilter::AdmissionControlFilter(AdmissionControlFilterConfigSharedPtr config,
                                               AdmissionControlStateSharedPtr state)
    : config_(std::move(config)), state_(state) {}

Http::FilterHeadersStatus AdmissionControlFilter::decodeHeaders(Http::HeaderMap&, bool) {
  if (!config_->filterEnabled() || decoder_callbacks_->streamInfo().healthCheck()) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (state_->shouldRejectRequest()) {
    decoder_callbacks_->sendLocalReply(Http::Code::ServiceUnavailable, "", nullptr, absl::nullopt,
                                       "throttling request");
    return Http::FilterHeadersStatus::StopIteration;
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus AdmissionControlFilter::encodeHeaders(Http::HeaderMap& headers,
                                                                bool end_stream) {
  if (end_stream) {
    // TODO @tallen make this match on config
    const uint64_t status_code = Http::Utility::getResponseStatus(headers);
    state_->recordRequest(status_code == enumToInt(Http::Code::OK));
  }
  return Http::FilterHeadersStatus::Continue;
}

AdmissionControlState::AdmissionControlState(TimeSource& time_source,
                                             AdmissionControlFilterConfigSharedPtr config,
                                             Runtime::RandomGenerator& random)
    : time_source_(time_source), random_(random), config_(std::move(config)) {}

void AdmissionControlState::maybeUpdateHistoricalData() {
  const MonotonicTime now = time_source_.monotonicTime();

  while (!historical_data_.empty() &&
         (now - historical_data_.front().first) >= config_->samplingWindow()) {
    // Remove stale data.
    global_data_.successes -= historical_data_.front().second.successes;
    global_data_.requests -= historical_data_.front().second.requests;
    historical_data_.pop_front();
  }

  if ((now - historical_data_.back().first) > std::chrono::seconds(1)) {
    // Reset the local data.
    historical_data_.emplace_back(now, local_data_);
    local_data_ = {};
  }
}

void AdmissionControlState::recordRequest(const bool success) {
  maybeUpdateHistoricalData();
  ++local_data_.requests;
  ++global_data_.requests;
  if (success) {
    ++local_data_.successes;
    ++global_data_.successes;
  }
}

bool AdmissionControlState::shouldRejectRequest() {
  const double probability =
      (global_data_.requests - config_->aggression() * global_data_.successes) /
      (global_data_.requests + 1);

  static constexpr uint64_t accuracy = 1e4;
  return (accuracy * std::max(probability, 0.0)) > (random_.random() % accuracy);
}

} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
