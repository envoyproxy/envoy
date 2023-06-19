#pragma once

#include "envoy/extensions/load_balancing_policies/random/v3/random.pb.h"
#include "envoy/extensions/load_balancing_policies/random/v3/random.pb.validate.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/common/logger.h"
#include "source/extensions/load_balancing_policies/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace Random {

using RandomLbProto = envoy::extensions::load_balancing_policies::random::v3::Random;

struct RandomCreator : public Logger::Loggable<Logger::Id::upstream> {
  Upstream::LoadBalancerPtr
  operator()(Upstream::LoadBalancerParams params, OptRef<const RandomLbProto> lb_config,
             const Upstream::ClusterInfo& cluster_info, const Upstream::PrioritySet& priority_set,
             Runtime::Loader& runtime, Envoy::Random::RandomGenerator& random,
             TimeSource& time_source);
};

class Factory : public Common::FactoryBase<RandomLbProto, RandomCreator> {
public:
  Factory() : FactoryBase("envoy.load_balancing_policies.random") {}
};

} // namespace Random
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
