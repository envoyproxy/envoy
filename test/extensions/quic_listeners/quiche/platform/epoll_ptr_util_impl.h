#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <memory>

namespace epoll_server {

template <typename T, typename... Args> std::unique_ptr<T> EpollMakeUniqueImpl(Args&&... args) {
  return std::make_unique<T>(std::forward<Args>(args)...);
}

} // namespace epoll_server
