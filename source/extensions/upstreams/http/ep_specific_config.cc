#include "source/extensions/upstreams/http/ep_specific_config.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {

// Register the factory
LEGACY_REGISTER_FACTORY(EpSpecificProtocolOptionsConfigFactory,
                        Server::Configuration::ProtocolOptionsFactory,
                        "envoy.upstreams.http.ep_specific");

} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
