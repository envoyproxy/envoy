#pragma once

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
 * Exact connection balancer on Windows to ensure all worker threads work. It is faster than
 * the Exact connection balancer.
 */
class RandomConnectionBalancerImpl : public Envoy::Network::ConnectionBalancer {
public:
  RandomConnectionBalancerImpl(Envoy::Random::RandomGenerator& random) : random_(random) {}
  void registerHandler(Envoy::Network::BalancedConnectionHandler& handler) override;

  void unregisterHandler(Envoy::Network::BalancedConnectionHandler& handler) override;

  Envoy::Network::BalancedConnectionHandler&
  pickTargetHandler(Envoy::Network::BalancedConnectionHandler& current_handler) override;

private:
  std::vector<Network::BalancedConnectionHandler*> handlers_;
  Envoy::Random::RandomGenerator& random_;
};

} // namespace Random
} // namespace ConnectioBalacnce
} // namespace Network
} // namespace Extensions
} // namespace Envoy
