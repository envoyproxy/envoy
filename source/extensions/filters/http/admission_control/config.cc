#include "extensions/filters/http/admission_control/config.h"

#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.h"
#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.validate.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/http/admission_control/admission_control.h"
#include "extensions/filters/http/admission_control/concurrency_controller/gradient_controller.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {

Http::FilterFactoryCb AdmissionControlFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::admission_control::v3alpha::AdmissionControl&
        config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {

  // TODO @tallen
  AdmissionControlFilterConfigSharedPtr filter_config(
      new AdmissionControlFilterConfig(config, context.runtime(), std::move(admctl_stats_prefix),
                                          context.scope(), context.timeSource()));

  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(
        std::make_shared<AdmissionControlFilter>(filter_config));
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
