#pragma once

#include "envoy/upstream/load_balancer.h"

#include "source/common/common/logger.h"
#include "source/extensions/load_balancing_policies/common/factory_base.h"
#include "source/extensions/load_balancing_policies/peak_ewma/peak_ewma_lb.h"

#include "envoy/extensions/load_balancing_policies/peak_ewma/v3/peak_ewma.pb.h"
#include "envoy/extensions/load_balancing_policies/peak_ewma/v3/peak_ewma.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace PeakEwma {

using PeakEwmaLbProto = envoy::extensions::load_balancing_policies::peak_ewma::v3::PeakEwma;

class TypedPeakEwmaLbConfig : public Upstream::LoadBalancerConfig {
public:
  explicit TypedPeakEwmaLbConfig(const PeakEwmaLbProto& lb_config) : lb_config_(lb_config) {}

  PeakEwmaLbProto lb_config_;
};

struct PeakEwmaCreator : public Logger::Loggable<Logger::Id::upstream> {
  Upstream::LoadBalancerPtr operator()(
      Upstream::LoadBalancerParams params, OptRef<const Upstream::LoadBalancerConfig> lb_config,
      const Upstream::ClusterInfo& cluster_info, const Upstream::PrioritySet& priority_set,
      Runtime::Loader& runtime, Envoy::Random::RandomGenerator& random, TimeSource& time_source);
};

class Factory
    : public ::Envoy::Extensions::LoadBalancingPolicies::Common::FactoryBase<PeakEwmaLbProto,
                                                                             PeakEwmaCreator> {
public:
  Factory() : FactoryBase("envoy.load_balancing_policies.peak_ewma") {}

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadConfig(Server::Configuration::ServerFactoryContext&,
             const Protobuf::Message& config) override {
    const auto& typed_config = dynamic_cast<const PeakEwmaLbProto&>(config);
    return Upstream::LoadBalancerConfigPtr{new TypedPeakEwmaLbConfig(typed_config)};
  }
};

DECLARE_FACTORY(Factory);

} // namespace PeakEwma
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
