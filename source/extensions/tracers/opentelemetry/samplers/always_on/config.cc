#include "config.h"

#include "source/common/config/utility.h"

#include "always_on_sampler.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

SamplerPtr AlwaysOnSamplerFactory::createSampler(
    const Protobuf::Message& config) {
  return std::make_shared<AlwaysOnSampler>(config);
}

/**
 * Static registration for the Env sampler factory. @see RegisterFactory.
 */
REGISTER_FACTORY(AlwaysOnSamplerFactory, SamplerFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy