#include "contrib/network/connection_balance/random/source/connection_balancer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace ConnectioBalacnce {
namespace Random {

void RandomConnectionBalancerImpl::registerHandler(
    Envoy::Network::BalancedConnectionHandler& handler) {
  handlers_.push_back(&handler);
}

void RandomConnectionBalancerImpl::unregisterHandler(
    Envoy::Network::BalancedConnectionHandler& handler) {
  handlers_.erase(std::find(handlers_.begin(), handlers_.end(), &handler));
}

Envoy::Network::BalancedConnectionHandler&
RandomConnectionBalancerImpl::pickTargetHandler(Envoy::Network::BalancedConnectionHandler&) {
  int index = random_.random() % handlers_.size();
  return *handlers_[index];
}

} // namespace Random
} // namespace ConnectioBalacnce
} // namespace Network
} // namespace Extensions
} // namespace Envoy
