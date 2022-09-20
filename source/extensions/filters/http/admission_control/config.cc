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

Http::FilterFactoryCb AdmissionControlFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::admission_control::v3::AdmissionControl& config,
    const std::string& stats_prefix, DualInfo dual_info,
    Server::Configuration::ServerFactoryContext& context) {
  if (config.has_sr_threshold() && config.sr_threshold().default_value().value() < 1.0) {
    throw EnvoyException("Success rate threshold cannot be less than 1.0%.");
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
  case AdmissionControlProto::EvaluationCriteriaCase::kSuccessCriteria:
    response_evaluator = std::make_unique<SuccessCriteriaEvaluator>(config.success_criteria());
    break;
  case AdmissionControlProto::EvaluationCriteriaCase::EVALUATION_CRITERIA_NOT_SET:
    throw EnvoyException("Evaluation criteria not set");
  }

  AdmissionControlFilterConfigSharedPtr filter_config =
      std::make_shared<AdmissionControlFilterConfig>(
          config, context.runtime(), context.api().randomGenerator(), dual_info.scope,
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
