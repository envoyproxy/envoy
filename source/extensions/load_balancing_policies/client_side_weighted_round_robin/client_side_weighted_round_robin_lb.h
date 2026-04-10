#pragma once

#include "envoy/extensions/load_balancing_policies/client_side_weighted_round_robin/v3/client_side_weighted_round_robin.pb.h"
#include "envoy/extensions/load_balancing_policies/round_robin/v3/round_robin.pb.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/thread_local/thread_local_object.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/callback_impl.h"
#include "source/extensions/load_balancing_policies/common/load_balancer_impl.h"
#include "source/extensions/load_balancing_policies/common/orca_weight_manager.h"
#include "source/extensions/load_balancing_policies/round_robin/round_robin_lb.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Upstream {

using ClientSideWeightedRoundRobinLbProto = envoy::extensions::load_balancing_policies::
    client_side_weighted_round_robin::v3::ClientSideWeightedRoundRobin;
using CommonLbConfig = envoy::config::cluster::v3::Cluster::CommonLbConfig;
using RoundRobinConfig = envoy::extensions::load_balancing_policies::round_robin::v3::RoundRobin;

/**
 * Load balancer config used to wrap the config proto.
 */
class ClientSideWeightedRoundRobinLbConfig : public Upstream::LoadBalancerConfig {
public:
  ClientSideWeightedRoundRobinLbConfig(const ClientSideWeightedRoundRobinLbProto& lb_proto,
                                       Event::Dispatcher& main_thread_dispatcher,
                                       ThreadLocal::SlotAllocator& tls_slot_allocator);

  // Parameters for weight calculation from Orca Load report.
  std::vector<std::string> metric_names_for_computing_utilization;
  double error_utilization_penalty;
  // Timing parameters for the weight update.
  std::chrono::milliseconds blackout_period;
  std::chrono::milliseconds weight_expiration_period;
  std::chrono::milliseconds weight_update_period;

  // Round robin proto overrides that we want to propagate to the worker RR LB (e.g., slow start).
  RoundRobinConfig round_robin_overrides_;

  Event::Dispatcher& main_thread_dispatcher_;
  ThreadLocal::SlotAllocator& tls_slot_allocator_;
};

/**
 * A client side weighted round robin load balancer. When in weighted mode, EDF
 * scheduling is used. When in not weighted mode, simple RR index selection is
 * used.
 */
class ClientSideWeightedRoundRobinLoadBalancer : public Upstream::ThreadAwareLoadBalancer,
                                                 protected Logger::Loggable<Logger::Id::upstream> {
public:
  // Thread local shim to store callbacks for weight updates of worker local lb.
  class ThreadLocalShim : public Envoy::ThreadLocal::ThreadLocalObject {
  public:
    Common::CallbackManager<void> apply_weights_cb_helper_;
  };

  // This class is used to handle the load balancing on the worker thread.
  class WorkerLocalLb : public RoundRobinLoadBalancer {
  public:
    WorkerLocalLb(const PrioritySet& priority_set, const PrioritySet* local_priority_set,
                  ClusterLbStats& stats, Runtime::Loader& runtime, Random::RandomGenerator& random,
                  const CommonLbConfig& common_config, const RoundRobinConfig& round_robin_config,
                  TimeSource& time_source, OptRef<ThreadLocalShim> tls_shim);

  private:
    friend class ClientSideWeightedRoundRobinLoadBalancerFriend;
    Common::CallbackHandlePtr apply_weights_cb_handle_;
  };

  // Factory used to create worker-local load balancer on the worker thread.
  class WorkerLocalLbFactory : public Upstream::LoadBalancerFactory {
  public:
    WorkerLocalLbFactory(const Upstream::ClusterInfo& cluster_info,
                         const Upstream::PrioritySet& priority_set, Runtime::Loader& runtime,
                         Envoy::Random::RandomGenerator& random, TimeSource& time_source,
                         ThreadLocal::SlotAllocator& tls,
                         const RoundRobinConfig& round_robin_config)
        : cluster_info_(cluster_info), priority_set_(priority_set), runtime_(runtime),
          random_(random), time_source_(time_source), round_robin_config_(round_robin_config) {
      tls_ = ThreadLocal::TypedSlot<ThreadLocalShim>::makeUnique(tls);
      tls_->set([](Envoy::Event::Dispatcher&) { return std::make_shared<ThreadLocalShim>(); });
    }

    Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams params) override;

    bool recreateOnHostChange() const override { return false; }

    Upstream::LoadBalancerPtr createWithCommonLbConfig(const CommonLbConfig& common_lb_config,
                                                       Upstream::LoadBalancerParams params);

    void applyWeightsToAllWorkers();

    std::unique_ptr<Envoy::ThreadLocal::TypedSlot<ThreadLocalShim>> tls_;

    const Upstream::ClusterInfo& cluster_info_;
    const Upstream::PrioritySet& priority_set_;
    Runtime::Loader& runtime_;
    Envoy::Random::RandomGenerator& random_;
    TimeSource& time_source_;
    const RoundRobinConfig round_robin_config_;
  };

public:
  ClientSideWeightedRoundRobinLoadBalancer(OptRef<const Upstream::LoadBalancerConfig> lb_config,
                                           const Upstream::ClusterInfo& cluster_info,
                                           const Upstream::PrioritySet& priority_set,
                                           Runtime::Loader& runtime,
                                           Envoy::Random::RandomGenerator& random,
                                           TimeSource& time_source);

private:
  friend class ClientSideWeightedRoundRobinLoadBalancerFriend;

  // {Upstream::ThreadAwareLoadBalancer} Interface implementation.
  Upstream::LoadBalancerFactorySharedPtr factory() override { return factory_; }
  absl::Status initialize() override;

  // Factory used to create worker-local load balancers on the worker thread.
  std::shared_ptr<WorkerLocalLbFactory> factory_;

  // ORCA weight manager handles all weight computation and host data management.
  std::unique_ptr<Extensions::LoadBalancingPolicies::Common::OrcaWeightManager>
      orca_weight_manager_;
};

} // namespace Upstream
} // namespace Envoy
