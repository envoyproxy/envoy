#include "source/extensions/load_balancing_policies/client_side_weighted_round_robin/client_side_weighted_round_robin_lb.h"

#include <memory>
#include <string>

#include "source/common/protobuf/utility.h"
#include "source/extensions/load_balancing_policies/common/load_balancer_impl.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Upstream {

namespace {

RoundRobinConfig getRoundRobinConfig(const CommonLbConfig& common_config,
                                     const RoundRobinConfig& override_config) {
  TypedRoundRobinLbConfig round_robin_config(common_config, Upstream::LegacyRoundRobinLbProto());
  if (override_config.has_slow_start_config()) {
    *round_robin_config.lb_config_.mutable_slow_start_config() =
        override_config.slow_start_config();
  }
  return round_robin_config.lb_config_;
}

} // namespace

ClientSideWeightedRoundRobinLbConfig::ClientSideWeightedRoundRobinLbConfig(
    const ClientSideWeightedRoundRobinLbProto& lb_proto, Event::Dispatcher& main_thread_dispatcher,
    ThreadLocal::SlotAllocator& tls_slot_allocator)
    : main_thread_dispatcher_(main_thread_dispatcher), tls_slot_allocator_(tls_slot_allocator) {
  ENVOY_LOG_MISC(trace, "ClientSideWeightedRoundRobinLbConfig config {}", lb_proto.DebugString());
  metric_names_for_computing_utilization =
      std::vector<std::string>(lb_proto.metric_names_for_computing_utilization().begin(),
                               lb_proto.metric_names_for_computing_utilization().end());
  error_utilization_penalty = lb_proto.error_utilization_penalty().value();
  blackout_period =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(lb_proto, blackout_period, 10000));
  weight_expiration_period = std::chrono::milliseconds(
      PROTOBUF_GET_MS_OR_DEFAULT(lb_proto, weight_expiration_period, 180000));
  weight_update_period =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(lb_proto, weight_update_period, 1000));

  if (lb_proto.has_slow_start_config()) {
    *round_robin_overrides_.mutable_slow_start_config() = lb_proto.slow_start_config();
  }
}

ClientSideWeightedRoundRobinLoadBalancer::WorkerLocalLb::WorkerLocalLb(
    const PrioritySet& priority_set, const PrioritySet* local_priority_set, ClusterLbStats& stats,
    Runtime::Loader& runtime, Random::RandomGenerator& random, const CommonLbConfig& common_config,
    const RoundRobinConfig& round_robin_config, TimeSource& time_source,
    OptRef<ThreadLocalShim> tls_shim)
    : RoundRobinLoadBalancer(priority_set, local_priority_set, stats, runtime, random,
                             PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(
                                 common_config, healthy_panic_threshold, 100, 50),
                             getRoundRobinConfig(common_config, round_robin_config), time_source) {
  if (tls_shim.has_value()) {
    apply_weights_cb_handle_ = tls_shim->apply_weights_cb_helper_.add([this]() {
      // Refresh the EDF scheduler on the hosts in priority set of the
      // worker-local load balancer on the worker thread.
      for (const HostSetPtr& host_set : priority_set_.hostSetsPerPriority()) {
        if (host_set != nullptr) {
          refresh(host_set->priority());
        }
      }
    });
  }
}

Upstream::LoadBalancerPtr ClientSideWeightedRoundRobinLoadBalancer::WorkerLocalLbFactory::create(
    Upstream::LoadBalancerParams params) {
  return createWithCommonLbConfig(cluster_info_.lbConfig(), params);
}

Upstream::LoadBalancerPtr
ClientSideWeightedRoundRobinLoadBalancer::WorkerLocalLbFactory::createWithCommonLbConfig(
    const CommonLbConfig& common_lb_config, Upstream::LoadBalancerParams params) {
  return std::make_unique<Upstream::ClientSideWeightedRoundRobinLoadBalancer::WorkerLocalLb>(
      params.priority_set, params.local_priority_set, cluster_info_.lbStats(), runtime_, random_,
      common_lb_config, round_robin_config_, time_source_, tls_->get());
}

void ClientSideWeightedRoundRobinLoadBalancer::WorkerLocalLbFactory::applyWeightsToAllWorkers() {
  tls_->runOnAllThreads([](OptRef<ThreadLocalShim> tls_shim) -> void {
    if (tls_shim.has_value()) {
      tls_shim->apply_weights_cb_helper_.runCallbacks();
    }
  });
}

ClientSideWeightedRoundRobinLoadBalancer::ClientSideWeightedRoundRobinLoadBalancer(
    OptRef<const Upstream::LoadBalancerConfig> lb_config, const Upstream::ClusterInfo& cluster_info,
    const Upstream::PrioritySet& priority_set, Runtime::Loader& runtime,
    Envoy::Random::RandomGenerator& random, TimeSource& time_source) {

  const auto* typed_lb_config =
      dynamic_cast<const ClientSideWeightedRoundRobinLbConfig*>(lb_config.ptr());
  ASSERT(typed_lb_config != nullptr);
  factory_ = std::make_shared<WorkerLocalLbFactory>(
      cluster_info, priority_set, runtime, random, time_source,
      typed_lb_config->tls_slot_allocator_, typed_lb_config->round_robin_overrides_);

  // Build OrcaWeightManagerConfig from the typed lb config.
  Extensions::LoadBalancingPolicies::Common::OrcaWeightManagerConfig orca_config{
      typed_lb_config->metric_names_for_computing_utilization,
      typed_lb_config->error_utilization_penalty,
      typed_lb_config->blackout_period,
      typed_lb_config->weight_expiration_period,
      typed_lb_config->weight_update_period,
  };
  orca_weight_manager_ =
      std::make_unique<Extensions::LoadBalancingPolicies::Common::OrcaWeightManager>(
          orca_config, priority_set, time_source, typed_lb_config->main_thread_dispatcher_,
          [factory = factory_]() { factory->applyWeightsToAllWorkers(); });
}

absl::Status ClientSideWeightedRoundRobinLoadBalancer::initialize() {
  return orca_weight_manager_->initialize();
}

} // namespace Upstream
} // namespace Envoy
