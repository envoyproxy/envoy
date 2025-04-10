#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/dynamic_modules/factory.h"

namespace Envoy {
namespace Server {
namespace Configuration {

REGISTER_FACTORY(DynamicModuleConfigFactory, NamedHttpFilterConfigFactory);
REGISTER_FACTORY(UpstreamDynamicModuleConfigFactory,
                 Server::Configuration::UpstreamHttpFilterConfigFactory);

} // namespace Configuration
} // namespace Server
} // namespace Envoy
