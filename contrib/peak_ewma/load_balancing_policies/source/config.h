#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/common/logger.h"
#include "source/extensions/load_balancing_policies/common/factory_base.h"

#include "contrib/envoy/extensions/load_balancing_policies/peak_ewma/v3alpha/peak_ewma.pb.h"
#include "contrib/envoy/extensions/load_balancing_policies/peak_ewma/v3alpha/peak_ewma.pb.validate.h"
#include "contrib/peak_ewma/load_balancing_policies/source/peak_ewma_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace PeakEwma {

using PeakEwmaLbProto = envoy::extensions::load_balancing_policies::peak_ewma::v3alpha::PeakEwma;

class TypedPeakEwmaLbConfig : public Upstream::LoadBalancerConfig {
public:
  TypedPeakEwmaLbConfig(const PeakEwmaLbProto& lb_config, Event::Dispatcher& main_dispatcher)
      : lb_config_(lb_config), main_dispatcher_(main_dispatcher) {}

  PeakEwmaLbProto lb_config_;
  Event::Dispatcher& main_dispatcher_;
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
  loadConfig(Server::Configuration::ServerFactoryContext& context,
             const Protobuf::Message& config) override {
    const auto& typed_config = dynamic_cast<const PeakEwmaLbProto&>(config);
    return Upstream::LoadBalancerConfigPtr{
        new TypedPeakEwmaLbConfig(typed_config, context.mainThreadDispatcher())};
  }
};

DECLARE_FACTORY(Factory);

} // namespace PeakEwma
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
