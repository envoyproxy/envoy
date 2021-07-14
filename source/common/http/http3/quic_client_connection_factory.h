#pragma once

namespace Envoy {
namespace Http {

// Store quic helpers which can be shared between connections and must live
// beyond the lifetime of individual connections.
struct PersistentQuicInfo {
  virtual ~PersistentQuicInfo() = default;
};

} // namespace Http
} // namespace Envoy
