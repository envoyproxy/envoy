#pragma once

#include "envoy/api/v2/listener/quic_config.pb.h"
#include "envoy/network/listener.h"

#include "server/connection_handler_impl.h"

#include "extensions/quic_listeners/quiche/envoy_quic_dispatcher.h"

namespace Envoy {
namespace Quic {

// QUIC specific UdpListenerCallbacks implemention which delegates incoming
// packets, write signal and listener error to QuicDispatcher.
class ActiveQuicListener : public Network::UdpListenerCallbacks,
                           public Server::ConnectionHandlerImpl::ActiveListenerImplBase,
                           // Inherits below two interfaces just to have common
                           // interfaces. Not expected to support listener
                           // filter.
                           public Network::UdpListenerFilterManager,
                           public Network::UdpReadFilterCallbacks {
public:
  ActiveQuicListener(Event::Dispatcher& dispatcher, Network::ConnectionHandler& parent,
                     spdlog::logger& logger, Network::ListenerConfig& listener_config,
                     const quic::QuicConfig& quic_config);

  ActiveQuicListener(Event::Dispatcher& dispatcher, Network::ConnectionHandler& parent,
                     Network::ListenerPtr&& listener, spdlog::logger& logger,
                     Network::ListenerConfig& listener_config, const quic::QuicConfig& quic_config);
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
  spdlog::logger& logger_;
  Event::Dispatcher& dispatcher_;
  quic::QuicVersionManager version_manager_;
  std::unique_ptr<EnvoyQuicDispatcher> quic_dispatcher_;
};

using ActiveQuicListenerPtr = std::unique_ptr<ActiveQuicListener>;

// A factory to create ActiveQuicListener based on given config.
class ActiveQuicListenerFactory : public Network::ActiveUdpListenerFactory {
public:
  ActiveQuicListenerFactory(const envoy::api::v2::listener::QuicConfigProto& config) {
    int32_t idle_network_timeout_ms =
        config.idle_network_timeout_ms() == 0 ? 300000 : config.idle_network_timeout_ms();
    quic_config_.SetIdleNetworkTimeout(
        quic::QuicTime::Delta::FromMilliseconds(idle_network_timeout_ms),
        quic::QuicTime::Delta::FromMilliseconds(idle_network_timeout_ms));
    int32_t max_time_before_crypto_handshake_ms =
        config.max_time_before_crypto_handshake_ms() == 0
            ? 20000
            : config.max_time_before_crypto_handshake_ms();
    quic_config_.set_max_time_before_crypto_handshake(
        quic::QuicTime::Delta::FromMilliseconds(max_time_before_crypto_handshake_ms));
  }

  Network::ConnectionHandler::ActiveListenerPtr
  createActiveUdpListener(Network::ConnectionHandler& parent, Event::Dispatcher& disptacher,
                          spdlog::logger& logger, Network::ListenerConfig& config) const override {
    return std::make_unique<ActiveQuicListener>(disptacher, parent, logger, config, quic_config_);
  }

private:
  quic::QuicConfig quic_config_;
};

} // namespace Quic
} // namespace Envoy
