#pragma once

#include <memory>

using NetworkHandle = int64_t;

namespace Envoy {
namespace Quic {

class QuicNetworkConnectivityObserver {
public:
  virtual ~QuicNetworkConnectivityObserver() = default;

  // Called when the device switches to a different network.
  virtual void onNetworkChanged(NetworkHandle network) PURE;
};

using QuicNetworkConnectivityObserverPtr = std::unique_ptr<QuicNetworkConnectivityObserver>;

} // namespace Quic
} // namespace Envoy
