#pragma once

#include "envoy/config/listener/v3/quic_config.pb.h"
#include "envoy/network/connection_handler.h"
#include "envoy/network/listener.h"
#include "envoy/network/socket.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/process_context.h"

#include "source/common/protobuf/utility.h"
#include "source/common/quic/envoy_quic_connection_id_generator_factory.h"
#include "source/common/quic/envoy_quic_dispatcher.h"
#include "source/common/quic/envoy_quic_proof_source_factory_interface.h"
#include "source/common/quic/envoy_quic_server_preferred_address_config_factory.h"
#include "source/common/runtime/runtime_protos.h"
#include "source/server/active_udp_listener.h"

#include "quiche/quic/load_balancer/load_balancer_encoder.h"

namespace Envoy {
namespace Quic {

// QUIC specific UdpListenerCallbacks implementation which delegates incoming
// packets, write signals and listener errors to QuicDispatcher.
class ActiveQuicListener : public Envoy::Server::ActiveUdpListenerBase,
                           Logger::Loggable<Logger::Id::quic> {
public:
  // TODO(bencebeky): Tune this value.
  static const size_t kNumSessionsToCreatePerLoop = 16;

  ActiveQuicListener(Runtime::Loader& runtime, uint32_t worker_index, uint32_t concurrency,
                     Event::Dispatcher& dispatcher, Network::UdpConnectionHandler& parent,
                     Network::SocketSharedPtr&& listen_socket,
                     Network::ListenerConfig& listener_config, const quic::QuicConfig& quic_config,
                     bool kernel_worker_routing,
                     const envoy::config::core::v3::RuntimeFeatureFlag& enabled,
                     QuicStatNames& quic_stat_names,
                     uint32_t packets_to_read_to_connection_count_ratio,
                     EnvoyQuicCryptoServerStreamFactoryInterface& crypto_server_stream_factory,
                     EnvoyQuicProofSourceFactoryInterface& proof_source_factory,
                     QuicConnectionIdGeneratorPtr&& cid_generator,
                     QuicConnectionIdWorkerSelector worker_selector);

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
  size_t numPacketsExpectedPerEventLoop() const override;

  // ActiveListenerImplBase
  void pauseListening() override;
  void resumeListening() override;
  void shutdownListener(const Network::ExtraShutdownListenerOptions& options) override;
  void updateListenerConfig(Network::ListenerConfig& config) override;
  void onFilterChainDraining(
      const std::list<const Network::FilterChain*>& draining_filter_chains) override;

protected:
  Event::Dispatcher& dispatcher() { return dispatcher_; }

private:
  friend class ActiveQuicListenerPeer;

  void closeConnectionsWithFilterChain(const Network::FilterChain* filter_chain);

  uint8_t random_seed_[16];
  std::unique_ptr<quic::QuicCryptoServerConfig> crypto_config_;
  Event::Dispatcher& dispatcher_;
  quic::QuicVersionManager version_manager_;
  std::unique_ptr<EnvoyQuicDispatcher> quic_dispatcher_;
  const bool kernel_worker_routing_;
  absl::optional<Runtime::FeatureFlag> enabled_{};
  Network::UdpPacketWriter* udp_packet_writer_;

  // The number of runs of the event loop in which at least one CHLO was buffered.
  // TODO(ggreenway): Consider making this a published stat, or some variation of this information.
  uint64_t event_loops_with_buffered_chlo_for_test_{0};
  uint32_t packets_to_read_to_connection_count_ratio_;
  EnvoyQuicCryptoServerStreamFactoryInterface& crypto_server_stream_factory_;
  const QuicConnectionIdGeneratorPtr connection_id_generator_;
  const QuicConnectionIdWorkerSelector select_connection_id_worker_;
  // Latches envoy.reloadable_features.quic_reject_all at the beginning of each event loop.
  bool reject_all_{false};
  // During hot restart, an optional handler for packets that weren't for existing connections.
  OptRef<Network::NonDispatchedUdpPacketHandler> non_dispatched_udp_packet_handler_;
};

using ActiveQuicListenerPtr = std::unique_ptr<ActiveQuicListener>;

// A factory to create ActiveQuicListener based on given config.
class ActiveQuicListenerFactory : public Network::ActiveUdpListenerFactory,
                                  Logger::Loggable<Logger::Id::quic> {
public:
  ActiveQuicListenerFactory(const envoy::config::listener::v3::QuicProtocolOptions& config,
                            uint32_t concurrency, QuicStatNames& quic_stat_names,
                            ProtobufMessage::ValidationVisitor& validation_visitor,
                            ProcessContextOptRef context);

  // Network::ActiveUdpListenerFactory.
  Network::ConnectionHandler::ActiveUdpListenerPtr
  createActiveUdpListener(Runtime::Loader& runtime, uint32_t worker_index,
                          Network::UdpConnectionHandler& parent,
                          Network::SocketSharedPtr&& listen_socket_ptr,
                          Event::Dispatcher& dispatcher, Network::ListenerConfig& config) override;
  bool isTransportConnectionless() const override { return false; }
  const Network::Socket::OptionsSharedPtr& socketOptions() const override { return options_; }

  static void setDisableKernelBpfPacketRoutingForTest(bool val) {
    disable_kernel_bpf_packet_routing_for_test_ = val;
  }

protected:
  virtual Network::ConnectionHandler::ActiveUdpListenerPtr createActiveQuicListener(
      Runtime::Loader& runtime, uint32_t worker_index, uint32_t concurrency,
      Event::Dispatcher& dispatcher, Network::UdpConnectionHandler& parent,
      Network::SocketSharedPtr&& listen_socket, Network::ListenerConfig& listener_config,
      const quic::QuicConfig& quic_config, bool kernel_worker_routing,
      const envoy::config::core::v3::RuntimeFeatureFlag& enabled, QuicStatNames& quic_stat_names,
      uint32_t packets_to_read_to_connection_count_ratio,
      EnvoyQuicCryptoServerStreamFactoryInterface& crypto_server_stream_factory,
      EnvoyQuicProofSourceFactoryInterface& proof_source_factory,
      QuicConnectionIdGeneratorPtr&& cid_generator);

private:
  friend class ActiveQuicListenerFactoryPeer;

  absl::optional<std::reference_wrapper<EnvoyQuicCryptoServerStreamFactoryInterface>>
      crypto_server_stream_factory_;
  absl::optional<std::reference_wrapper<EnvoyQuicProofSourceFactoryInterface>>
      proof_source_factory_;
  EnvoyQuicConnectionIdGeneratorFactoryPtr quic_cid_generator_factory_;
  EnvoyQuicServerPreferredAddressConfigPtr server_preferred_address_config_;
  quic::QuicConfig quic_config_;
  const uint32_t concurrency_;
  envoy::config::core::v3::RuntimeFeatureFlag enabled_;
  QuicStatNames& quic_stat_names_;
  const uint32_t packets_to_read_to_connection_count_ratio_;
  const Network::Socket::OptionsSharedPtr options_{std::make_shared<Network::Socket::Options>()};
  QuicConnectionIdWorkerSelector worker_selector_;
  bool kernel_worker_routing_{};
  ProcessContextOptRef context_;

  static bool disable_kernel_bpf_packet_routing_for_test_;
};

} // namespace Quic
} // namespace Envoy
