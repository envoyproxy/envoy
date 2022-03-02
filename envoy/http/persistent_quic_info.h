#pragma once

#include <memory>

namespace Envoy {
namespace Http {

// Store quic helpers which can be shared between connections and must live
// beyond the lifetime of individual connections.
struct PersistentQuicInfo {
  virtual ~PersistentQuicInfo() = default;
};

using PersistentQuicInfoPtr = std::unique_ptr<PersistentQuicInfo>;

} // namespace Http
} // namespace Envoy
