#include "common/network/connection_balancer_impl.h"

namespace Envoy {
namespace Network {

void ExactConnectionBalancerImpl::registerHandler(BalancedConnectionHandler& handler) {
  absl::MutexLock lock(&lock_);
  handlers_.push_back(&handler);
}

void ExactConnectionBalancerImpl::unregisterHandler(BalancedConnectionHandler& handler) {
  absl::MutexLock lock(&lock_);
  handlers_.erase(std::find(handlers_.begin(), handlers_.end(), &handler));
}

ConnectionBalancer::BalanceConnectionResult
ExactConnectionBalancerImpl::balanceConnection(ConnectionSocketPtr&& socket,
                                               BalancedConnectionHandler& current_handler) {
  BalancedConnectionHandler* min_connection_handler = nullptr;
  {
    absl::MutexLock lock(&lock_);
    for (const auto handler : handlers_) {
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
