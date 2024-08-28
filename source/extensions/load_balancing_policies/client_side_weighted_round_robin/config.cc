#include "source/extensions/load_balancing_policies/client_side_weighted_round_robin/config.h"

#include "source/extensions/load_balancing_policies/client_side_weighted_round_robin/client_side_weighted_round_robin_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace ClientSideWeightedRoundRobin {

ClientSideWeightedRoundRobinLbConfig::ClientSideWeightedRoundRobinLbConfig(
    const ClientSideWeightedRoundRobinLbProto& lb_config,
    Envoy::Event::Dispatcher& main_thread_dispatcher)
    : lb_config_(lb_config), main_thread_dispatcher_(main_thread_dispatcher) {}

Upstream::LoadBalancerPtr ClientSideWeightedRoundRobinCreator::operator()(
    Upstream::LoadBalancerParams params, OptRef<const Upstream::LoadBalancerConfig> lb_config,
    const Upstream::ClusterInfo& cluster_info, const Upstream::PrioritySet&,
    Runtime::Loader& runtime, Random::RandomGenerator& random, TimeSource& time_source) {
  const auto typed_lb_config =
      dynamic_cast<const ClientSideWeightedRoundRobinLbConfig*>(lb_config.ptr());

  ENVOY_LOG_MISC(error,
                 "ClientSideWeightedRoundRobinCreator::operator() "
                 "main_thread_dispatcher {} IsMainThread {}",
                 reinterpret_cast<int64_t>(&typed_lb_config->main_thread_dispatcher_),
                 Envoy::Thread::MainThread::isMainThread());

  return std::make_unique<Upstream::ClientSideWeightedRoundRobinLoadBalancer>(
      params.priority_set, params.local_priority_set, cluster_info.lbStats(), runtime, random,
      cluster_info.lbConfig(), typed_lb_config->lb_config_, time_source,
      &typed_lb_config->main_thread_dispatcher_);
}

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(Factory, Upstream::TypedLoadBalancerFactory);

} // namespace ClientSideWeightedRoundRobin
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
