#include "extensions/filters/http/admission_control/config.h"

#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.h"
#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.validate.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/http/admission_control/admission_control.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {

Http::FilterFactoryCb AdmissionControlFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::admission_control::v3alpha::AdmissionControl& config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {

  std::string admctl_stats_prefix = stats_prefix + "admission_control.";
  AdmissionControlFilterConfigSharedPtr filter_config =
      std::make_shared<AdmissionControlFilterConfig>(config, context.runtime(),
                                                     std::move(admctl_stats_prefix),
                                                     context.scope(), context.timeSource());

  // TODO @tallen thread local
  auto state = std::make_shared<AdmissionControlState>(context.timeSource(), filter_config,
                                                       context.random());

  return [filter_config, state](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<AdmissionControlFilter>(filter_config, state));
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
