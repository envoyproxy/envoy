#include "source/extensions/filters/http/admission_control/config.h"

#include "envoy/common/exception.h"
#include "envoy/extensions/filters/http/admission_control/v3/admission_control.pb.h"
#include "envoy/extensions/filters/http/admission_control/v3/admission_control.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/common/enum_to_int.h"
#include "source/extensions/filters/http/admission_control/admission_control.h"
#include "source/extensions/filters/http/admission_control/evaluators/response_evaluator.h"
#include "source/extensions/filters/http/admission_control/evaluators/success_criteria_evaluator.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {

static constexpr std::chrono::seconds defaultSamplingWindow{30};

absl::StatusOr<Http::FilterFactoryCb>
AdmissionControlFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::admission_control::v3::AdmissionControl& config,
    const std::string& stats_prefix, DualInfo dual_info,
    Server::Configuration::ServerFactoryContext& context) {
  if (config.has_sr_threshold() && config.sr_threshold().default_value().value() < 1.0) {
    return absl::InvalidArgumentError("Success rate threshold cannot be less than 1.0%.");
  }

  if (!config.rules().empty()) {
    for (const auto& rule : config.rules()) {
      if (rule.has_sr_threshold() && rule.sr_threshold().default_value().value() < 1.0) {
        return absl::InvalidArgumentError(
            "Success rate threshold cannot be less than 1.0% in rule.");
      }
    }
  }

  const std::string prefix = stats_prefix + "admission_control.";

  // Create the thread-local controller.
  auto tls = ThreadLocal::TypedSlot<ThreadLocalControllerImpl>::makeUnique(context.threadLocal());
  auto sampling_window = std::chrono::seconds(
      PROTOBUF_GET_MS_OR_DEFAULT(config, sampling_window, 1000 * defaultSamplingWindow.count()) /
      1000);
  tls->set([sampling_window, &context](Event::Dispatcher&) {
    return std::make_shared<ThreadLocalControllerImpl>(context.timeSource(), sampling_window);
  });

  std::unique_ptr<ResponseEvaluator> response_evaluator;
  switch (config.evaluation_criteria_case()) {
  case AdmissionControlProto::EvaluationCriteriaCase::kSuccessCriteria: {
    absl::StatusOr<std::unique_ptr<SuccessCriteriaEvaluator>> response_evaluator_or =
        SuccessCriteriaEvaluator::create(config.success_criteria());
    RETURN_IF_NOT_OK(response_evaluator_or.status());
    response_evaluator = std::move(response_evaluator_or.value());
    break;
  }
  case AdmissionControlProto::EvaluationCriteriaCase::EVALUATION_CRITERIA_NOT_SET:
    return absl::InvalidArgumentError("Evaluation criteria not set");
  }

  // Create rules from proto config
  std::vector<AdmissionControlRuleConfigSharedPtr> rules;
  if (!config.rules().empty()) {
    // Use new multi-rule configuration
    for (const auto& rule_proto : config.rules()) {
      // Create a thread-local controller for each rule
      auto rule_tls =
          ThreadLocal::TypedSlot<ThreadLocalControllerImpl>::makeUnique(context.threadLocal());
      const auto rule_sampling_window =
          std::chrono::seconds(PROTOBUF_GET_MS_OR_DEFAULT(rule_proto, sampling_window,
                                                          1000 * defaultSamplingWindow.count()) /
                               1000);
      rule_tls->set([rule_sampling_window, &context](Event::Dispatcher&) {
        return std::make_shared<ThreadLocalControllerImpl>(context.timeSource(),
                                                           rule_sampling_window);
      });

      // Create response evaluator for this rule
      std::shared_ptr<ResponseEvaluator> rule_evaluator;
      switch (rule_proto.evaluation_criteria_case()) {
      case AdmissionControlRuleProto::EvaluationCriteriaCase::kSuccessCriteria: {
        absl::StatusOr<std::unique_ptr<SuccessCriteriaEvaluator>> evaluator_or =
            SuccessCriteriaEvaluator::create(rule_proto.success_criteria());
        RETURN_IF_NOT_OK(evaluator_or.status());
        rule_evaluator = std::move(evaluator_or.value());
        break;
      }
      case AdmissionControlRuleProto::EvaluationCriteriaCase::EVALUATION_CRITERIA_NOT_SET:
        return absl::InvalidArgumentError("Evaluation criteria not set for admission control rule");
      }

      // Create filter headers for this rule
      auto rule_filter_headers =
          Http::HeaderUtility::buildHeaderDataVector(rule_proto.headers(), context);

      rules.push_back(std::make_shared<AdmissionControlRuleConfig>(
          rule_proto, context.runtime(), std::move(rule_tls), std::move(rule_evaluator),
          std::move(rule_filter_headers)));
    }
  }

  auto global = std::make_shared<AdmissionControlRuleConfig>(
      std::move(tls), std::move(response_evaluator),
      std::vector<Http::HeaderUtility::HeaderDataPtr>{},
      config.has_aggression()
          ? std::make_unique<Runtime::Double>(config.aggression(), context.runtime())
          : nullptr,
      config.has_sr_threshold()
          ? std::make_unique<Runtime::Percentage>(config.sr_threshold(), context.runtime())
          : nullptr,
      config.has_rps_threshold()
          ? std::make_unique<Runtime::UInt32>(config.rps_threshold(), context.runtime())
          : nullptr,
      config.has_max_rejection_probability()
          ? std::make_unique<Runtime::Percentage>(config.max_rejection_probability(),
                                                  context.runtime())
          : nullptr);

  AdmissionControlFilterConfigSharedPtr filter_config =
      std::make_shared<AdmissionControlFilterConfig>(
          config, context.runtime(), context.api().randomGenerator(), dual_info.scope,
          std::move(global), std::move(rules));

  return [filter_config, prefix](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<AdmissionControlFilter>(filter_config, prefix));
  };
}

/**
 * Static registration for the admission_control filter. @see RegisterFactory.
 */
REGISTER_FACTORY(AdmissionControlFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);
REGISTER_FACTORY(UpstreamAdmissionControlFilterFactory,
                 Server::Configuration::UpstreamHttpFilterConfigFactory);

} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
