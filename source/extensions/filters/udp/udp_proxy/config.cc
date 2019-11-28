#include "extensions/filters/udp/udp_proxy/config.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {

static Registry::RegisterFactory<UdpProxyFilterConfigFactory,
                                 Server::Configuration::NamedUdpListenerFilterConfigFactory>
    register_;

}
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
