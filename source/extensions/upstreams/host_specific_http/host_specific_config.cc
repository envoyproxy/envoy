#include "source/extensions/upstreams/host_specific_http/host_specific_config.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace HostSpecificHttp {

// Register the factory
LEGACY_REGISTER_FACTORY(EpSpecificProtocolOptionsConfigFactory,
                        Server::Configuration::ProtocolOptionsFactory,
                        "envoy.upstreams.http.ep_specific");

} // namespace HostSpecificHttp
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
