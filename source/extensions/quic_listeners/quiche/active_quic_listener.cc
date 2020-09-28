#include "extensions/quic_listeners/quiche/active_quic_listener.h"

#include "envoy/network/exception.h"

#if defined(__linux__)
#include <linux/filter.h>
#endif

#include <vector>

#include "common/runtime/runtime_features.h"
#include "extensions/quic_listeners/quiche/envoy_quic_alarm_factory.h"
#include "extensions/quic_listeners/quiche/envoy_quic_connection_helper.h"
#include "extensions/quic_listeners/quiche/envoy_quic_dispatcher.h"
#include "extensions/quic_listeners/quiche/envoy_quic_proof_source.h"
#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"
#include "extensions/quic_listeners/quiche/envoy_quic_packet_writer.h"

namespace Envoy {
namespace Quic {

ActiveQuicListener::ActiveQuicListener(
    uint32_t worker_index, uint32_t concurrency, Event::Dispatcher& dispatcher,
    Network::ConnectionHandler& parent, Network::ListenerConfig& listener_config,
    const quic::QuicConfig& quic_config, Network::Socket::OptionsSharedPtr options,
    bool kernel_worker_routing, const envoy::config::core::v3::RuntimeFeatureFlag& enabled)
    : ActiveQuicListener(worker_index, concurrency, dispatcher, parent,
                         listener_config.listenSocketFactory().getListenSocket(), listener_config,
                         quic_config, std::move(options), kernel_worker_routing, enabled) {}

ActiveQuicListener::ActiveQuicListener(
    uint32_t worker_index, uint32_t concurrency, Event::Dispatcher& dispatcher,
    Network::ConnectionHandler& parent, Network::SocketSharedPtr listen_socket,
    Network::ListenerConfig& listener_config, const quic::QuicConfig& quic_config,
    Network::Socket::OptionsSharedPtr options, bool kernel_worker_routing,
    const envoy::config::core::v3::RuntimeFeatureFlag& enabled)
    : Server::ActiveUdpListenerBase(worker_index, concurrency, parent, *listen_socket,
                                    dispatcher.createUdpListener(listen_socket, *this),
                                    &listener_config),
      dispatcher_(dispatcher), version_manager_(quic::CurrentSupportedVersions()),
      kernel_worker_routing_(kernel_worker_routing),
      enabled_(enabled, Runtime::LoaderSingleton::get()) {
  if (options != nullptr) {
    const bool ok = Network::Socket::applyOptions(
        options, listen_socket_, envoy::config::core::v3::SocketOption::STATE_BOUND);
    if (!ok) {
      // TODO(fcoras): consider removing the fd from the log message
      ENVOY_LOG(warn, "Failed to apply socket options to socket {} on listener {} after binding",
                listen_socket_.ioHandle().fdDoNotUse(), listener_config.name());
      throw Network::CreateListenerException("Failed to apply socket options.");
    }
    listen_socket_.addOptions(options);
  }

  quic::QuicRandom* const random = quic::QuicRandom::GetInstance();
  random->RandBytes(random_seed_, sizeof(random_seed_));
  crypto_config_ = std::make_unique<quic::QuicCryptoServerConfig>(
      quiche::QuicheStringPiece(reinterpret_cast<char*>(random_seed_), sizeof(random_seed_)),
      quic::QuicRandom::GetInstance(),
      std::make_unique<EnvoyQuicProofSource>(listen_socket_, listener_config.filterChainManager(),
                                             stats_),
      quic::KeyExchangeSource::Default());
  auto connection_helper = std::make_unique<EnvoyQuicConnectionHelper>(dispatcher_);
  crypto_config_->AddDefaultConfig(random, connection_helper->GetClock(),
                                   quic::QuicCryptoServerConfig::ConfigOptions());
  auto alarm_factory =
      std::make_unique<EnvoyQuicAlarmFactory>(dispatcher_, *connection_helper->GetClock());
  quic_dispatcher_ = std::make_unique<EnvoyQuicDispatcher>(
      crypto_config_.get(), quic_config, &version_manager_, std::move(connection_helper),
      std::move(alarm_factory), quic::kQuicDefaultConnectionIdLength, parent, *config_, stats_,
      per_worker_stats_, dispatcher, listen_socket_);

  // Create udp_packet_writer
  Network::UdpPacketWriterPtr udp_packet_writer =
      listener_config.udpPacketWriterFactory()->get().createUdpPacketWriter(
          listen_socket_.ioHandle(), listener_config.listenerScope());
  udp_packet_writer_ = udp_packet_writer.get();

  // Some packet writers (like `UdpGsoBatchWriter`) already directly implement
  // `quic::QuicPacketWriter` and can be used directly here. Other types need
  // `EnvoyQuicPacketWriter` as an adapter.
  auto* quic_packet_writer = dynamic_cast<quic::QuicPacketWriter*>(udp_packet_writer.get());
  if (quic_packet_writer != nullptr) {
    quic_dispatcher_->InitializeWithWriter(quic_packet_writer);
    udp_packet_writer.release();
  } else {
    quic_dispatcher_->InitializeWithWriter(new EnvoyQuicPacketWriter(std::move(udp_packet_writer)));
  }
}

ActiveQuicListener::~ActiveQuicListener() { onListenerShutdown(); }

void ActiveQuicListener::onListenerShutdown() {
  ENVOY_LOG(info, "Quic listener {} shutdown.", config_->name());
  quic_dispatcher_->Shutdown();
  udp_listener_.reset();
}

void ActiveQuicListener::onDataWorker(Network::UdpRecvData&& data) {
  if (!enabled_.enabled()) {
    return;
  }

  quic::QuicSocketAddress peer_address(
      envoyIpAddressToQuicSocketAddress(data.addresses_.peer_->ip()));
  quic::QuicSocketAddress self_address(
      envoyIpAddressToQuicSocketAddress(data.addresses_.local_->ip()));
  quic::QuicTime timestamp =
      quic::QuicTime::Zero() +
      quic::QuicTime::Delta::FromMicroseconds(std::chrono::duration_cast<std::chrono::microseconds>(
                                                  data.receive_time_.time_since_epoch())
                                                  .count());
  ASSERT(data.buffer_->getRawSlices().size() == 1);
  Buffer::RawSliceVector slices = data.buffer_->getRawSlices(/*max_slices=*/1);
  // TODO(danzh): pass in TTL and UDP header.
  quic::QuicReceivedPacket packet(reinterpret_cast<char*>(slices[0].mem_), slices[0].len_,
                                  timestamp, /*owns_buffer=*/false, /*ttl=*/0, /*ttl_valid=*/false,
                                  /*packet_headers=*/nullptr, /*headers_length=*/0,
                                  /*owns_header_buffer*/ false);
  quic_dispatcher_->ProcessPacket(self_address, peer_address, packet);

  if (quic_dispatcher_->HasChlosBuffered()) {
    // If there are any buffered CHLOs, activate a read event for the next event loop to process
    // them.
    udp_listener_->activateRead();
  }
}

void ActiveQuicListener::onReadReady() {
  if (!enabled_.enabled()) {
    ENVOY_LOG(trace, "Quic listener {}: runtime disabled", config_->name());
    return;
  }

  if (quic_dispatcher_->HasChlosBuffered()) {
    event_loops_with_buffered_chlo_for_test_++;
  }

  quic_dispatcher_->ProcessBufferedChlos(kNumSessionsToCreatePerLoop);

  // If there were more buffered than the limit, schedule again for the next event loop.
  if (quic_dispatcher_->HasChlosBuffered()) {
    udp_listener_->activateRead();
  }
}

void ActiveQuicListener::onWriteReady(const Network::Socket& /*socket*/) {
  quic_dispatcher_->OnCanWrite();
}

void ActiveQuicListener::pauseListening() { quic_dispatcher_->StopAcceptingNewConnections(); }

void ActiveQuicListener::resumeListening() { quic_dispatcher_->StartAcceptingNewConnections(); }

void ActiveQuicListener::shutdownListener() {
  // Same as pauseListening() because all we want is to stop accepting new
  // connections.
  quic_dispatcher_->StopAcceptingNewConnections();
}

uint32_t ActiveQuicListener::destination(const Network::UdpRecvData& data) const {
  if (kernel_worker_routing_) {
    // The kernel has already routed the packet correctly. Make it stay on the current worker.
    return worker_index_;
  }

  // This implementation is not as performant as it could be. It will result in most packets being
  // delivered by the kernel to the wrong worker, and then redirected to the correct worker.
  //
  // This could possibly be improved by keeping a global table of connection IDs, so that a new
  // connection will add its connection ID to the table on the current worker, and so packets should
  // be delivered to the correct worker by the kernel unless the client changes address.

  // This is a re-implementation of the same algorithm written in BPF in
  // ``ActiveQuicListenerFactory::createActiveUdpListener``
  const uint64_t packet_length = data.buffer_->length();
  if (packet_length < 9) {
    return worker_index_;
  }

  uint8_t first_octet;
  data.buffer_->copyOut(0, sizeof(first_octet), &first_octet);

  uint32_t connection_id_snippet;
  if (first_octet & 0x80) {
    // IETF QUIC long header.
    // The connection id starts from 7th byte.
    // Minimum length of a long header packet is 14.
    if (packet_length < 14) {
      return worker_index_;
    }

    data.buffer_->copyOut(6, sizeof(connection_id_snippet), &connection_id_snippet);
  } else {
    // IETF QUIC short header, or gQUIC.
    // The connection id starts from 2nd byte.
    data.buffer_->copyOut(1, sizeof(connection_id_snippet), &connection_id_snippet);
  }

  connection_id_snippet = htonl(connection_id_snippet);
  return connection_id_snippet % concurrency_;
}

ActiveQuicListenerFactory::ActiveQuicListenerFactory(
    const envoy::config::listener::v3::QuicProtocolOptions& config, uint32_t concurrency)
    : concurrency_(concurrency), enabled_(config.enabled()) {
  uint64_t idle_network_timeout_ms =
      config.has_idle_timeout() ? DurationUtil::durationToMilliseconds(config.idle_timeout())
                                : 300000;
  quic_config_.SetIdleNetworkTimeout(
      quic::QuicTime::Delta::FromMilliseconds(idle_network_timeout_ms));
  int32_t max_time_before_crypto_handshake_ms =
      config.has_crypto_handshake_timeout()
          ? DurationUtil::durationToMilliseconds(config.crypto_handshake_timeout())
          : 20000;
  quic_config_.set_max_time_before_crypto_handshake(
      quic::QuicTime::Delta::FromMilliseconds(max_time_before_crypto_handshake_ms));
  int32_t max_streams = PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_concurrent_streams, 100);
  quic_config_.SetMaxBidirectionalStreamsToSend(max_streams);
  quic_config_.SetMaxUnidirectionalStreamsToSend(max_streams);
}

Network::ConnectionHandler::ActiveUdpListenerPtr ActiveQuicListenerFactory::createActiveUdpListener(
    uint32_t worker_index, Network::ConnectionHandler& parent, Event::Dispatcher& disptacher,
    Network::ListenerConfig& config) {
  bool kernel_worker_routing = false;
  std::unique_ptr<Network::Socket::Options> options = std::make_unique<Network::Socket::Options>();

#if defined(SO_ATTACH_REUSEPORT_CBPF) && defined(__linux__)
  // This BPF filter reads the 1st word of QUIC connection id in the UDP payload and mods it by the
  // number of workers to get the socket index in the SO_REUSEPORT socket groups. QUIC packets
  // should be at least 9 bytes, with the 1st byte indicating one of the below QUIC packet headers:
  // 1) IETF QUIC long header: most significant bit is 1. The connection id starts from the 7th
  // byte.
  // 2) IETF QUIC short header: most significant bit is 0. The connection id starts from 2nd
  // byte.
  // 3) Google QUIC header: most significant bit is 0. The connection id starts from 2nd
  // byte.
  // Any packet that doesn't belong to any of the three packet header types are dispatched
  // based on 5-tuple source/destination addresses.
  // SPELLCHECKER(off)
  std::vector<sock_filter> filter = {
      {0x80, 0, 0, 0000000000}, //                   ld len
      {0x35, 0, 9, 0x00000009}, //                   jlt #0x9, packet_too_short
      {0x30, 0, 0, 0000000000}, //                   ldb [0]
      {0x54, 0, 0, 0x00000080}, //                   and #0x80
      {0x15, 0, 2, 0000000000}, //                   jne #0, ietf_long_header
      {0x20, 0, 0, 0x00000001}, //                   ld [1]
      {0x05, 0, 0, 0x00000005}, //                   ja return
      {0x80, 0, 0, 0000000000}, // ietf_long_header: ld len
      {0x35, 0, 2, 0x0000000e}, //                   jlt #0xe, packet_too_short
      {0x20, 0, 0, 0x00000006}, //                   ld [6]
      {0x05, 0, 0, 0x00000001}, //                   ja return
      {0x20, 0, 0,              // packet_too_short: ld rxhash
       static_cast<uint32_t>(SKF_AD_OFF + SKF_AD_RXHASH)},
      {0x94, 0, 0, concurrency_}, // return:         mod #socket_count
      {0x16, 0, 0, 0000000000},   //                 ret a
  };
  // SPELLCHECKER(on)
  sock_fprog prog;
  // This option only needs to be applied once to any one of the sockets in SO_REUSEPORT socket
  // group. One of the listener will be created with this socket option.
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.prefer_quic_kernel_bpf_packet_routing")) {
    absl::call_once(install_bpf_once_, [&]() {
      if (concurrency_ > 1) {
        prog.len = filter.size();
        prog.filter = filter.data();
        options->push_back(std::make_shared<Network::SocketOptionImpl>(
            envoy::config::core::v3::SocketOption::STATE_BOUND, ENVOY_ATTACH_REUSEPORT_CBPF,
            absl::string_view(reinterpret_cast<char*>(&prog), sizeof(prog))));
      }
    });

    kernel_worker_routing = true;
  };

#else
  if (concurrency_ != 1) {
    ENVOY_LOG(warn, "Efficient routing of QUIC packets to the correct worker is not supported or "
                    "not implemented by Envoy on this platform. QUIC performance may be degraded.");
  }
#endif

  return std::make_unique<ActiveQuicListener>(worker_index, concurrency_, disptacher, parent,
                                              config, quic_config_, std::move(options),
                                              kernel_worker_routing, enabled_);
} // namespace Quic

} // namespace Quic
} // namespace Envoy
