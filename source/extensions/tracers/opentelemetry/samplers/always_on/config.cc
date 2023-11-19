#include "source/extensions/tracers/opentelemetry/samplers/always_on/config.h"

#include "envoy/server/tracer_config.h"

#include "source/common/config/utility.h"
#include "source/extensions/tracers/opentelemetry/samplers/always_on/always_on_sampler.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

SamplerSharedPtr
AlwaysOnSamplerFactory::createSampler(const Protobuf::Message& config,
                                      Server::Configuration::TracerFactoryContext& context) {
  return std::make_shared<AlwaysOnSampler>(config, context);
}

/**
 * Static registration for the Env sampler factory. @see RegisterFactory.
 */
REGISTER_FACTORY(AlwaysOnSamplerFactory, SamplerFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
