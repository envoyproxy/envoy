#include "extensions/filters/http/admission_control/admission_control.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.h"
#include "envoy/server/filter_config.h"
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

static constexpr double defaultAggression = 2.0;
static constexpr std::chrono::seconds defaultSamplingWindow{120};
static constexpr uint32_t defaultMinRequestSamples = 100;

AdmissionControlFilterConfig::AdmissionControlFilterConfig(
    const AdmissionControlProto& proto_config,
    Server::Configuration::FactoryContext& context)
    : runtime_(context.runtime()), time_source_(context.timeSource()), random_(context.random()),
      scope_(context.scope()), tls_(context.threadLocal().allocateSlot()),
      admission_control_feature_(proto_config.enabled(), runtime_),
      sampling_window_(proto_config.has_sampling_window()
                           ? DurationUtil::durationToSeconds(proto_config.sampling_window())
                           : defaultSamplingWindow.count()),
      aggression_(PROTOBUF_PERCENT_TO_DOUBLE_OR_DEFAULT(proto_config, aggression_coefficient,
                                                        defaultAggression)),
      min_request_samples_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config, min_request_samples,
                                                           defaultMinRequestSamples)) {
     tls_->set([this](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
  return std::make_shared<ThreadLocalController>(time_source_, sampling_window_);
         }); 
      }

AdmissionControlFilter::AdmissionControlFilter(AdmissionControlFilterConfigSharedPtr config,
                                               const std::string& stats_prefix)
    : config_(std::move(config)), stats_(generateStats(config_->scope(), stats_prefix)) {}

Http::FilterHeadersStatus AdmissionControlFilter::decodeHeaders(Http::HeaderMap&, bool) {
  if (!config_->filterEnabled() || decoder_callbacks_->streamInfo().healthCheck()) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (shouldRejectRequest()) {
    decoder_callbacks_->sendLocalReply(Http::Code::ServiceUnavailable, "", nullptr, absl::nullopt,
                                       "throttling request");
    stats_.rq_rejected_.inc();
    return Http::FilterHeadersStatus::StopIteration;
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus AdmissionControlFilter::encodeHeaders(Http::HeaderMap& headers,
                                                                bool end_stream) {
  if (end_stream) {
    // TODO @tallen make this match on config
    const uint64_t status_code = Http::Utility::getResponseStatus(headers);
    config_->getController().recordRequest(status_code == enumToInt(Http::Code::OK));
  }
  return Http::FilterHeadersStatus::Continue;
}

ThreadLocalController::ThreadLocalController(TimeSource& time_source,
                                             std::chrono::seconds sampling_window)
    : time_source_(time_source), sampling_window_(sampling_window) {}

bool AdmissionControlFilter::shouldRejectRequest() const {
  const double total = config_->getController().requestTotal();
  const double success = config_->getController().requestSuccessCount();
  const double probability = (total - config_->aggression() * success) / (total + 1);

  // Choosing an accuracy of 4 significant figures for the probability.
  static constexpr uint64_t accuracy = 1e4;
  return (accuracy * std::max(probability, 0.0)) > (config_->random().random() % accuracy);
}

void ThreadLocalController::maybeUpdateHistoricalData() {
  const MonotonicTime now = time_source_.monotonicTime();

  while (!historical_data_.empty() &&
         (now - historical_data_.front().first) >= sampling_window_) {
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

void ThreadLocalController::recordRequest(const bool success) {
  maybeUpdateHistoricalData();
  ++local_data_.requests;
  ++global_data_.requests;
  if (success) {
    ++local_data_.successes;
    ++global_data_.successes;
  }
}

} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
