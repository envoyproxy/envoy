#pragma once

#include <memory>
#include <utility>

#include "envoy/extensions/load_balancing_policies/wrr_locality/v3/wrr_locality.pb.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/thread_local/thread_local_object.h"
#include "envoy/upstream/upstream.h"

#include "source/common/config/utility.h"
#include "source/common/upstream/load_balancer_factory_base.h"
#include "source/extensions/load_balancing_policies/client_side_weighted_round_robin/config.h"
#include "source/extensions/load_balancing_policies/common/load_balancer_impl.h"
#include "source/extensions/load_balancing_policies/round_robin/round_robin_lb.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace WrrLocality {

using ::Envoy::Logger::Loggable;
using ::Envoy::Upstream::ThreadAwareLoadBalancer;
using ::Envoy::Upstream::TypedLoadBalancerFactoryBase;

using WrrLocalityLbProto =
    envoy::extensions::load_balancing_policies::wrr_locality::v3::WrrLocality;

/**
 * Load balancer config used to wrap the config proto.
 */
class WrrLocalityLbConfig : public Upstream::LoadBalancerConfig {
public:
  WrrLocalityLbConfig(Upstream::TypedLoadBalancerFactory& endpoint_picking_policy_factory,
                      Upstream::LoadBalancerConfigPtr endpoint_picking_policy_config)
      : endpoint_picking_policy_factory_(endpoint_picking_policy_factory),
        endpoint_picking_policy_config_(std::move(endpoint_picking_policy_config)) {}

  Upstream::TypedLoadBalancerFactory& endpoint_picking_policy_factory_;
  Upstream::LoadBalancerConfigPtr endpoint_picking_policy_config_;
};

/*
 * Weighted Round Robin Locality policy. Wraps Client Side Weighted Round Robin
 * policy to enable locality weights.
 */
class WrrLocalityLoadBalancer : public ThreadAwareLoadBalancer,
                                protected Loggable<::Envoy::Logger::Id::upstream> {
public:
  WrrLocalityLoadBalancer(OptRef<const Upstream::LoadBalancerConfig> lb_config,
                          const Upstream::ClusterInfo& cluster_info,
                          const Upstream::PrioritySet& priority_set, Runtime::Loader& runtime,
                          Envoy::Random::RandomGenerator& random, TimeSource& time_source);

  // {Upstream::ThreadAwareLoadBalancer} Interface implementation.
  Upstream::LoadBalancerFactorySharedPtr factory() override { return factory_; }
  absl::Status initialize() override { return endpoint_picking_policy_->initialize(); };

  // Factory used to create worker-local load balancer on the worker thread.
  class WorkerLocalLbFactory : public Upstream::LoadBalancerFactory {
  public:
    WorkerLocalLbFactory(const Upstream::ClusterInfo& cluster_info,
                         Upstream::LoadBalancerFactorySharedPtr endpoint_picking_policy_factory)
        : cluster_info_(cluster_info),
          endpoint_picking_policy_factory_(std::move(endpoint_picking_policy_factory)) {}

    Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams params) override;

    bool recreateOnHostChange() const override { return false; }

    const Upstream::ClusterInfo& cluster_info_;
    Upstream::LoadBalancerFactorySharedPtr endpoint_picking_policy_factory_;
  };

private:
  Upstream::ThreadAwareLoadBalancerPtr endpoint_picking_policy_;
  // Factory used to create worker-local load balancers on the worker thread.
  std::shared_ptr<WorkerLocalLbFactory> factory_;
};

} // namespace WrrLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
