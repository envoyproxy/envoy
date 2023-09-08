#include "config.h"

#include "source/common/config/utility.h"

#include "all_sampler.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

SamplerPtr AllSamplerFactory::createSampler(
    Server::Configuration::TracerFactoryContext& context) {

  return std::make_shared<AllSampler>(context);
}

/**
 * Static registration for the Env sampler factory. @see RegisterFactory.
 */
REGISTER_FACTORY(AllSamplerFactory, SamplerFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy