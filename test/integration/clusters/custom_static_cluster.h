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

template <class ConfigType> class CustomStaticClusterFactoryBase;

class CustomStaticCluster : public Upstream::ClusterImplBase {
public:
  CustomStaticCluster(const envoy::config::cluster::v3::Cluster& cluster,
                      Upstream::ClusterFactoryContext& context, uint32_t priority,
                      std::string address, uint32_t port, absl::Status& creation_status)
      : ClusterImplBase(cluster, context, creation_status), priority_(priority),
        address_(std::move(address)), port_(port) {
    THROW_IF_NOT_OK_REF(creation_status);
    host_ = makeHost();
  }

  InitializePhase initializePhase() const override { return InitializePhase::Primary; }

private:
  Upstream::ThreadAwareLoadBalancerPtr threadAwareLb();

  // ClusterImplBase
  void startPreInit() override;

  Upstream::HostSharedPtr makeHost();

  const uint32_t priority_;
  const std::string address_;
  const uint32_t port_;
  Upstream::HostSharedPtr host_;

  friend class CustomStaticClusterFactoryBase<test::integration::clusters::CustomStaticConfig1>;
  friend class CustomStaticClusterFactoryBase<test::integration::clusters::CustomStaticConfig2>;
};

template <class ConfigProto>
class CustomStaticClusterFactoryBase
    : public Upstream::ConfigurableClusterFactoryBase<ConfigProto> {
protected:
  CustomStaticClusterFactoryBase(const std::string& name, bool create_lb)
      : Upstream::ConfigurableClusterFactoryBase<ConfigProto>(name), create_lb_(create_lb) {}

private:
  absl::StatusOr<
      std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>
  createClusterWithConfig(const envoy::config::cluster::v3::Cluster& cluster,
                          const ConfigProto& proto_config,
                          Upstream::ClusterFactoryContext& context) override {
    absl::Status creation_status = absl::OkStatus();
    auto new_cluster = std::make_shared<CustomStaticCluster>(
        cluster, context, proto_config.priority(), proto_config.address(),
        proto_config.port_value(), creation_status);
    THROW_IF_NOT_OK_REF(creation_status);
    return std::make_pair(new_cluster, create_lb_ ? new_cluster->threadAwareLb() : nullptr);
  }

  const bool create_lb_;
};

class CustomStaticClusterFactoryNoLb
    : public CustomStaticClusterFactoryBase<test::integration::clusters::CustomStaticConfig1> {
public:
  CustomStaticClusterFactoryNoLb()
      : CustomStaticClusterFactoryBase("envoy.clusters.custom_static", false) {};
};

class CustomStaticClusterFactoryWithLb
    : public CustomStaticClusterFactoryBase<test::integration::clusters::CustomStaticConfig2> {
public:
  CustomStaticClusterFactoryWithLb()
      : CustomStaticClusterFactoryBase("envoy.clusters.custom_static_with_lb", true) {}
};

} // namespace Envoy
