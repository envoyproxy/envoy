#include "config.h"

#include "source/common/config/utility.h"

#include "environment_resource_detector.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

ResourceDetectorPtr EnvironmentResourceDetectorFactory::createResourceDetector(
    Server::Configuration::TracerFactoryContext& context) {
  return std::make_shared<EnvironmentResourceDetector>(context);
}

/**
 * Static registration for the Env resource detector factory. @see RegisterFactory.
 */
REGISTER_FACTORY(EnvironmentResourceDetectorFactory, ResourceDetectorFactory);

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
