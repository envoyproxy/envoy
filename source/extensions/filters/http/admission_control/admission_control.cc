#include "source/extensions/filters/http/admission_control/admission_control.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/common/random_generator.h"
#include "envoy/extensions/filters/http/admission_control/v3/admission_control.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/http/codes.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/filter_config.h"

#include "source/common/common/cleanup.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/grpc/common.h"
#include "source/common/http/codes.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/admission_control/evaluators/success_criteria_evaluator.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {

using GrpcStatus = Grpc::Status::GrpcStatus;

static constexpr double defaultAggression = 1.0;
static constexpr double defaultSuccessRateThreshold = 95.0;
static constexpr uint32_t defaultRpsThreshold = 0;
static constexpr double defaultMaxRejectionProbability = 80.0;

AdmissionControlFilterConfig::AdmissionControlFilterConfig(
    const AdmissionControlProto& proto_config, Runtime::Loader& runtime,
    Random::RandomGenerator& random, Stats::Scope& scope,
    ThreadLocal::TypedSlotPtr<ThreadLocalControllerImpl>&& tls,
    std::shared_ptr<ResponseEvaluator> response_evaluator)
    : random_(random), scope_(scope), tls_(std::move(tls)),
      admission_control_feature_(proto_config.enabled(), runtime),
      aggression_(proto_config.has_aggression()
                      ? std::make_unique<Runtime::Double>(proto_config.aggression(), runtime)
                      : nullptr),
      sr_threshold_(proto_config.has_sr_threshold() ? std::make_unique<Runtime::Percentage>(
                                                          proto_config.sr_threshold(), runtime)
                                                    : nullptr),
      rps_threshold_(proto_config.has_rps_threshold()
                         ? std::make_unique<Runtime::UInt32>(proto_config.rps_threshold(), runtime)
                         : nullptr),
      max_rejection_probability_(proto_config.has_max_rejection_probability()
                                     ? std::make_unique<Runtime::Percentage>(
                                           proto_config.max_rejection_probability(), runtime)
                                     : nullptr),
      response_evaluator_(std::move(response_evaluator)) {}

double AdmissionControlFilterConfig::aggression() const {
  return std::max<double>(1.0, aggression_ ? aggression_->value() : defaultAggression);
}

double AdmissionControlFilterConfig::successRateThreshold() const {
  const double pct = sr_threshold_ ? sr_threshold_->value() : defaultSuccessRateThreshold;
  return std::min<double>(pct, 100.0) / 100.0;
}

uint32_t AdmissionControlFilterConfig::rpsThreshold() const {
  return rps_threshold_ ? rps_threshold_->value() : defaultRpsThreshold;
}

double AdmissionControlFilterConfig::maxRejectionProbability() const {
  const double ret = max_rejection_probability_ ? max_rejection_probability_->value()
                                                : defaultMaxRejectionProbability;
  return ret / 100.0;
}

AdmissionControlFilter::AdmissionControlFilter(AdmissionControlFilterConfigSharedPtr config,
                                               const std::string& stats_prefix)
    : config_(std::move(config)), stats_(generateStats(config_->scope(), stats_prefix)) {}

Http::FilterHeadersStatus AdmissionControlFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  if (!config_->filterEnabled() || decoder_callbacks_->streamInfo().healthCheck()) {
    // We must forego recording the success/failure of this request during encoding.
    record_request_ = false;
    return Http::FilterHeadersStatus::Continue;
  }

  if (config_->getController().averageRps() < config_->rpsThreshold()) {
    ENVOY_LOG(debug, "Current rps: {} is below rps_threshold: {}, continue");
    return Http::FilterHeadersStatus::Continue;
  }

  if (shouldRejectRequest()) {
    // We do not want to sample requests that we are rejecting, since this taints the measurements
    // that should be describing the upstreams. In addition, if we were to record the requests
    // rejected, the rejection probabilities would not converge back to 0 even if the upstream
    // success rate returns to 100%.
    record_request_ = false;

    stats_.rq_rejected_.inc();
    decoder_callbacks_->sendLocalReply(Http::Code::ServiceUnavailable, "", nullptr, absl::nullopt,
                                       "denied_by_admission_control");
    return Http::FilterHeadersStatus::StopIteration;
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus AdmissionControlFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                                bool end_stream) {
  // TODO(tonya11en): It's not possible for an HTTP filter to understand why a stream is reset, so
  // we are not currently accounting for resets when recording requests.

  if (!record_request_) {
    return Http::FilterHeadersStatus::Continue;
  }

  bool successful_response = false;
  if (Grpc::Common::isGrpcResponseHeaders(headers, end_stream)) {
    absl::optional<GrpcStatus> grpc_status = Grpc::Common::getGrpcStatus(headers);

    // If the GRPC status isn't found in the headers, it must be found in the trailers.
    expect_grpc_status_in_trailer_ = !grpc_status.has_value();
    if (expect_grpc_status_in_trailer_) {
      return Http::FilterHeadersStatus::Continue;
    }

    const uint32_t status = enumToInt(grpc_status.value());
    successful_response = config_->responseEvaluator().isGrpcSuccess(status);
  } else {
    // HTTP response.
    const uint64_t http_status = Http::Utility::getResponseStatus(headers);
    successful_response = config_->responseEvaluator().isHttpSuccess(http_status);
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

    if (grpc_status.has_value() &&
        config_->responseEvaluator().isGrpcSuccess(grpc_status.value())) {
      recordSuccess();
    } else {
      recordFailure();
    }
  }

  return Http::FilterTrailersStatus::Continue;
}

bool AdmissionControlFilter::shouldRejectRequest() const {
  // This formula is documented in the admission control filter documentation:
  // https://www.envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/admission_control_filter.html
  const auto request_counts = config_->getController().requestCounts();
  const double total_requests = request_counts.requests;
  const double successful_requests = request_counts.successes;
  double probability = total_requests - successful_requests / config_->successRateThreshold();
  probability = probability / (total_requests + 1);
  const auto aggression = config_->aggression();
  if (aggression != 1.0) {
    probability = std::pow(probability, 1.0 / aggression);
  }
  probability = std::min<double>(probability, config_->maxRejectionProbability());

  // Choosing an accuracy of 4 significant figures for the probability.
  static constexpr uint64_t accuracy = 1e4;
  auto r = config_->random().random();
  return (accuracy * std::max(probability, 0.0)) > (r % accuracy);
}

} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
