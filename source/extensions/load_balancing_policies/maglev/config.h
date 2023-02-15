#pragma once

#include <memory>

#include "envoy/extensions/load_balancing_policies/maglev/v3/maglev.pb.h"
#include "envoy/extensions/load_balancing_policies/maglev/v3/maglev.pb.validate.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/upstream/load_balancer_factory_base.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace Maglev {

class Factory : public Upstream::TypedLoadBalancerFactoryBase<
                    envoy::extensions::load_balancing_policies::maglev::v3::Maglev> {
public:
  Factory() : TypedLoadBalancerFactoryBase("envoy.load_balancing_policies.maglev") {}

  Upstream::ThreadAwareLoadBalancerPtr create(const Upstream::ClusterInfo& cluster_info,
                                              const Upstream::PrioritySet& priority_set,
                                              Runtime::Loader& runtime,
                                              Random::RandomGenerator& random,
                                              TimeSource& time_source) override;
};

} // namespace Maglev
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
