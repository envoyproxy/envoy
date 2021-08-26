#pragma once

#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.h"

#include "source/common/network/lc_trie.h"
#include "source/extensions/filters/udp/udp_proxy/router/router.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace Router {

class ClusterRouteEntry : public RouteEntry, public Route {
public:
  ClusterRouteEntry(const envoy::extensions::filters::udp::udp_proxy::v3::Route& route);
  ClusterRouteEntry(const std::string& cluster);
  ~ClusterRouteEntry() override = default;

  // Router::RouteEntry
  const std::string& clusterName() const override { return cluster_name_; }

  // Router::Route
  const RouteEntry* routeEntry() const override { return this; }

private:
  const std::string cluster_name_;
};

class ConfigImpl : public Config {
public:
  ConfigImpl(const envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig& config);
  ~ConfigImpl() override = default;

  // Router::Config
  RouteConstSharedPtr route(Network::Address::InstanceConstSharedPtr address) const override;
  const std::vector<RouteEntryConstSharedPtr>& entries() const override { return entries_; }

private:
  using SourceIPsTrie = Network::LcTrie::LcTrie<RouteConstSharedPtr>;
  using RouteConfiguration = envoy::extensions::filters::udp::udp_proxy::v3::RouteConfiguration;

  RouteConstSharedPtr cluster_;
  const SourceIPsTrie source_ips_trie_;
  const std::vector<RouteEntryConstSharedPtr> entries_;

  SourceIPsTrie buildRouteTrie(const RouteConfiguration& config);
  std::vector<RouteEntryConstSharedPtr> buildEntryList(const std::string& cluster,
                                                       const RouteConfiguration& config);
};

} // namespace Router
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
