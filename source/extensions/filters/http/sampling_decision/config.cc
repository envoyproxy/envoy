#include "source/extensions/filters/http/sampling_decision/config.h"

#include "envoy/extensions/filters/http/sampling_decision/v3/sampling_decision.pb.h"
#include "envoy/extensions/filters/http/sampling_decision/v3/sampling_decision.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/sampling_decision/sampling_decision_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SamplingDecision {

Http::FilterFactoryCb SamplingDecisionFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::sampling_decision::v3::SamplingDecision& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  auto config = std::make_shared<SamplingDecisionConfig>(proto_config);
  return [config, &context](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<SamplingDecisionFilter>(
        config, context.serverFactoryContext().runtime(),
        context.serverFactoryContext().api().randomGenerator()));
  };
}

/**
 * Static registration for the sampling decision filter.
 */
REGISTER_FACTORY(SamplingDecisionFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace SamplingDecision
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
