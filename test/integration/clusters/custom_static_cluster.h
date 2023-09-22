#pragma once

#include <chrono>
#include <list>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/http/codec.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/network/address_impl.h"
#include "source/common/upstream/cluster_factory_impl.h"
#include "source/server/transport_socket_config_impl.h"

#include "test/common/upstream/utility.h"
#include "test/integration/clusters/cluster_factory_config.pb.h"
#include "test/integration/clusters/cluster_factory_config.pb.validate.h"
#include "test/test_common/registry.h"

namespace Envoy {

class CustomStaticCluster : public Upstream::ClusterImplBase {
public:
  CustomStaticCluster(const envoy::config::cluster::v3::Cluster& cluster,
                      Upstream::ClusterFactoryContext& context, uint32_t priority,
                      std::string address, uint32_t port)
      : ClusterImplBase(cluster, context), priority_(priority), address_(std::move(address)),
        port_(port), host_(makeHost()) {}

  InitializePhase initializePhase() const override { return InitializePhase::Primary; }

private:
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
  absl::StatusOr<
      std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>
  createClusterWithConfig(const envoy::config::cluster::v3::Cluster& cluster,
                          const test::integration::clusters::CustomStaticConfig& proto_config,
                          Upstream::ClusterFactoryContext& context) override {
    auto new_cluster =
        std::make_shared<CustomStaticCluster>(cluster, context, proto_config.priority(),
                                              proto_config.address(), proto_config.port_value());
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
