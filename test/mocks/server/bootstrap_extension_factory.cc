#include "bootstrap_extension_factory.h"

namespace Envoy {
namespace Server {

MockBootstrapExtension::MockBootstrapExtension() = default;

MockBootstrapExtension::~MockBootstrapExtension() = default;

namespace Configuration {
MockBootstrapExtensionFactory::MockBootstrapExtensionFactory() = default;

MockBootstrapExtensionFactory::~MockBootstrapExtensionFactory() = default;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
