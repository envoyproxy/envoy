#pragma once

#include <memory>

namespace Envoy {
namespace Quic {

class EnvoyQuicClientSession;

// TODO(danzh) deprecate this class once QUICHE has its own more detailed network observer.
class QuicNetworkConnectivityObserver {
public:
  // session must outlive this object.
  explicit QuicNetworkConnectivityObserver(EnvoyQuicClientSession& session);
  QuicNetworkConnectivityObserver(const QuicNetworkConnectivityObserver&) = delete;
  QuicNetworkConnectivityObserver& operator=(const QuicNetworkConnectivityObserver&) = delete;

  void onNetworkChanged();

private:
  EnvoyQuicClientSession& session_;
};

using QuicNetworkConnectivityObserverPtr = std::unique_ptr<QuicNetworkConnectivityObserver>;

} // namespace Quic
} // namespace Envoy
