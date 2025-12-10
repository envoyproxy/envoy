#pragma once

#include <memory>

namespace Envoy {

using NetworkHandle = int64_t;

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
