#include "contrib/network/connection_balance/random/source/connection_balancer_impl.h"

#include <bits/types/FILE.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <memory>

namespace Envoy {
namespace Extensions {
namespace Random {

void RandomConnectionBalancerImpl::registerHandler(
    Envoy::Network::BalancedConnectionHandler& handler) {
  absl::MutexLock lock(&lock_);
  handlers_.push_back(&handler);
}

void RandomConnectionBalancerImpl::unregisterHandler(
    Envoy::Network::BalancedConnectionHandler& handler) {
  absl::MutexLock lock(&lock_);
  handlers_.erase(std::find(handlers_.begin(), handlers_.end(), &handler));
}

Envoy::Network::BalancedConnectionHandler&
RandomConnectionBalancerImpl::pickTargetHandler(Envoy::Network::BalancedConnectionHandler&) {
  absl::MutexLock lock(&lock_);
  int index = random_.random() % handlers_.size();
  return *handlers_[index];
}

} // namespace Random
} // namespace Extensions
} // namespace Envoy
