#include "common/network/connection_balancer_impl.h"

namespace Envoy {
namespace Network {

void ExactConnectionBalancerImpl::registerHandler(BalancedConnectionHandler& handler) {
  absl::MutexLock lock(&lock_);
  handlers_.push_back(&handler);
}

void ExactConnectionBalancerImpl::unregisterHandler(BalancedConnectionHandler& handler) {
  absl::MutexLock lock(&lock_);
  // This could be made more efficient in various ways, but the number of listeners is generally
  // small and this is a rare operation so we can start with this and optimize later if this
  // becomes a perf bottleneck.
  handlers_.erase(std::find(handlers_.begin(), handlers_.end(), &handler));
}

ConnectionBalancer::BalanceConnectionResult
ExactConnectionBalancerImpl::balanceConnection(ConnectionSocketPtr&& socket,
                                               BalancedConnectionHandler& current_handler) {
  BalancedConnectionHandler* min_connection_handler = nullptr;
  {
    absl::MutexLock lock(&lock_);
    for (BalancedConnectionHandler* handler : handlers_) {
      if (min_connection_handler == nullptr ||
          handler->numConnections() < min_connection_handler->numConnections()) {
        min_connection_handler = handler;
      }
    }

    min_connection_handler->incNumConnections();
  }

  if (min_connection_handler != &current_handler) {
    min_connection_handler->post(std::move(socket));
    return ConnectionBalancer::BalanceConnectionResult::Rebalanced;
  }
  return ConnectionBalancer::BalanceConnectionResult::Continue;
}

} // namespace Network
} // namespace Envoy
