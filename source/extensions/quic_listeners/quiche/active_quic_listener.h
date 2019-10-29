#pragma once

#include "envoy/api/v2/listener/quic_config.pb.h"
#include "envoy/network/connection_handler.h"
#include "envoy/network/listener.h"

#include "common/protobuf/utility.h"

#include "server/connection_handler_impl.h"

#include "extensions/quic_listeners/quiche/envoy_quic_dispatcher.h"

namespace Envoy {
namespace Quic {

// QUIC specific UdpListenerCallbacks implementation which delegates incoming
// packets, write signals and listener errors to QuicDispatcher.
class ActiveQuicListener : public Network::UdpListenerCallbacks,
                           public Server::ConnectionHandlerImpl::ActiveListenerImplBase,
                           // Inherits below two interfaces just to have common
                           // interfaces. Not expected to support listener
                           // filter.
                           // TODO(danzh): clean up meaningless inheritance.
                           public Network::UdpListenerFilterManager,
                           public Network::UdpReadFilterCallbacks,
                           Logger::Loggable<Logger::Id::quic> {
public:
  ActiveQuicListener(Event::Dispatcher& dispatcher, Network::ConnectionHandler& parent,
                     Network::ListenerConfig& listener_config, const quic::QuicConfig& quic_config);

  ActiveQuicListener(Event::Dispatcher& dispatcher, Network::ConnectionHandler& parent,
                     Network::UdpListenerPtr&& listener, Network::ListenerConfig& listener_config,
                     const quic::QuicConfig& quic_config);

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

  ActiveQuicListener(Event::Dispatcher& dispatcher, Network::ConnectionHandler& parent,
                     std::unique_ptr<quic::QuicPacketWriter> writer,
                     Network::UdpListenerPtr&& listener, Network::ListenerConfig& listener_config,
                     const quic::QuicConfig& quic_config);

  uint8_t random_seed_[16];
  std::unique_ptr<quic::QuicCryptoServerConfig> crypto_config_;
  Event::Dispatcher& dispatcher_;
  quic::QuicVersionManager version_manager_;
  std::unique_ptr<EnvoyQuicDispatcher> quic_dispatcher_;
};

using ActiveQuicListenerPtr = std::unique_ptr<ActiveQuicListener>;

// A factory to create ActiveQuicListener based on given config.
class ActiveQuicListenerFactory : public Network::ActiveUdpListenerFactory {
public:
  ActiveQuicListenerFactory(const envoy::api::v2::listener::QuicProtocolOptions& config) {
    uint64_t idle_network_timeout_ms =
        config.has_idle_timeout() ? DurationUtil::durationToMilliseconds(config.idle_timeout())
                                  : 300000;
    quic_config_.SetIdleNetworkTimeout(
        quic::QuicTime::Delta::FromMilliseconds(idle_network_timeout_ms),
        quic::QuicTime::Delta::FromMilliseconds(idle_network_timeout_ms));
    int32_t max_time_before_crypto_handshake_ms =
        config.has_crypto_handshake_timeout()
            ? DurationUtil::durationToMilliseconds(config.crypto_handshake_timeout())
            : 20000;
    quic_config_.set_max_time_before_crypto_handshake(
        quic::QuicTime::Delta::FromMilliseconds(max_time_before_crypto_handshake_ms));
    int32_t max_streams = PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_concurrent_streams, 100);
    quic_config_.SetMaxIncomingBidirectionalStreamsToSend(max_streams);
    quic_config_.SetMaxIncomingUnidirectionalStreamsToSend(max_streams);
  }

  // Network::ActiveUdpListenerFactory.
  Network::ConnectionHandler::ActiveListenerPtr
  createActiveUdpListener(Network::ConnectionHandler& parent, Event::Dispatcher& disptacher,
                          Network::ListenerConfig& config) const override {
    return std::make_unique<ActiveQuicListener>(disptacher, parent, config, quic_config_);
  }
  bool isTransportConnectionless() const override { return false; }

private:
  friend class ActiveQuicListenerFactoryPeer;

  quic::QuicConfig quic_config_;
};

} // namespace Quic
} // namespace Envoy
