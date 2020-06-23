#pragma once

#include "envoy/config/listener/v3/quic_config.pb.h"
#include "envoy/network/connection_handler.h"
#include "envoy/network/listener.h"
#include "envoy/runtime/runtime.h"

#include "common/network/socket_option_impl.h"
#include "common/protobuf/utility.h"
#include "common/runtime/runtime_protos.h"

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
  // TODO(bencebeky): Tune this value.
  static const size_t kNumSessionsToCreatePerLoop = 16;

  ActiveQuicListener(Event::Dispatcher& dispatcher, Network::ConnectionHandler& parent,
                     Network::ListenerConfig& listener_config, const quic::QuicConfig& quic_config,
                     Network::Socket::OptionsSharedPtr options,
                     const envoy::config::core::v3::RuntimeFeatureFlag& enabled);

  ActiveQuicListener(Event::Dispatcher& dispatcher, Network::ConnectionHandler& parent,
                     Network::SocketSharedPtr listen_socket,
                     Network::ListenerConfig& listener_config, const quic::QuicConfig& quic_config,
                     Network::Socket::OptionsSharedPtr options,
                     const envoy::config::core::v3::RuntimeFeatureFlag& enabled);

  ActiveQuicListener(Event::Dispatcher& dispatcher, Network::ConnectionHandler& parent,
                     Network::SocketSharedPtr listen_socket,
                     Network::ListenerConfig& listener_config, const quic::QuicConfig& quic_config,
                     Network::Socket::OptionsSharedPtr options,
                     std::unique_ptr<quic::ProofSource> proof_source,
                     const envoy::config::core::v3::RuntimeFeatureFlag& enabled);

  ~ActiveQuicListener() override;

  void onListenerShutdown();

  // Network::UdpListenerCallbacks
  void onData(Network::UdpRecvData& data) override;
  void onReadReady() override;
  void onWriteReady(const Network::Socket& socket) override;
  void onReceiveError(Api::IoError::IoErrorCode /*error_code*/) override {
    // No-op. Quic can't do anything upon listener error.
  }

  // ActiveListenerImplBase
  Network::Listener* listener() override { return udp_listener_.get(); }
  void pauseListening() override;
  void resumeListening() override;
  void shutdownListener() override;

private:
  friend class ActiveQuicListenerPeer;

  Network::UdpListenerPtr udp_listener_;
  uint8_t random_seed_[16];
  std::unique_ptr<quic::QuicCryptoServerConfig> crypto_config_;
  Event::Dispatcher& dispatcher_;
  quic::QuicVersionManager version_manager_;
  std::unique_ptr<EnvoyQuicDispatcher> quic_dispatcher_;
  Network::Socket& listen_socket_;
  Runtime::FeatureFlag enabled_;
};

using ActiveQuicListenerPtr = std::unique_ptr<ActiveQuicListener>;

// A factory to create ActiveQuicListener based on given config.
class ActiveQuicListenerFactory : public Network::ActiveUdpListenerFactory,
                                  Logger::Loggable<Logger::Id::quic> {
public:
  ActiveQuicListenerFactory(const envoy::config::listener::v3::QuicProtocolOptions& config,
                            uint32_t concurrency);

  // Network::ActiveUdpListenerFactory.
  Network::ConnectionHandler::ActiveListenerPtr
  createActiveUdpListener(Network::ConnectionHandler& parent, Event::Dispatcher& disptacher,
                          Network::ListenerConfig& config) override;
  bool isTransportConnectionless() const override { return false; }

private:
  friend class ActiveQuicListenerFactoryPeer;

  quic::QuicConfig quic_config_;
  const uint32_t concurrency_;
  absl::once_flag install_bpf_once_;
  envoy::config::core::v3::RuntimeFeatureFlag enabled_;
};

} // namespace Quic
} // namespace Envoy
