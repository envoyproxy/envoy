#include "source/common/quic/active_quic_listener.h"

#include <vector>

#include "envoy/extensions/quic/crypto_stream/v3/crypto_stream.pb.h"
#include "envoy/extensions/quic/proof_source/v3/proof_source.pb.h"
#include "envoy/network/exception.h"

#include "source/common/config/utility.h"
#include "source/common/http/utility.h"
#include "source/common/network/socket_option_impl.h"
#include "source/common/quic/envoy_quic_alarm_factory.h"
#include "source/common/quic/envoy_quic_connection_helper.h"
#include "source/common/quic/envoy_quic_dispatcher.h"
#include "source/common/quic/envoy_quic_packet_writer.h"
#include "source/common/quic/envoy_quic_proof_source.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/quic_network_connection.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Quic {

ActiveQuicListener::ActiveQuicListener(
    uint32_t worker_index, uint32_t concurrency, Event::Dispatcher& dispatcher,
    Network::UdpConnectionHandler& parent, Network::ListenerConfig& listener_config,
    const quic::QuicConfig& quic_config, bool kernel_worker_routing,
    const envoy::config::core::v3::RuntimeFeatureFlag& enabled, QuicStatNames& quic_stat_names,
    uint32_t packets_received_to_connection_count_ratio,
    EnvoyQuicCryptoServerStreamFactoryInterface& crypto_server_stream_factory,
    EnvoyQuicProofSourceFactoryInterface& proof_source_factory)
    : ActiveQuicListener(worker_index, concurrency, dispatcher, parent,
                         listener_config.listenSocketFactory().getListenSocket(worker_index),
                         listener_config, quic_config, kernel_worker_routing, enabled,
                         quic_stat_names, packets_received_to_connection_count_ratio,
                         crypto_server_stream_factory, proof_source_factory) {}

ActiveQuicListener::ActiveQuicListener(
    uint32_t worker_index, uint32_t concurrency, Event::Dispatcher& dispatcher,
    Network::UdpConnectionHandler& parent, Network::SocketSharedPtr listen_socket,
    Network::ListenerConfig& listener_config, const quic::QuicConfig& quic_config,
    bool kernel_worker_routing, const envoy::config::core::v3::RuntimeFeatureFlag& enabled,
    QuicStatNames& quic_stat_names, uint32_t packets_to_read_to_connection_count_ratio,
    EnvoyQuicCryptoServerStreamFactoryInterface& crypto_server_stream_factory,
    EnvoyQuicProofSourceFactoryInterface& proof_source_factory)
    : Server::ActiveUdpListenerBase(
          worker_index, concurrency, parent, *listen_socket,
          dispatcher.createUdpListener(
              listen_socket, *this,
              listener_config.udpListenerConfig()->config().downstream_socket_config()),
          &listener_config),
      dispatcher_(dispatcher), version_manager_(quic::CurrentSupportedHttp3Versions()),
      kernel_worker_routing_(kernel_worker_routing),
      packets_to_read_to_connection_count_ratio_(packets_to_read_to_connection_count_ratio),
      crypto_server_stream_factory_(crypto_server_stream_factory) {
  // This flag fix a QUICHE issue which may crash Envoy during connection close.
  SetQuicReloadableFlag(quic_single_ack_in_packet2, true);
  // Do not include 32-byte per-entry overhead while counting header size.
  quiche::FlagRegistry::getInstance();
  ASSERT(!GetQuicFlag(FLAGS_quic_header_size_limit_includes_overhead));

  if (Runtime::LoaderSingleton::getExisting()) {
    enabled_.emplace(Runtime::FeatureFlag(enabled, Runtime::LoaderSingleton::get()));
  }

  quic::QuicRandom* const random = quic::QuicRandom::GetInstance();
  random->RandBytes(random_seed_, sizeof(random_seed_));
  crypto_config_ = std::make_unique<quic::QuicCryptoServerConfig>(
      absl::string_view(reinterpret_cast<char*>(random_seed_), sizeof(random_seed_)),
      quic::QuicRandom::GetInstance(),
      proof_source_factory.createQuicProofSource(listen_socket_,
                                                 listener_config.filterChainManager(), stats_),
      quic::KeyExchangeSource::Default());
  auto connection_helper = std::make_unique<EnvoyQuicConnectionHelper>(dispatcher_);
  crypto_config_->AddDefaultConfig(random, connection_helper->GetClock(),
                                   quic::QuicCryptoServerConfig::ConfigOptions());
  auto alarm_factory =
      std::make_unique<EnvoyQuicAlarmFactory>(dispatcher_, *connection_helper->GetClock());
  quic_dispatcher_ = std::make_unique<EnvoyQuicDispatcher>(
      crypto_config_.get(), quic_config, &version_manager_, std::move(connection_helper),
      std::move(alarm_factory), quic::kQuicDefaultConnectionIdLength, parent, *config_, stats_,
      per_worker_stats_, dispatcher, listen_socket_, quic_stat_names,
      crypto_server_stream_factory_);

  // Create udp_packet_writer
  Network::UdpPacketWriterPtr udp_packet_writer =
      listener_config.udpListenerConfig()->packetWriterFactory().createUdpPacketWriter(
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
  if (enabled_.has_value() && !enabled_.value().enabled()) {
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
  Buffer::RawSlice slice = data.buffer_->frontSlice();
  ASSERT(data.buffer_->length() == slice.len_);
  // TODO(danzh): pass in TTL and UDP header.
  quic::QuicReceivedPacket packet(reinterpret_cast<char*>(slice.mem_), slice.len_, timestamp,
                                  /*owns_buffer=*/false, /*ttl=*/0, /*ttl_valid=*/false,
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
  if (enabled_.has_value() && !enabled_.value().enabled()) {
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

size_t ActiveQuicListener::numPacketsExpectedPerEventLoop() const {
  // Expect each session to read packets_to_read_to_connection_count_ratio_ number of packets in
  // this read event.
  return quic_dispatcher_->NumSessions() * packets_to_read_to_connection_count_ratio_;
}

ActiveQuicListenerFactory::ActiveQuicListenerFactory(
    const envoy::config::listener::v3::QuicProtocolOptions& config, uint32_t concurrency,
    QuicStatNames& quic_stat_names)
    : concurrency_(concurrency), enabled_(config.enabled()), quic_stat_names_(quic_stat_names),
      packets_to_read_to_connection_count_ratio_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, packets_to_read_to_connection_count_ratio,
                                          DEFAULT_PACKETS_TO_READ_PER_CONNECTION)) {
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
  int32_t max_streams =
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.quic_protocol_options(), max_concurrent_streams, 100);
  quic_config_.SetMaxBidirectionalStreamsToSend(max_streams);
  quic_config_.SetMaxUnidirectionalStreamsToSend(max_streams);
  configQuicInitialFlowControlWindow(config.quic_protocol_options(), quic_config_);

  // Initialize crypto stream factory.
  envoy::config::core::v3::TypedExtensionConfig crypto_stream_config;
  if (!config.has_crypto_stream_config()) {
    // If not specified, use the quic crypto stream created by QUICHE.
    crypto_stream_config.set_name("envoy.quic.crypto_stream.server.quiche");
    envoy::extensions::quic::crypto_stream::v3::CryptoServerStreamConfig empty_crypto_stream_config;
    crypto_stream_config.mutable_typed_config()->PackFrom(empty_crypto_stream_config);
  } else {
    crypto_stream_config = config.crypto_stream_config();
  }
  crypto_server_stream_factory_ =
      Config::Utility::getAndCheckFactory<EnvoyQuicCryptoServerStreamFactoryInterface>(
          crypto_stream_config);

  // Initialize proof source factory.
  envoy::config::core::v3::TypedExtensionConfig proof_source_config;
  if (!config.has_proof_source_config()) {
    proof_source_config.set_name("envoy.quic.proof_source.filter_chain");
    envoy::extensions::quic::proof_source::v3::ProofSourceConfig empty_proof_source_config;
    proof_source_config.mutable_typed_config()->PackFrom(empty_proof_source_config);
  } else {
    proof_source_config = config.proof_source_config();
  }
  proof_source_factory_ = Config::Utility::getAndCheckFactory<EnvoyQuicProofSourceFactoryInterface>(
      proof_source_config);

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
  filter_ = {
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
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.prefer_quic_kernel_bpf_packet_routing")) {
    if (concurrency_ > 1) {
      // Note that this option refers to the BPF program data above, which must live until the
      // option is used. The program is kept as a member variable for this purpose.
      prog_.len = filter_.size();
      prog_.filter = filter_.data();
      options_->push_back(std::make_shared<Network::SocketOptionImpl>(
          envoy::config::core::v3::SocketOption::STATE_BOUND, ENVOY_ATTACH_REUSEPORT_CBPF,
          absl::string_view(reinterpret_cast<char*>(&prog_), sizeof(prog_))));
    } else {
      ENVOY_LOG(info, "Not applying BPF because concurrency is 1");
    }

    kernel_worker_routing_ = true;
  };

#else
  if (concurrency_ != 1) {
    ENVOY_LOG(warn, "Efficient routing of QUIC packets to the correct worker is not supported or "
                    "not implemented by Envoy on this platform. QUIC performance may be degraded.");
  }
#endif
}

Network::ConnectionHandler::ActiveUdpListenerPtr ActiveQuicListenerFactory::createActiveUdpListener(
    uint32_t worker_index, Network::UdpConnectionHandler& parent, Event::Dispatcher& disptacher,
    Network::ListenerConfig& config) {
  ASSERT(crypto_server_stream_factory_.has_value());
  return std::make_unique<ActiveQuicListener>(
      worker_index, concurrency_, disptacher, parent, config, quic_config_, kernel_worker_routing_,
      enabled_, quic_stat_names_, packets_to_read_to_connection_count_ratio_,
      crypto_server_stream_factory_.value(), proof_source_factory_.value());
}

} // namespace Quic
} // namespace Envoy
