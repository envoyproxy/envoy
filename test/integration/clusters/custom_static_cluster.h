#include <chrono>
#include <list>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
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
  CustomStaticCluster(const envoy::config::cluster::v3::Cluster& cluster, Runtime::Loader& runtime,
                      Server::Configuration::TransportSocketFactoryContextImpl& factory_context,
                      Stats::ScopePtr&& stats_scope, bool added_via_api, uint32_t priority,
                      std::string address, uint32_t port)
      : ClusterImplBase(cluster, runtime, factory_context, std::move(stats_scope), added_via_api),
        priority_(priority), address_(std::move(address)), port_(port), host_(makeHost()) {}

  InitializePhase initializePhase() const override { return InitializePhase::Primary; }

private:
  struct LbImpl : public Upstream::LoadBalancer {
    LbImpl(const Upstream::HostSharedPtr& host) : host_(host) {}

    Upstream::HostConstSharedPtr chooseHost(Upstream::LoadBalancerContext*) override {
      return host_;
    }

    const Upstream::HostSharedPtr host_;
  };

  struct LbFactory : public Upstream::LoadBalancerFactory {
    LbFactory(const Upstream::HostSharedPtr& host) : host_(host) {}

    Upstream::LoadBalancerPtr create() override { return std::make_unique<LbImpl>(host_); }

    const Upstream::HostSharedPtr host_;
  };

  struct ThreadAwareLbImpl : public Upstream::ThreadAwareLoadBalancer {
    ThreadAwareLbImpl(const Upstream::HostSharedPtr& host) : host_(host) {}

    Upstream::LoadBalancerFactorySharedPtr factory() override {
      return std::make_shared<LbFactory>(host_);
    }
    void initialize() override {}

    const Upstream::HostSharedPtr host_;
  };

  Upstream::ThreadAwareLoadBalancerPtr threadAwareLb();

  // ClusterImplBase
  void startPreInit() override;

  Upstream::HostSharedPtr makeHost();

  const uint32_t priority_;
  const std::string address_;
  const uint32_t port_;
  const Upstream::HostSharedPtr host_;

  friend class CustomStaticClusterFactoryBase;
};

class CustomStaticClusterFactoryBase : public Upstream::ConfigurableClusterFactoryBase<
                                           test::integration::clusters::CustomStaticConfig> {
protected:
  CustomStaticClusterFactoryBase(const std::string& name, bool create_lb)
      : ConfigurableClusterFactoryBase(name), create_lb_(create_lb) {}

private:
  std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>
  createClusterWithConfig(
      const envoy::config::cluster::v3::Cluster& cluster,
      const test::integration::clusters::CustomStaticConfig& proto_config,
      Upstream::ClusterFactoryContext& context,
      Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
      Stats::ScopePtr&& stats_scope) override {
    auto new_cluster = std::make_shared<CustomStaticCluster>(
        cluster, context.runtime(), socket_factory_context, std::move(stats_scope),
        context.addedViaApi(), proto_config.priority(), proto_config.address(),
        proto_config.port_value());
    return std::make_pair(new_cluster, create_lb_ ? new_cluster->threadAwareLb() : nullptr);
  }

  const bool create_lb_;
};

class CustomStaticClusterFactoryNoLb : public CustomStaticClusterFactoryBase {
public:
  CustomStaticClusterFactoryNoLb()
      : CustomStaticClusterFactoryBase("envoy.clusters.custom_static", false) {}
};

class CustomStaticClusterFactoryWithLb : public CustomStaticClusterFactoryBase {
public:
  CustomStaticClusterFactoryWithLb()
      : CustomStaticClusterFactoryBase("envoy.clusters.custom_static_with_lb", true) {}
};

} // namespace Envoy
