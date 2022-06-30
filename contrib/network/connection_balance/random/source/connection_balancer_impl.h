#pragma once

#include <memory>

#include "envoy/common/random_generator.h"
#include "envoy/event/dispatcher.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/network/connection_balancer_impl.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Random {
/**
 * Implementation of connection balancer that does random balancing. It is a replacement for
 * Exact connection balancer on Windows to ensure all work threads work, faster than Exact
 * connection balancer.
 */
class RandomConnectionBalancerImpl : public Envoy::Network::ConnectionBalancer {
public:
  RandomConnectionBalancerImpl(Envoy::Random::RandomGenerator& random) : random_(random) {}
  void registerHandler(Envoy::Network::BalancedConnectionHandler& handler) override;

  void unregisterHandler(Envoy::Network::BalancedConnectionHandler& handler) override;

  Envoy::Network::BalancedConnectionHandler&
  pickTargetHandler(Envoy::Network::BalancedConnectionHandler& current_handler) override;

private:
  absl::Mutex lock_;
  std::vector<Network::BalancedConnectionHandler*> handlers_ ABSL_GUARDED_BY(lock_);
  Envoy::Random::RandomGenerator& random_;
};

} // namespace Random
} // namespace Extensions
} // namespace Envoy
