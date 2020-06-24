#include "extensions/filters/http/admission_control/config.h"

#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.h"
#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/common/enum_to_int.h"

#include "extensions/filters/http/admission_control/admission_control.h"
#include "extensions/filters/http/admission_control/evaluators/response_evaluator.h"
#include "extensions/filters/http/admission_control/evaluators/success_criteria_evaluator.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {

static constexpr std::chrono::seconds defaultSamplingWindow{120};

Http::FilterFactoryCb AdmissionControlFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::admission_control::v3alpha::AdmissionControl& config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {

  const std::string prefix = stats_prefix + "admission_control.";

  // Create the thread-local controller.
  auto tls = context.threadLocal().allocateSlot();
  auto sampling_window = std::chrono::seconds(
      PROTOBUF_GET_MS_OR_DEFAULT(config, sampling_window, 1000 * defaultSamplingWindow.count()) /
      1000);
  tls->set(
      [sampling_window, &context](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
        return std::make_shared<ThreadLocalControllerImpl>(context.timeSource(), sampling_window);
      });

  std::unique_ptr<ResponseEvaluator> response_evaluator;
  switch (config.evaluation_criteria_case()) {
  case AdmissionControlProto::EvaluationCriteriaCase::kSuccessCriteria:
    response_evaluator = std::make_unique<SuccessCriteriaEvaluator>(config.success_criteria());
    break;
  case AdmissionControlProto::EvaluationCriteriaCase::EVALUATION_CRITERIA_NOT_SET:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  AdmissionControlFilterConfigSharedPtr filter_config =
      std::make_shared<AdmissionControlFilterConfig>(config, context.runtime(), context.random(),
                                                     context.scope(), std::move(tls),
                                                     std::move(response_evaluator));

  return [filter_config, prefix](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<AdmissionControlFilter>(filter_config, prefix));
  };
}

/**
 * Static registration for the admission_control filter. @see RegisterFactory.
 */
REGISTER_FACTORY(AdmissionControlFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
