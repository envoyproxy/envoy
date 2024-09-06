#pragma once

#include <memory>

#include "source/common/common/logger.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicClientSession;

// TODO(danzh) deprecate this class once QUICHE has its own more detailed network observer.
class QuicNetworkConnectivityObserver : protected Logger::Loggable<Logger::Id::connection> {
public:
  // session must outlive this object.
  explicit QuicNetworkConnectivityObserver(EnvoyQuicClientSession& session);
  QuicNetworkConnectivityObserver(const QuicNetworkConnectivityObserver&) = delete;
  QuicNetworkConnectivityObserver& operator=(const QuicNetworkConnectivityObserver&) = delete;

  // Called when the device switches to a different network.
  void onNetworkChanged() {
    // TODO(danzh) close the connection if it's idle, otherwise mark it as go away.
    (void)session_;
  }

private:
  EnvoyQuicClientSession& session_;
};

using QuicNetworkConnectivityObserverPtr = std::unique_ptr<QuicNetworkConnectivityObserver>;

} // namespace Quic
} // namespace Envoy
