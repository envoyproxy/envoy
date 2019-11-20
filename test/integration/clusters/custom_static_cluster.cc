#include "custom_static_cluster.h"

namespace Envoy {

// ClusterImplBase
void CustomStaticCluster::startPreInit() {
  Upstream::HostVector hosts{host_};
  auto hosts_ptr = std::make_shared<Upstream::HostVector>(hosts);

  priority_set_.updateHosts(
      priority_,
      Upstream::HostSetImpl::partitionHosts(hosts_ptr, Upstream::HostsPerLocalityImpl::empty()), {},
      hosts, {}, absl::nullopt);

  onPreInitComplete();
}

Upstream::HostSharedPtr CustomStaticCluster::makeHost() {
  Network::Address::InstanceConstSharedPtr address =
      Network::Utility::parseInternetAddress(address_, port_, true);
  return Upstream::HostSharedPtr{new Upstream::HostImpl(
      info(), "", address, info()->metadata(), 1,
      envoy::api::v2::core::Locality::default_instance(),
      envoy::api::v2::endpoint::Endpoint::HealthCheckConfig::default_instance(), priority_,
      envoy::api::v2::core::HealthStatus::UNKNOWN)};
}

Upstream::ThreadAwareLoadBalancerPtr CustomStaticCluster::threadAwareLb() {
  return std::make_unique<ThreadAwareLbImpl>(host_);
}

REGISTER_FACTORY(CustomStaticClusterFactoryNoLb, Upstream::ClusterFactory);
REGISTER_FACTORY(CustomStaticClusterFactoryWithLb, Upstream::ClusterFactory);

} // namespace Envoy
