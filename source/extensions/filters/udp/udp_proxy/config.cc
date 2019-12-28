#include "extensions/filters/udp/udp_proxy/config.h"

#include "envoy/config/filter/udp/udp_proxy/v2alpha/udp_proxy.pb.validate.h"

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
