#include "source/extensions/filters/udp/udp_proxy/router/router_impl.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace Router {

ClusterRouteEntry::ClusterRouteEntry(
    const envoy::extensions::filters::udp::udp_proxy::v3::Route& route)
    : cluster_name_(route.route().cluster()) {}

ConfigImpl::ConfigImpl(const envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig& config)
    : cluster_(config.cluster()), source_ips_trie_({}) {
  // TODO(zhxie): Not finished
}

RouteConstSharedPtr ConfigImpl::route(std::string& key) const {
  // TODO(zhxie): Not finished
  if (key.length() == 0) {
    return nullptr;
  }

  return nullptr;
}

} // namespace Router
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
