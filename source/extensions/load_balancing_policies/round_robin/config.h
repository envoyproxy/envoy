#pragma once

#include "envoy/extensions/load_balancing_policies/round_robin/v3/round_robin.pb.h"
#include "envoy/extensions/load_balancing_policies/round_robin/v3/round_robin.pb.validate.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/common/logger.h"
#include "source/common/upstream/load_balancer_impl.h"
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

  Upstream::LoadBalancerConfigPtr loadConfig(const Protobuf::Message& config,
                                             ProtobufMessage::ValidationVisitor&) override {

    auto active_or_legacy = Common::ActiveOrLegacy<RoundRobinLbProto, ClusterProto>::get(&config);
    ASSERT(active_or_legacy.hasLegacy() || active_or_legacy.hasActive());

    return active_or_legacy.hasLegacy()
               ? Upstream::LoadBalancerConfigPtr{new LegacyRoundRobinLbConfig(
                     *active_or_legacy.legacy())}
               : Upstream::LoadBalancerConfigPtr{
                     new TypedRoundRobinLbConfig(*active_or_legacy.active())};
  }
};

DECLARE_FACTORY(Factory);

} // namespace RoundRobin
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
