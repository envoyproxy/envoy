#include "config.h"

#include "source/common/config/utility.h"

#include "dynatrace_sampler.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

SamplerPtr DynatraceSamplerFactory::createSampler(
    const Protobuf::Message& message) {
  (void)message;
  return std::make_shared<DynatraceSampler>();
}

/**
 * Static registration for the Env sampler factory. @see RegisterFactory.
 */
REGISTER_FACTORY(DynatraceSamplerFactory, SamplerFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy