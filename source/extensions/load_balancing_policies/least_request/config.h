#pragma once

#include "envoy/extensions/load_balancing_policies/least_request/v3/least_request.pb.h"
#include "envoy/extensions/load_balancing_policies/least_request/v3/least_request.pb.validate.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/common/logger.h"
#include "source/extensions/load_balancing_policies/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace LeastRequest {

using LeastRequestLbProto =
    envoy::extensions::load_balancing_policies::least_request::v3::LeastRequest;

struct LeastRequestCreator : public Logger::Loggable<Logger::Id::upstream> {
  Upstream::LoadBalancerPtr
  operator()(Upstream::LoadBalancerParams params, OptRef<const LeastRequestLbProto> lb_config,
             const Upstream::ClusterInfo& cluster_info, const Upstream::PrioritySet& priority_set,
             Runtime::Loader& runtime, Random::RandomGenerator& random, TimeSource& time_source);
};

class Factory : public Common::FactoryBase<LeastRequestLbProto, LeastRequestCreator> {
public:
  Factory() : FactoryBase("envoy.load_balancing_policies.least_request") {}
};

} // namespace LeastRequest
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
