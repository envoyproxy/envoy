#pragma once

#include "envoy/extensions/load_balancing_policies/round_robin/v3/round_robin.pb.h"
#include "envoy/extensions/load_balancing_policies/round_robin/v3/round_robin.pb.validate.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/common/logger.h"
#include "source/extensions/load_balancing_policies/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace RoundRobin {

using RoundRobinLbProto = envoy::extensions::load_balancing_policies::round_robin::v3::RoundRobin;
using ClusterProto = envoy::config::cluster::v3::Cluster;
using LegacyRoundRobinLbProto = ClusterProto::RoundRobinLbConfig;

/**
 * Load balancer config that used to wrap the legacy proto config.
 */
class LegacyRoundRobinLbConfig : public Upstream::LoadBalancerConfig {
public:
  LegacyRoundRobinLbConfig(const ClusterProto& cluster);

  OptRef<const LegacyRoundRobinLbProto> lbConfig() const {
    if (lb_config_.has_value()) {
      return lb_config_.value();
    }
    return {};
  };

private:
  absl::optional<LegacyRoundRobinLbProto> lb_config_;
};

/**
 * Load balancer config that used to wrap the proto config.
 */
class TypedRoundRobinLbConfig : public Upstream::LoadBalancerConfig {
public:
  TypedRoundRobinLbConfig(const RoundRobinLbProto& lb_config);

  const RoundRobinLbProto lb_config_;
};

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
    // TODO(wbocode): to merge the legacy and typed config and related constructors into one.
    return Upstream::LoadBalancerConfigPtr{new TypedRoundRobinLbConfig(typed_config)};
  }

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadLegacy(Server::Configuration::ServerFactoryContext&, const ClusterProto& cluster) override {
    return Upstream::LoadBalancerConfigPtr{new LegacyRoundRobinLbConfig(cluster)};
  }
};

DECLARE_FACTORY(Factory);

} // namespace RoundRobin
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
