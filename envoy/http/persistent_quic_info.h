#pragma once

#include <memory>

namespace Envoy {
namespace Http {

// Store quic helpers which can be shared between connections and must live beyond the lifetime of
// individual connections. When used in HTTP/3 upstream, it should be owned by cluster and shared
// across its HTTP/3 connection pools. This an opaque placeholder is needed so that an
// implementation can be passed around while the QUICHE members which are behind ENVOY_ENABLE_QUIC
// preprocessor in the actual implementation can be hidden from the Envoy intefaces.
struct PersistentQuicInfo {
  virtual ~PersistentQuicInfo() = default;
};

using PersistentQuicInfoPtr = std::unique_ptr<PersistentQuicInfo>;

} // namespace Http
} // namespace Envoy
