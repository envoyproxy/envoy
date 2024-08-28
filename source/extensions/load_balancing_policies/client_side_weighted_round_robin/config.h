#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/extensions/load_balancing_policies/client_side_weighted_round_robin/v3/client_side_weighted_round_robin.pb.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/common/logger.h"
#include "source/extensions/load_balancing_policies/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace ClientSideWeightedRoundRobin {

using ClientSideWeightedRoundRobinLbProto = envoy::extensions::load_balancing_policies::
    client_side_weighted_round_robin::v3::ClientSideWeightedRoundRobin;
using ClusterProto = envoy::config::cluster::v3::Cluster;

/**
 * Load balancer config that used to wrap the proto config.
 */
class ClientSideWeightedRoundRobinLbConfig : public Upstream::LoadBalancerConfig {
public:
  ClientSideWeightedRoundRobinLbConfig(const ClientSideWeightedRoundRobinLbProto& lb_config,
                                       Event::Dispatcher& main_thread_dispatcher);

  const ClientSideWeightedRoundRobinLbProto lb_config_;
  Event::Dispatcher& main_thread_dispatcher_;
};

struct ClientSideWeightedRoundRobinCreator : public Logger::Loggable<Logger::Id::upstream> {
  Upstream::LoadBalancerPtr operator()(Upstream::LoadBalancerParams params,
                                       OptRef<const Upstream::LoadBalancerConfig> lb_config,
                                       const Upstream::ClusterInfo& cluster_info,
                                       const Upstream::PrioritySet& priority_set,
                                       Runtime::Loader& runtime, Random::RandomGenerator& random,
                                       TimeSource& time_source);
};

class Factory : public Common::FactoryBase<ClientSideWeightedRoundRobinLbProto,
                                           ClientSideWeightedRoundRobinCreator> {
public:
  Factory() : FactoryBase("envoy.load_balancing_policies.client_side_weighted_round_robin") {}

  Upstream::LoadBalancerConfigPtr loadConfig(Upstream::LoadBalancerFactoryContext& context,
                                             const Protobuf::Message& config,
                                             ProtobufMessage::ValidationVisitor&) override {
    const auto& lb_config = dynamic_cast<const ClientSideWeightedRoundRobinLbProto&>(config);
    return Upstream::LoadBalancerConfigPtr{
        new ClientSideWeightedRoundRobinLbConfig(lb_config, context.mainThreadDispatcher())};
  }
};

DECLARE_FACTORY(Factory);

} // namespace ClientSideWeightedRoundRobin
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
