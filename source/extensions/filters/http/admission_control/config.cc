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
  return createFilterFactory(config, stats_prefix, context, dual_info.scope);
}

Http::FilterFactoryCb
AdmissionControlFilterFactory::createFilterFactoryFromProtoWithServerContextTyped(
    const envoy::extensions::filters::http::admission_control::v3::AdmissionControl& proto_config,
    const std::string& stats_prefix, Server::Configuration::ServerFactoryContext& context) {
  auto cb = createFilterFactory(proto_config, stats_prefix, context, context.scope());
  THROW_IF_NOT_OK_REF(cb.status());
  return cb.value();
}

absl::StatusOr<Http::FilterFactoryCb> AdmissionControlFilterFactory::createFilterFactory(
    const envoy::extensions::filters::http::admission_control::v3::AdmissionControl& config,
    const std::string& stats_prefix, Server::Configuration::ServerFactoryContext& context,
    Stats::Scope& scope) {
  if (config.has_sr_threshold() && config.sr_threshold().default_value().value() < 1.0) {
    return absl::InvalidArgumentError("Success rate threshold cannot be less than 1.0%.");
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

  AdmissionControlFilterConfigSharedPtr filter_config =
      std::make_shared<AdmissionControlFilterConfig>(config, context.runtime(),
                                                     context.api().randomGenerator(), scope,
                                                     std::move(tls), std::move(response_evaluator));

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
