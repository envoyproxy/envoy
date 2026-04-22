#pragma once
#include "source/extensions/load_balancing_policies/round_robin/config.h"
#include "source/extensions/load_balancing_policies/round_robin/round_robin_lb.h"

#include "test/integration/async_round_robin.pb.h"

namespace Envoy {
namespace Upstream {

// The async round robin load balancer is functionally the RoundRobin load
// balancer with a synthetic pause between selecting the host and returning the
// host, to exercise the onAsyncHostSelection host selection path.

// Creates the AsyncRoundRobin LB.
struct AsyncRoundRobinCreator : public Logger::Loggable<Logger::Id::upstream> {
  Upstream::LoadBalancerPtr operator()(Upstream::LoadBalancerParams params,
                                       OptRef<const Upstream::LoadBalancerConfig> lb_config,
                                       const Upstream::ClusterInfo& cluster_info,
                                       const Upstream::PrioritySet& priority_set,
                                       Runtime::Loader& runtime, Random::RandomGenerator& random,
                                       TimeSource& time_source);
};

class TypedAsyncRoundRobinLbConfig : public Upstream::LoadBalancerConfig {
public:
  TypedAsyncRoundRobinLbConfig(const test::integration::lb::AsyncRoundRobin& lb_config)
      : lb_config_(lb_config) {}
  const test::integration::lb::AsyncRoundRobin lb_config_;
};

// Factory code to create the AsyncRoundRobin LB.
class AsyncRoundRobinFactory : public Extensions::LoadBalancingPolicies::Common::FactoryBase<
                                   test::integration::lb::AsyncRoundRobin, AsyncRoundRobinCreator> {
public:
  AsyncRoundRobinFactory() : FactoryBase("envoy.load_balancing_policies.async_round_robin") {}

  absl::StatusOr<LoadBalancerConfigPtr> loadConfig(Server::Configuration::ServerFactoryContext&,
                                                   const Protobuf::Message& config) override {
    const auto& typed_config = dynamic_cast<const test::integration::lb::AsyncRoundRobin&>(config);
    return Upstream::LoadBalancerConfigPtr{new TypedAsyncRoundRobinLbConfig(typed_config)};
  }
};

DECLARE_FACTORY(AsyncRoundRobinFactory);

} // namespace Upstream
} // namespace Envoy
