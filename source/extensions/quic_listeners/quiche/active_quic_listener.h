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
class ActiveQuicListener : public Envoy::Server::ActiveUdpListenerBase,
                           Logger::Loggable<Logger::Id::quic> {
public:
  // TODO(bencebeky): Tune this value.
  static const size_t kNumSessionsToCreatePerLoop = 16;

  ActiveQuicListener(uint32_t worker_index, uint32_t concurrency, Event::Dispatcher& dispatcher,
                     Network::ConnectionHandler& parent, Network::ListenerConfig& listener_config,
                     const quic::QuicConfig& quic_config, Network::Socket::OptionsSharedPtr options,
                     bool kernel_worker_routing,
                     const envoy::config::core::v3::RuntimeFeatureFlag& enabled);

  ActiveQuicListener(uint32_t worker_index, uint32_t concurrency, Event::Dispatcher& dispatcher,
                     Network::ConnectionHandler& parent, Network::SocketSharedPtr listen_socket,
                     Network::ListenerConfig& listener_config, const quic::QuicConfig& quic_config,
                     Network::Socket::OptionsSharedPtr options, bool kernel_worker_routing,
                     const envoy::config::core::v3::RuntimeFeatureFlag& enabled);

  ~ActiveQuicListener() override;

  void onListenerShutdown();
  uint64_t eventLoopsWithBufferedChlosForTest() const {
    return event_loops_with_buffered_chlo_for_test_;
  }

  // Network::UdpListenerCallbacks
  void onReadReady() override;
  void onWriteReady(const Network::Socket& socket) override;
  void onReceiveError(Api::IoError::IoErrorCode /*error_code*/) override {
    // No-op. Quic can't do anything upon listener error.
  }
  Network::UdpPacketWriter& udpPacketWriter() override { return *udp_packet_writer_; }
  void onDataWorker(Network::UdpRecvData&& data) override;
  uint32_t destination(const Network::UdpRecvData& data) const override;

  // ActiveListenerImplBase
  void pauseListening() override;
  void resumeListening() override;
  void shutdownListener() override;

private:
  friend class ActiveQuicListenerPeer;

  uint8_t random_seed_[16];
  std::unique_ptr<quic::QuicCryptoServerConfig> crypto_config_;
  Event::Dispatcher& dispatcher_;
  quic::QuicVersionManager version_manager_;
  std::unique_ptr<EnvoyQuicDispatcher> quic_dispatcher_;
  const bool kernel_worker_routing_;
  Runtime::FeatureFlag enabled_;
  Network::UdpPacketWriter* udp_packet_writer_;

  // The number of runs of the event loop in which at least one CHLO was buffered.
  // TODO(ggreenway): Consider making this a published stat, or some variation of this information.
  uint64_t event_loops_with_buffered_chlo_for_test_{0};
};

using ActiveQuicListenerPtr = std::unique_ptr<ActiveQuicListener>;

// A factory to create ActiveQuicListener based on given config.
class ActiveQuicListenerFactory : public Network::ActiveUdpListenerFactory,
                                  Logger::Loggable<Logger::Id::quic> {
public:
  ActiveQuicListenerFactory(const envoy::config::listener::v3::QuicProtocolOptions& config,
                            uint32_t concurrency);

  // Network::ActiveUdpListenerFactory.
  Network::ConnectionHandler::ActiveUdpListenerPtr
  createActiveUdpListener(uint32_t worker_index, Network::ConnectionHandler& parent,
                          Event::Dispatcher& disptacher, Network::ListenerConfig& config) override;
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
