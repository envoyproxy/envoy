#pragma once

#include <memory>

namespace Envoy {
namespace Http {

// Store quic helpers which can be shared between connections and must live beyond the lifetime of
// individual connections. When used in HTTP/3 upstream, it is created by the cluster and shared
// across its HTTP/3 connection pools. This an opaque placeholder is needed so that an
// implementation can be passed around while the QUICHE members which are behind ENVOY_ENABLE_QUIC
// preprocessor in the actual implementation can be hidden from the Envoy interfaces.
//
// Every upstream QUIC connection keeps raw pointers into this object (its clock, alarm factory and
// config), and a connection pool can outlive the cluster that created it: a removed cluster only
// drains its pools, and pools with active streams live on until those streams end. The cluster and
// its pools therefore share ownership, so the object outlives every connection that uses it.
struct PersistentQuicInfo {
  virtual ~PersistentQuicInfo() = default;
};

using PersistentQuicInfoPtr = std::shared_ptr<PersistentQuicInfo>;

} // namespace Http
} // namespace Envoy
