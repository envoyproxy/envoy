#pragma once

#include <memory>

#include "source/common/common/logger.h"
#include "source/common/quic/quic_network_connectivity_observer.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicClientSession;

class QuicNetworkConnectivityObserverImpl : public QuicNetworkConnectivityObserver,
                                            protected Logger::Loggable<Logger::Id::connection> {
public:
  // session must outlive this object.
  explicit QuicNetworkConnectivityObserverImpl(EnvoyQuicClientSession& session);
  QuicNetworkConnectivityObserverImpl(const QuicNetworkConnectivityObserverImpl&) = delete;
  QuicNetworkConnectivityObserverImpl&
  operator=(const QuicNetworkConnectivityObserverImpl&) = delete;

  // QuicNetworkConnectivityObserver
  void onNetworkMadeDefault(NetworkHandle network) override;
  void onNetworkConnected(NetworkHandle network) override;
  void onNetworkDisconnected(NetworkHandle network) override;

private:
  EnvoyQuicClientSession& session_;
};

} // namespace Quic
} // namespace Envoy
