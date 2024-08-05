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

/**
 * Empty load balancer config that used to represent the config for the random load balancer.
 */
class EmptyRandomLbConfig : public Upstream::LoadBalancerConfig {
public:
  EmptyRandomLbConfig() = default;
};

/**
 * Load balancer config that used to wrap the random config.
 */
class TypedRandomLbConfig : public Upstream::LoadBalancerConfig {
public:
  TypedRandomLbConfig(const RandomLbProto& lb_config);

  const RandomLbProto lb_config_;
};

struct RandomCreator : public Logger::Loggable<Logger::Id::upstream> {
  Upstream::LoadBalancerPtr operator()(
      Upstream::LoadBalancerParams params, OptRef<const Upstream::LoadBalancerConfig> lb_config,
      const Upstream::ClusterInfo& cluster_info, const Upstream::PrioritySet& priority_set,
      Runtime::Loader& runtime, Envoy::Random::RandomGenerator& random, TimeSource& time_source);
};

class Factory : public Common::FactoryBase<RandomLbProto, RandomCreator> {
public:
  Factory() : FactoryBase("envoy.load_balancing_policies.random") {}

  Upstream::LoadBalancerConfigPtr loadConfig(Upstream::LoadBalancerFactoryContext&,
                                             const Protobuf::Message& config,
                                             ProtobufMessage::ValidationVisitor&) override {
    auto typed_config = dynamic_cast<const RandomLbProto*>(&config);
    if (typed_config == nullptr) {
      return std::make_unique<EmptyRandomLbConfig>();
    }
    return std::make_unique<TypedRandomLbConfig>(*typed_config);
  }
};

DECLARE_FACTORY(Factory);

} // namespace Random
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
