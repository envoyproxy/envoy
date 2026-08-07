#pragma once

#include "envoy/common/optref.h"
#include "envoy/common/random_generator.h"
#include "envoy/common/time.h"
#include "envoy/extensions/load_balancing_policies/header_order/v3/header_order.pb.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/upstream/load_balancer_factory_base.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace HeaderOrder {

using ::envoy::extensions::load_balancing_policies::header_order::v3::HeaderOrder;
using ::Envoy::Random::RandomGenerator;
using ::Envoy::Runtime::Loader;
using ::Envoy::Upstream::ClusterInfo;
using ::Envoy::Upstream::PrioritySet;

class HeaderOrderLoadBalancerFactory
    : public Upstream::TypedLoadBalancerFactoryBase<HeaderOrder> {
public:
  HeaderOrderLoadBalancerFactory()
      : TypedLoadBalancerFactoryBase("envoy.load_balancing_policies.header_order") {}

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadConfig(Server::Configuration::ServerFactoryContext& context,
             const Protobuf::Message& config) override;

  Upstream::ThreadAwareLoadBalancerPtr
  create(Envoy::OptRef<const Upstream::LoadBalancerConfig> lb_config,
         const ClusterInfo& cluster_info, const PrioritySet& priority_set, Loader& runtime,
         RandomGenerator& random, TimeSource& time_source) override;
};

} // namespace HeaderOrder
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
