#pragma once
#include "source/extensions/load_balancing_policies/round_robin/config.h"
#include "source/extensions/load_balancing_policies/round_robin/round_robin_lb.h"

#include "test/integration/async_round_robin.pb.h"

namespace Envoy {
namespace Upstream {

struct AsyncRoundRobinCreator : public Logger::Loggable<Logger::Id::upstream> {
  Upstream::LoadBalancerPtr operator()(Upstream::LoadBalancerParams params,
                                       OptRef<const Upstream::LoadBalancerConfig> lb_config,
                                       const Upstream::ClusterInfo& cluster_info,
                                       const Upstream::PrioritySet& priority_set,
                                       Runtime::Loader& runtime, Random::RandomGenerator& random,
                                       TimeSource& time_source);
};

class AsyncRoundRobinFactory : public Extensions::LoadBalancingPolices::Common::FactoryBase<
                                   test::integration::lb::AsyncRoundRobin, AsyncRoundRobinCreator> {
public:
  AsyncRoundRobinFactory() : FactoryBase("envoy.load_balancing_policies.async_round_robin") {}

  absl::StatusOr<LoadBalancerConfigPtr> loadConfig(Server::Configuration::ServerFactoryContext&,
                                                   const Protobuf::Message&) override {
    return Upstream::LoadBalancerConfigPtr{new Upstream::LoadBalancerConfig()};
  }
};

DECLARE_FACTORY(AsyncRoundRobinFactory);

} // namespace Upstream
} // namespace Envoy
