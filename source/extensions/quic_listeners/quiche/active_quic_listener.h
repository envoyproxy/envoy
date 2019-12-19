#pragma once

#include <linux/filter.h>

#include <vector>

#include "envoy/api/v2/listener/quic_config.pb.h"
#include "envoy/network/connection_handler.h"
#include "envoy/network/listener.h"

#include "common/network/socket_option_impl.h"
#include "common/protobuf/utility.h"

#include "server/connection_handler_impl.h"

#include "extensions/quic_listeners/quiche/envoy_quic_dispatcher.h"

namespace Envoy {
namespace Quic {

// QUIC specific UdpListenerCallbacks implementation which delegates incoming
// packets, write signals and listener errors to QuicDispatcher.
class ActiveQuicListener : public Network::UdpListenerCallbacks,
                           public Server::ConnectionHandlerImpl::ActiveListenerImplBase,
                           Logger::Loggable<Logger::Id::quic> {
public:
  ActiveQuicListener(Event::Dispatcher& dispatcher, Network::ConnectionHandler& parent,
                     Network::ListenerConfig& listener_config, const quic::QuicConfig& quic_config,
                     Network::Socket::OptionsSharedPtr options);

  ActiveQuicListener(Event::Dispatcher& dispatcher, Network::ConnectionHandler& parent,
                     Network::SocketSharedPtr listen_socket,
                     Network::ListenerConfig& listener_config, const quic::QuicConfig& quic_config,
                     Network::Socket::OptionsSharedPtr options);

  ~ActiveQuicListener() override;

  // TODO(#7465): Make this a callback.
  void onListenerShutdown();

  // Network::UdpListenerCallbacks
  void onData(Network::UdpRecvData& data) override;
  void onWriteReady(const Network::Socket& socket) override;
  void onReceiveError(Api::IoError::IoErrorCode /*error_code*/) override {
    // No-op. Quic can't do anything upon listener error.
  }

  // ActiveListenerImplBase
  Network::Listener* listener() override { return udp_listener_.get(); }
  void destroy() override { udp_listener_.reset(); }

private:
  friend class ActiveQuicListenerPeer;

  Network::UdpListenerPtr udp_listener_;
  uint8_t random_seed_[16];
  std::unique_ptr<quic::QuicCryptoServerConfig> crypto_config_;
  Event::Dispatcher& dispatcher_;
  quic::QuicVersionManager version_manager_;
  std::unique_ptr<EnvoyQuicDispatcher> quic_dispatcher_;
  Network::Socket& listen_socket_;
};

using ActiveQuicListenerPtr = std::unique_ptr<ActiveQuicListener>;

// A factory to create ActiveQuicListener based on given config.
class ActiveQuicListenerFactory : public Network::ActiveUdpListenerFactory {
public:
  ActiveQuicListenerFactory(const envoy::api::v2::listener::QuicProtocolOptions& config,
                            uint32_t concurrency)
      : concurrency_(concurrency) {
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
                          Network::ListenerConfig& config) override {
    std::unique_ptr<Network::Socket::Options> options =
        std::make_unique<Network::Socket::Options>();
#ifdef SO_ATTACH_REUSEPORT_CBPF
    std::vector<sock_filter> filter = {
        {0x80, 0, 0, 0000000000},   {0x35, 0, 9, 0x00000009},
        {0x30, 0, 0, 0000000000},   {0x54, 0, 0, 0x00000080},
        {0x15, 0, 2, 0000000000},   {0x20, 0, 0, 0x00000001},
        {0x05, 0, 0, 0x00000005},   {0x80, 0, 0, 0000000000},
        {0x35, 0, 2, 0x0000000e},   {0x20, 0, 0, 0x00000006},
        {0x05, 0, 0, 0x00000001},   {0x20, 0, 0, static_cast<uint32_t>(SKF_AD_OFF + SKF_AD_RXHASH)},
        {0x94, 0, 0, concurrency_}, {0x16, 0, 0, 0000000000},
    };
    sock_fprog prog;
    absl::call_once(install_bpf_once_, [&]() {
      if (concurrency_ > 1) {
        prog.len = filter.size();
        prog.filter = filter.data();
        options->push_back(std::make_shared<Network::SocketOptionImpl>(
            envoy::api::v2::core::SocketOption::STATE_BOUND, ENVOY_ATTACH_REUSEPORT_CBPF,
            absl::string_view(reinterpret_cast<char*>(&prog), sizeof(prog))));
      }
    });
#endif
    return std::make_unique<ActiveQuicListener>(disptacher, parent, config, quic_config_,
                                                std::move(options));
  }
  bool isTransportConnectionless() const override { return false; }

private:
  friend class ActiveQuicListenerFactoryPeer;

  quic::QuicConfig quic_config_;
  const uint32_t concurrency_;
  absl::once_flag install_bpf_once_;
};

} // namespace Quic
} // namespace Envoy
