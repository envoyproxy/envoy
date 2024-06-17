#include "custom_static_cluster.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

#include "test/integration/load_balancers/custom_lb_policy.h"

namespace Envoy {

// ClusterImplBase
void CustomStaticCluster::startPreInit() {
  Upstream::HostVector hosts{host_};
  auto hosts_ptr = std::make_shared<Upstream::HostVector>(hosts);

  priority_set_.updateHosts(
      priority_,
      Upstream::HostSetImpl::partitionHosts(hosts_ptr, Upstream::HostsPerLocalityImpl::empty()), {},
      hosts, {}, 123, absl::nullopt);

  onPreInitComplete();
}

Upstream::HostSharedPtr CustomStaticCluster::makeHost() {
  Network::Address::InstanceConstSharedPtr address =
      Network::Utility::parseInternetAddressNoThrow(address_, port_, true);
  return Upstream::HostSharedPtr{new Upstream::HostImpl(
      info(), "", address,
      std::make_shared<const envoy::config::core::v3::Metadata>(info()->metadata()), nullptr, 1,
      envoy::config::core::v3::Locality::default_instance(),
      envoy::config::endpoint::v3::Endpoint::HealthCheckConfig::default_instance(), priority_,
      envoy::config::core::v3::UNKNOWN, time_source_)};
}

Upstream::ThreadAwareLoadBalancerPtr CustomStaticCluster::threadAwareLb() {
  return std::make_unique<ThreadAwareLbImpl>(host_);
}

REGISTER_FACTORY(CustomStaticClusterFactoryNoLb, Upstream::ClusterFactory);
REGISTER_FACTORY(CustomStaticClusterFactoryWithLb, Upstream::ClusterFactory);

} // namespace Envoy
