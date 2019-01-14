#pragma once

#include <memory>
#include <utility>

// NOLINT(namespace-envoy)

namespace http2 {

template <typename T, typename... Args> std::unique_ptr<T> Http2MakeUniqueImpl(Args&&... args) {
  return std::make_unique<T>(std::forward<Args>(args)...);
}

} // namespace http2
