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

  Upstream::LoadBalancerConfigPtr loadConfig(ProtobufTypes::MessagePtr config,
                                             ProtobufMessage::ValidationVisitor& visitor) override {
    return std::make_unique<Envoy::Upstream::TypedRoundRobinLbConfig>(
        MessageUtil::downcastAndValidate<const RoundRobinLbProto&>(*config, visitor));
  }
};

} // namespace RoundRobin
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
