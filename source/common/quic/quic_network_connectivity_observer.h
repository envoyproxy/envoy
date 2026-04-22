#pragma once

#include <memory>

#ifdef ENVOY_ENABLE_QUIC
#include "quiche/quic/core/quic_path_validator.h"

using NetworkHandle = quic::QuicNetworkHandle;
#else
using NetworkHandle = int64_t;
#endif

namespace Envoy {

namespace Quic {

// An interface to get network change notifications from the underlying platform.
class QuicNetworkConnectivityObserver {
public:
  virtual ~QuicNetworkConnectivityObserver() = default;

  // Called when the device switches to a different network.
  virtual void onNetworkMadeDefault(NetworkHandle network) PURE;

  // Called when a new network is connected.
  virtual void onNetworkConnected(NetworkHandle network) PURE;

  // Called when the given network gets disconnected.
  virtual void onNetworkDisconnected(NetworkHandle network) PURE;
};

using QuicNetworkConnectivityObserverPtr = std::unique_ptr<QuicNetworkConnectivityObserver>;

} // namespace Quic
} // namespace Envoy
