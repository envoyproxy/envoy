#pragma once

#include "envoy/extensions/load_balancing_policies/round_robin/v3/round_robin.pb.h"
#include "envoy/extensions/load_balancing_policies/round_robin/v3/round_robin.pb.validate.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/common/logger.h"
#include "source/extensions/load_balancing_policies/common/factory_base.h"
#include "source/extensions/load_balancing_policies/round_robin/round_robin_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace RoundRobin {

using RoundRobinLbProto = envoy::extensions::load_balancing_policies::round_robin::v3::RoundRobin;
using ClusterProto = envoy::config::cluster::v3::Cluster;

struct RoundRobinCreator : public Logger::Loggable<Logger::Id::upstream> {
  Upstream::LoadBalancerPtr operator()(Upstream::LoadBalancerParams params,
                                       OptRef<const Upstream::LoadBalancerConfig> lb_config,
                                       const Upstream::ClusterInfo& cluster_info,
                                       const Upstream::PrioritySet& priority_set,
                                       Runtime::Loader& runtime, Random::RandomGenerator& random,
                                       TimeSource& time_source);
};

class Factory : public Common::FactoryBase<RoundRobinLbProto, RoundRobinCreator> {
public:
  Factory() : FactoryBase("envoy.load_balancing_policies.round_robin") {}

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadConfig(Server::Configuration::ServerFactoryContext&,
             const Protobuf::Message& config) override {
    ASSERT(dynamic_cast<const RoundRobinLbProto*>(&config) != nullptr);
    const RoundRobinLbProto& typed_config = dynamic_cast<const RoundRobinLbProto&>(config);
    return Upstream::LoadBalancerConfigPtr{new Upstream::TypedRoundRobinLbConfig(typed_config)};
  }

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadLegacy(Server::Configuration::ServerFactoryContext&, const ClusterProto& cluster) override {
    return Upstream::LoadBalancerConfigPtr{new Upstream::TypedRoundRobinLbConfig(
        cluster.common_lb_config(), cluster.round_robin_lb_config())};
  }
};

DECLARE_FACTORY(Factory);

} // namespace RoundRobin
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
