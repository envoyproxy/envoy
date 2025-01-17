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
using ClusterProto = envoy::config::cluster::v3::Cluster;

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

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadConfig(Server::Configuration::ServerFactoryContext&,
             const Protobuf::Message& config) override {
    ASSERT(dynamic_cast<const RandomLbProto*>(&config) != nullptr);
    const RandomLbProto& typed_config = dynamic_cast<const RandomLbProto&>(config);
    // TODO(wbocode): to merge the legacy and typed config and related constructors into one.
    return Upstream::LoadBalancerConfigPtr{new TypedRandomLbConfig(typed_config)};
  }

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadLegacy(Server::Configuration::ServerFactoryContext&, const ClusterProto&) override {
    return Upstream::LoadBalancerConfigPtr{new EmptyRandomLbConfig()};
  }
};

DECLARE_FACTORY(Factory);

} // namespace Random
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
