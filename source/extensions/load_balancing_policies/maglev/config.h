#pragma once

#include <memory>

#include "envoy/extensions/load_balancing_policies/maglev/v3/maglev.pb.h"
#include "envoy/extensions/load_balancing_policies/maglev/v3/maglev.pb.validate.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/upstream/load_balancer_factory_base.h"
#include "source/extensions/load_balancing_policies/common/factory_base.h"
#include "source/extensions/load_balancing_policies/maglev/maglev_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace Maglev {

using MaglevLbProto = envoy::extensions::load_balancing_policies::maglev::v3::Maglev;

class Factory : public Upstream::TypedLoadBalancerFactoryBase<MaglevLbProto> {
public:
  Factory() : TypedLoadBalancerFactoryBase("envoy.load_balancing_policies.maglev") {}

  Upstream::ThreadAwareLoadBalancerPtr create(OptRef<const Upstream::LoadBalancerConfig> lb_config,
                                              const Upstream::ClusterInfo& cluster_info,
                                              const Upstream::PrioritySet& priority_set,
                                              Runtime::Loader& runtime,
                                              Random::RandomGenerator& random,
                                              TimeSource& time_source) override;

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadConfig(Server::Configuration::ServerFactoryContext&,
             const Protobuf::Message& config) override {
    ASSERT(dynamic_cast<const MaglevLbProto*>(&config) != nullptr);
    const MaglevLbProto& typed_config = dynamic_cast<const MaglevLbProto&>(config);
    return Upstream::LoadBalancerConfigPtr{new Upstream::TypedMaglevLbConfig(typed_config)};
  }

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadLegacy(Server::Configuration::ServerFactoryContext&,
             const Upstream::ClusterProto& cluster) override {
    return Upstream::LoadBalancerConfigPtr{
        new Upstream::TypedMaglevLbConfig(cluster.common_lb_config(), cluster.maglev_lb_config())};
  }
};

} // namespace Maglev
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
