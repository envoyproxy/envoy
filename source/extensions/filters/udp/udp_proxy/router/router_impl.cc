#include "source/extensions/filters/udp/udp_proxy/router/router_impl.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace Router {

ClusterRouteEntry::ClusterRouteEntry(
    const envoy::extensions::filters::udp::udp_proxy::v3::Route& route)
    : cluster_name_(route.route().cluster()) {}

ClusterRouteEntry::ClusterRouteEntry(const std::string& cluster) : cluster_name_(cluster) {}

ConfigImpl::ConfigImpl(const envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig& config)
    : cluster_(std::make_shared<ClusterRouteEntry>(config.cluster())),
      source_ips_trie_(buildRouteTrie(config.route_config())),
      entries_(buildEntryList(config.cluster(), config.route_config())) {}

RouteConstSharedPtr ConfigImpl::route(Network::Address::InstanceConstSharedPtr address) const {
  if (!cluster_->routeEntry()->clusterName().empty()) {
    return cluster_;
  }

  const auto& data = source_ips_trie_.getData(address);
  if (!data.empty()) {
    ASSERT(data.size() == 1);
    return data.back();
  }

  return nullptr;
}

ConfigImpl::SourceIPsTrie ConfigImpl::buildRouteTrie(
    const envoy::extensions::filters::udp::udp_proxy::v3::RouteConfiguration& config) {
  std::vector<std::pair<RouteConstSharedPtr, std::vector<Network::Address::CidrRange>>>
      source_ips_list;
  source_ips_list.reserve(config.routes().size());

  auto convertAddress = [](const auto& prefix_ranges) -> std::vector<Network::Address::CidrRange> {
    std::vector<Network::Address::CidrRange> ips;
    ips.reserve(prefix_ranges.size());
    for (const auto& ip : prefix_ranges) {
      const auto& cidr_range = Network::Address::CidrRange::create(ip);
      ips.push_back(cidr_range);
    }
    return ips;
  };

  for (auto& route : config.routes()) {
    auto ranges = route.match().source_prefix_ranges();
    auto route_entry = std::make_shared<ClusterRouteEntry>(route);

    source_ips_list.push_back(make_pair(route_entry, convertAddress(ranges)));
  }

  return {source_ips_list, true};
}

std::vector<RouteEntryPtr> ConfigImpl::buildEntryList(
    const std::string& cluster,
    const envoy::extensions::filters::udp::udp_proxy::v3::RouteConfiguration& config) {
  auto set = absl::flat_hash_set<RouteEntryPtr>();

  if (!cluster.empty()) {
    set.emplace(std::make_shared<ClusterRouteEntry>(cluster));
  }

  for (const auto& route : config.routes()) {
    auto route_entry = std::make_shared<ClusterRouteEntry>(route);
    auto cluster_name = route_entry->routeEntry()->clusterName();
    if (!cluster_name.empty()) {
      set.emplace(std::make_shared<ClusterRouteEntry>(cluster_name));
    }
  }

  auto list = std::vector<RouteEntryPtr>();
  list.reserve(set.size());
  for (const auto& entry : set) {
    list.push_back(entry);
  }

  return list;
}

} // namespace Router
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
