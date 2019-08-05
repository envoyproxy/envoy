#pragma once

#include "envoy/network/listener.h"

#include "server/connection_handler_impl.h"

#include "extensions/quic_listeners/quiche/envoy_quic_dispatcher.h"

namespace Envoy {
namespace Quic {
class ActiveQuicListener : public Network::UdpListenerCallbacks,
                           public Server::ConnectionHandlerImpl::ActiveListenerBase,
                           // Inherits below two interfaces just to have common
                           // interfaces. Not expected to support listener
                           // filter.
                           public Network::UdpListenerFilterManager,
                           public Network::UdpReadFilterCallbacks {
public:
  ActiveQuicListener(Server::ConnectionHandlerImpl& parent,
                     Network::ListenerConfig& listener_config);

  ActiveQuicListener(Server::ConnectionHandlerImpl& parent, Network::ListenerPtr&& listener,
                     Network::ListenerConfig& listener_config);
  // TODO(#7465): Make this a callback.
  void onListenerShutdown();

  // Network::UdpListenerCallbacks
  void onData(Network::UdpRecvData& data) override;
  void onWriteReady(const Network::Socket& socket) override;
  void onReceiveError(const Network::UdpListenerCallbacks::ErrorCode& /*error_code*/,
                      Api::IoError::IoErrorCode /*err*/) override {
    // No-op. Quic can't do anything upon listener error.
  }

  // Network::UdpListenerFilterManager
  void addReadFilter(Network::UdpListenerReadFilterPtr&& /*filter*/) override {
    // QUIC doesn't support listener filter.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  // Network::UdpReadFilterCallbacks
  Network::UdpListener& udpListener() override { NOT_REACHED_GCOVR_EXCL_LINE; }

private:
  friend class ActiveQuicListenerPeer;

  uint8_t random_seed_[16];
  std::unique_ptr<quic::QuicCryptoServerConfig> crypto_config_;
  quic::QuicVersionManager version_manager_;
  std::unique_ptr<EnvoyQuicDispatcher> quic_dispatcher_;
};

using ActiveQuicListenerPtr = std::unique_ptr<ActiveQuicListener>;

} // namespace Quic
} // namespace Envoy
