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
#include "source/common/common/fmt.h"
#include "source/common/grpc/common.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_utility.h"
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

AdmissionControlRuleConfig::AdmissionControlRuleConfig(
    const AdmissionControlRuleProto& rule_config, Runtime::Loader& runtime,
    ThreadLocal::TypedSlotPtr<ThreadLocalControllerImpl>&& tls,
    std::shared_ptr<ResponseEvaluator> response_evaluator,
    std::vector<Http::HeaderUtility::HeaderDataPtr>&& filter_headers)
    : filter_headers_(std::move(filter_headers)), tls_(std::move(tls)),
      aggression_(rule_config.has_aggression()
                      ? std::make_unique<Runtime::Double>(rule_config.aggression(), runtime)
                      : nullptr),
      sr_threshold_(rule_config.has_sr_threshold()
                        ? std::make_unique<Runtime::Percentage>(rule_config.sr_threshold(), runtime)
                        : nullptr),
      rps_threshold_(rule_config.has_rps_threshold()
                         ? std::make_unique<Runtime::UInt32>(rule_config.rps_threshold(), runtime)
                         : nullptr),
      max_rejection_probability_(rule_config.has_max_rejection_probability()
                                     ? std::make_unique<Runtime::Percentage>(
                                           rule_config.max_rejection_probability(), runtime)
                                     : nullptr),
      response_evaluator_(std::move(response_evaluator)) {}

AdmissionControlRuleConfig::AdmissionControlRuleConfig(
    ThreadLocal::TypedSlotPtr<ThreadLocalControllerImpl>&& tls,
    std::shared_ptr<ResponseEvaluator> response_evaluator,
    std::vector<Http::HeaderUtility::HeaderDataPtr>&& filter_headers,
    std::unique_ptr<Runtime::Double>&& aggression,
    std::unique_ptr<Runtime::Percentage>&& sr_threshold,
    std::unique_ptr<Runtime::UInt32>&& rps_threshold,
    std::unique_ptr<Runtime::Percentage>&& max_rejection_probability)
    : filter_headers_(std::move(filter_headers)), tls_(std::move(tls)),
      aggression_(std::move(aggression)), sr_threshold_(std::move(sr_threshold)),
      rps_threshold_(std::move(rps_threshold)),
      max_rejection_probability_(std::move(max_rejection_probability)),
      response_evaluator_(std::move(response_evaluator)) {}

double AdmissionControlRuleConfig::aggression() const {
  return std::max<double>(1.0, aggression_ ? aggression_->value() : defaultAggression);
}

double AdmissionControlRuleConfig::successRateThreshold() const {
  const double pct = sr_threshold_ ? sr_threshold_->value() : defaultSuccessRateThreshold;
  return std::min<double>(pct, 100.0) / 100.0;
}

uint32_t AdmissionControlRuleConfig::rpsThreshold() const {
  return rps_threshold_ ? rps_threshold_->value() : defaultRpsThreshold;
}

double AdmissionControlRuleConfig::maxRejectionProbability() const {
  const double ret = max_rejection_probability_ ? max_rejection_probability_->value()
                                                : defaultMaxRejectionProbability;
  return ret / 100.0;
}

AdmissionControlFilterConfig::AdmissionControlFilterConfig(
    const AdmissionControlProto& proto_config, Runtime::Loader& runtime,
    Random::RandomGenerator& random, Stats::Scope& scope,
    AdmissionControlRuleConfigSharedPtr&& global,
    std::vector<AdmissionControlRuleConfigSharedPtr>&& rules)
    : random_(random), scope_(scope), admission_control_feature_(proto_config.enabled(), runtime),
      global_(std::move(global)), rules_(std::move(rules)) {}

const AdmissionControlRuleConfig*
AdmissionControlFilterConfig::findMatchingRule(const Http::RequestHeaderMap& headers) const {
  if (!rules_.empty()) {
    for (const auto& rule : rules_) {
      if (rule->filterHeaders().empty() ||
          Http::HeaderUtility::matchHeaders(headers, rule->filterHeaders())) {
        return rule.get();
      }
    }
    return global_.get();
  }
  return global_.get();
}

AdmissionControlFilter::AdmissionControlFilter(AdmissionControlFilterConfigSharedPtr config,
                                               const std::string& stats_prefix)
    : config_(std::move(config)), stats_(generateStats(config_->scope(), stats_prefix)) {}

Http::FilterHeadersStatus AdmissionControlFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                                bool) {
  if (!config_->filterEnabled() || decoder_callbacks_->streamInfo().healthCheck()) {
    // We must forego recording the success/failure of this request during encoding.
    record_request_ = false;
    return Http::FilterHeadersStatus::Continue;
  }

  matched_rule_config_ = config_->findMatchingRule(headers);
  if (!matched_rule_config_) {
    record_request_ = false;
    return Http::FilterHeadersStatus::Continue;
  }

  uint32_t rps_threshold = matched_rule_config_->rpsThreshold();
  ThreadLocalController& controller = matched_rule_config_->getController();

  if (controller.averageRps() < rps_threshold) {
    ENVOY_LOG(debug, "Current rps: {} is below rps_threshold: {}, continue",
              controller.averageRps(), rps_threshold);
    return Http::FilterHeadersStatus::Continue;
  }

  if (shouldRejectRequest(matched_rule_config_)) {
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

  ResponseEvaluator& evaluator = matched_rule_config_->responseEvaluator();

  bool successful_response = false;
  if (Grpc::Common::isGrpcResponseHeaders(headers, end_stream)) {
    absl::optional<GrpcStatus> grpc_status = Grpc::Common::getGrpcStatus(headers);

    // If the GRPC status isn't found in the headers, it must be found in the trailers.
    expect_grpc_status_in_trailer_ = !grpc_status.has_value();
    if (expect_grpc_status_in_trailer_) {
      return Http::FilterHeadersStatus::Continue;
    }

    const uint32_t status = enumToInt(grpc_status.value());
    successful_response = evaluator.isGrpcSuccess(status);
  } else {
    // HTTP response.
    const uint64_t http_status = Http::Utility::getResponseStatus(headers);
    successful_response = evaluator.isHttpSuccess(http_status);
  }

  if (successful_response) {
    recordSuccess(matched_rule_config_);
  } else {
    recordFailure(matched_rule_config_);
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterTrailersStatus
AdmissionControlFilter::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  if (expect_grpc_status_in_trailer_) {
    ResponseEvaluator& evaluator = matched_rule_config_->responseEvaluator();

    absl::optional<GrpcStatus> grpc_status = Grpc::Common::getGrpcStatus(trailers, false);

    if (grpc_status.has_value() && evaluator.isGrpcSuccess(grpc_status.value())) {
      recordSuccess(matched_rule_config_);
    } else {
      recordFailure(matched_rule_config_);
    }
  }

  return Http::FilterTrailersStatus::Continue;
}

bool AdmissionControlFilter::shouldRejectRequest(
    const AdmissionControlRuleConfig* rule_config) const {
  // This formula is documented in the admission control filter documentation:
  // https://www.envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/admission_control_filter.html
  ThreadLocalController& controller = rule_config->getController();
  const auto request_counts = controller.requestCounts();
  const double total_requests = request_counts.requests;
  const double successful_requests = request_counts.successes;
  const double sr_threshold = rule_config->successRateThreshold();
  double probability = total_requests - successful_requests / sr_threshold;
  probability = probability / (total_requests + 1);
  const auto aggression = rule_config->aggression();
  if (aggression != 1.0) {
    probability = std::pow(probability, 1.0 / aggression);
  }
  const double max_rejection_probability = rule_config->maxRejectionProbability();
  probability = std::min<double>(probability, max_rejection_probability);

  // Choosing an accuracy of 4 significant figures for the probability.
  static constexpr uint64_t accuracy = 1e4;
  auto r = config_->random().random();
  return (accuracy * std::max(probability, 0.0)) > (r % accuracy);
}

} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
