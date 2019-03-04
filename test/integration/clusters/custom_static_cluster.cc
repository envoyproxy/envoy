#include <chrono>
#include <list>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/http/codec.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/network/address_impl.h"
#include "common/upstream/cluster_factory_impl.h"

#include "server/transport_socket_config_impl.h"

#include "test/common/upstream/utility.h"
#include "test/integration/clusters/cluster_factory_config.pb.h"
#include "test/integration/clusters/cluster_factory_config.pb.validate.h"
#include "test/test_common/registry.h"

namespace Envoy {

class CustomStaticCluster : public Upstream::ClusterImplBase {
public:
  CustomStaticCluster(const envoy::api::v2::Cluster& cluster, Runtime::Loader& runtime,
                      Server::Configuration::TransportSocketFactoryContext& factory_context,
                      Stats::ScopePtr&& stats_scope, bool added_via_api, uint32_t priority,
                      std::string address, uint32_t port)
      : ClusterImplBase(cluster, runtime, factory_context, std::move(stats_scope), added_via_api),
        priority_(priority), address_(std::move(address)), port_(port) {
    printf("CustomStaticCluster created");
  }

  InitializePhase initializePhase() const override { return InitializePhase::Primary; }

private:
  // ClusterImplBase
  void startPreInit() override {
    printf("startPreInit");
    Upstream::HostSharedPtr host = makeHost();
    Upstream::HostVector hosts{host};
    auto hosts_ptr = std::make_shared<Upstream::HostVector>(hosts);

    this->priority_set_.updateHosts(
        priority_,
        Upstream::HostSetImpl::partitionHosts(hosts_ptr, Upstream::HostsPerLocalityImpl::empty()),
        {}, hosts, {}, absl::nullopt);

    onPreInitComplete();
  }

  inline Upstream::HostSharedPtr makeHost() {
    Network::Address::InstanceConstSharedPtr address =
        Network::Utility::parseInternetAddress(address_, port_, false);
    return Upstream::HostSharedPtr{new Upstream::HostImpl(
        this->info(), "", address, this->info()->metadata(), 1,
        envoy::api::v2::core::Locality::default_instance(),
        envoy::api::v2::endpoint::Endpoint::HealthCheckConfig::default_instance(), priority_,
        envoy::api::v2::core::HealthStatus::UNKNOWN)};
  }

  const uint32_t priority_;
  const std::string address_;
  const uint32_t port_;
};

class CustomStaticClusterFactory : public Upstream::ConfigurableClusterFactoryBase<
                                       test::integration::clusters::CustomStaticConfig> {
public:
  CustomStaticClusterFactory() : ConfigurableClusterFactoryBase("envoy.clusters.custom_static") {}

private:
  Upstream::ClusterImplBaseSharedPtr createClusterWithConfig(
      const envoy::api::v2::Cluster& cluster,
      const test::integration::clusters::CustomStaticConfig& proto_config,
      Upstream::ClusterFactoryContext& context,
      Server::Configuration::TransportSocketFactoryContext& socket_factory_context,
      Stats::ScopePtr&& stats_scope) override {
    return std::make_unique<CustomStaticCluster>(cluster, context.runtime(), socket_factory_context,
                                                 std::move(stats_scope), context.addedViaApi(),
                                                 proto_config.priority(), proto_config.address(),
                                                 proto_config.port_value());
  }
};

REGISTER_FACTORY(CustomStaticClusterFactory, Upstream::ClusterFactory);

} // namespace Envoy