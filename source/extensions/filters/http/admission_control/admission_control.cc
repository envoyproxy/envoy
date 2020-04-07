#include "extensions/filters/http/admission_control/admission_control.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.h"
#include "extensions/filters/http/admission_control/default_evaluator.h"
#include "envoy/grpc/status.h"
#include "envoy/http/codes.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/filter_config.h"

#include "common/common/assert.h"
#include "common/common/cleanup.h"
#include "common/common/enum_to_int.h"
#include "common/grpc/common.h"
#include "common/http/codes.h"
#include "common/http/utility.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {

using GrpcStatus = Grpc::Status::GrpcStatus;

static constexpr double defaultAggression = 2.0;
static constexpr std::chrono::seconds defaultHistoryGranularity{1};

AdmissionControlFilterConfig::AdmissionControlFilterConfig(
    const AdmissionControlProto& proto_config, Runtime::Loader& runtime, TimeSource& time_source,
    Runtime::RandomGenerator& random, Stats::Scope& scope, ThreadLocal::SlotPtr&& tls)
    : runtime_(runtime), time_source_(time_source), random_(random), scope_(scope),
      tls_(std::move(tls)), admission_control_feature_(proto_config.enabled(), runtime_),
      aggression_(
          proto_config.has_aggression_coefficient()
              ? std::make_unique<Runtime::Double>(proto_config.aggression_coefficient(), runtime_)
              : nullptr) {
  switch (proto_config.success_criteria_case()) {
  case AdmissionControlProto::SuccessCriteriaCase::kDefaultSuccessCriteria:
    response_evaluator_ =
        std::make_unique<DefaultResponseEvaluator>(proto_config.default_success_criteria());
    break;
  case AdmissionControlProto::SuccessCriteriaCase::SUCCESS_CRITERIA_NOT_SET:
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

double AdmissionControlFilterConfig::aggression() const {
  return std::max<double>(1.0, aggression_ ? aggression_->value() : defaultAggression);
}

AdmissionControlFilter::AdmissionControlFilter(AdmissionControlFilterConfigSharedPtr config,
                                               const std::string& stats_prefix)
    : config_(std::move(config)), stats_(generateStats(config_->scope(), stats_prefix)) {}

Http::FilterHeadersStatus AdmissionControlFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  if (!config_->filterEnabled() || decoder_callbacks_->streamInfo().healthCheck()) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (shouldRejectRequest()) {
    decoder_callbacks_->sendLocalReply(Http::Code::ServiceUnavailable, "", nullptr, absl::nullopt,
                                       "denied by admission control");
    stats_.rq_rejected_.inc();
    return Http::FilterHeadersStatus::StopIteration;
  }

  deferred_record_failure_ =
      std::make_unique<Cleanup>([this]() { config_->getController().recordFailure(); });

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus AdmissionControlFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                                bool end_stream) {
  bool successful_response = false;
  if (Grpc::Common::isGrpcResponseHeader(headers, end_stream)) {
    absl::optional<GrpcStatus> grpc_status = Grpc::Common::getGrpcStatus(headers);

    // If the GRPC status isn't found in the headers, it must be found in the trailers.
    expect_grpc_status_in_trailer_ = !grpc_status.has_value();
    if (expect_grpc_status_in_trailer_) {
      return Http::FilterHeadersStatus::Continue;
    }

    successful_response = config_->response_evaluator()->isGrpcSuccess(grpc_status.value());
  } else {
    // HTTP response.
    const uint64_t http_status = Http::Utility::getResponseStatus(headers);
    successful_response = config_->response_evaluator()->isHttpSuccess(http_status);
  }

  if (successful_response) {
    recordSuccess();
  } else {
    recordFailure();
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterTrailersStatus
AdmissionControlFilter::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  if (expect_grpc_status_in_trailer_) {
    absl::optional<GrpcStatus> grpc_status = Grpc::Common::getGrpcStatus(trailers, false);

    // Status code must be sent in trailers.
    ASSERT(grpc_status.has_value());

    if (config_->response_evaluator()->isGrpcSuccess(grpc_status.value())) {
      recordSuccess();
    } else {
      recordFailure();
    }
  }

  return Http::FilterTrailersStatus::Continue;
}

bool AdmissionControlFilter::shouldRejectRequest() const {
  const double total = config_->getController().requestTotalCount();
  const double success = config_->getController().requestSuccessCount();
  const double probability = (total - config_->aggression() * success) / (total + 1);

  // Choosing an accuracy of 4 significant figures for the probability.
  static constexpr uint64_t accuracy = 1e4;
  auto r = config_->random().random();
  return (accuracy * std::max(probability, 0.0)) > (r % accuracy);
}

ThreadLocalControllerImpl::ThreadLocalControllerImpl(TimeSource& time_source,
                                                     std::chrono::seconds sampling_window)
    : time_source_(time_source), sampling_window_(sampling_window) {}

void ThreadLocalControllerImpl::maybeUpdateHistoricalData() {
  // Purge stale samples.
  while (!historical_data_.empty() && ageOfOldestSample() >= sampling_window_) {
    removeOldestSample();
  }

  // It's possible we purged stale samples from the history and are left with nothing, so it's
  // necessary to add an empty entry. We will also need to roll over into a new entry in the
  // historical data if we've exceeded the time specified by the granularity.
  if (historical_data_.empty() || ageOfNewestSample() >= defaultHistoryGranularity) {
    historical_data_.emplace_back(time_source_.monotonicTime(), RequestData());
  }
}

void ThreadLocalControllerImpl::recordRequest(const bool success) {
  maybeUpdateHistoricalData();

  // The back of the deque will be the most recent samples.
  ++historical_data_.back().second.requests;
  ++global_data_.requests;
  if (success) {
    ++historical_data_.back().second.successes;
    ++global_data_.successes;
  }
}

} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
