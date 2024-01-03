#include "source/common/quic/active_quic_listener.h"

#include <vector>

#include "envoy/extensions/quic/connection_id_generator/v3/envoy_deterministic_connection_id_generator.pb.h"
#include "envoy/extensions/quic/crypto_stream/v3/crypto_stream.pb.h"
#include "envoy/extensions/quic/proof_source/v3/proof_source.pb.h"
#include "envoy/network/exception.h"

#include "source/common/config/utility.h"
#include "source/common/http/utility.h"
#include "source/common/network/socket_option_impl.h"
#include "source/common/network/udp_listener_impl.h"
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

bool ActiveQuicListenerFactory::disable_kernel_bpf_packet_routing_for_test_ = false;

ActiveQuicListener::ActiveQuicListener(
    Runtime::Loader& runtime, uint32_t worker_index, uint32_t concurrency,
    Event::Dispatcher& dispatcher, Network::UdpConnectionHandler& parent,
    Network::SocketSharedPtr&& listen_socket, Network::ListenerConfig& listener_config,
    const quic::QuicConfig& quic_config, bool kernel_worker_routing,
    const envoy::config::core::v3::RuntimeFeatureFlag& enabled, QuicStatNames& quic_stat_names,
    uint32_t packets_to_read_to_connection_count_ratio,
    EnvoyQuicCryptoServerStreamFactoryInterface& crypto_server_stream_factory,
    EnvoyQuicProofSourceFactoryInterface& proof_source_factory,
    QuicConnectionIdGeneratorPtr&& cid_generator, QuicConnectionIdWorkerSelector worker_selector)
    : Server::ActiveUdpListenerBase(
          worker_index, concurrency, parent, *listen_socket,
          std::make_unique<Network::UdpListenerImpl>(
              dispatcher, listen_socket, *this, dispatcher.timeSource(),
              listener_config.udpListenerConfig()->config().downstream_socket_config()),
          &listener_config),
      dispatcher_(dispatcher), version_manager_(quic::CurrentSupportedHttp3Versions()),
      kernel_worker_routing_(kernel_worker_routing),
      packets_to_read_to_connection_count_ratio_(packets_to_read_to_connection_count_ratio),
      crypto_server_stream_factory_(crypto_server_stream_factory),
      connection_id_generator_(std::move(cid_generator)),
      select_connection_id_worker_(std::move(worker_selector)) {
  ASSERT(!GetQuicFlag(quic_header_size_limit_includes_overhead));
  ASSERT(select_connection_id_worker_ != nullptr);

  enabled_.emplace(Runtime::FeatureFlag(enabled, runtime));

  quic::QuicRandom* const random = quic::QuicRandom::GetInstance();
  random->RandBytes(random_seed_, sizeof(random_seed_));
  crypto_config_ = std::make_unique<quic::QuicCryptoServerConfig>(
      absl::string_view(reinterpret_cast<char*>(random_seed_), sizeof(random_seed_)),
      quic::QuicRandom::GetInstance(),
      proof_source_factory.createQuicProofSource(
          listen_socket_, listener_config.filterChainManager(), stats_, dispatcher.timeSource()),
      quic::KeyExchangeSource::Default());
  auto connection_helper = std::make_unique<EnvoyQuicConnectionHelper>(dispatcher_);
  crypto_config_->AddDefaultConfig(random, connection_helper->GetClock(),
                                   quic::QuicCryptoServerConfig::ConfigOptions());
  auto alarm_factory =
      std::make_unique<EnvoyQuicAlarmFactory>(dispatcher_, *connection_helper->GetClock());
  quic_dispatcher_ = std::make_unique<EnvoyQuicDispatcher>(
      crypto_config_.get(), quic_config, &version_manager_, std::move(connection_helper),
      std::move(alarm_factory), quic::kQuicDefaultConnectionIdLength, parent, *config_, stats_,
      per_worker_stats_, dispatcher, listen_socket_, quic_stat_names, crypto_server_stream_factory_,
      *connection_id_generator_);

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
  if ((enabled_.has_value() && !enabled_.value().enabled()) || reject_all_) {
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
  if (!quic_dispatcher_->processPacket(self_address, peer_address, packet)) {
    if (non_dispatched_udp_packet_handler_.has_value()) {
      non_dispatched_udp_packet_handler_->handle(worker_index_, std::move(data));
    }
  }

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
  const bool reject_all =
      Runtime::runtimeFeatureEnabled("envoy.reloadable_features.quic_reject_all");
  if (reject_all != reject_all_) {
    reject_all_ = reject_all;
    if (reject_all_) {
      ENVOY_LOG(trace, "Quic listener {}: start rejecting traffic.", config_->name());
    } else {
      ENVOY_LOG(trace, "Quic listener {}: start accepting traffic.", config_->name());
    }
  }
  if (reject_all_) {
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

void ActiveQuicListener::shutdownListener(const Network::ExtraShutdownListenerOptions& options) {
  non_dispatched_udp_packet_handler_ = options.non_dispatched_udp_packet_handler_;

  // Same as pauseListening() because all we want is to stop accepting new
  // connections.
  quic_dispatcher_->StopAcceptingNewConnections();
}

uint32_t ActiveQuicListener::destination(const Network::UdpRecvData& data) const {
  if (kernel_worker_routing_) {
    // The kernel has already routed the packet correctly. Make it stay on the current worker.
    return worker_index_;
  }

  // Taking this path is not as performant as it could be. It means most packets are being
  // delivered by the kernel to the wrong worker, and then redirected to the correct worker.
  return select_connection_id_worker_(*data.buffer_, worker_index_);
}

size_t ActiveQuicListener::numPacketsExpectedPerEventLoop() const {
  // Expect each session to read packets_to_read_to_connection_count_ratio_ number of packets in
  // this read event.
  return quic_dispatcher_->NumSessions() * packets_to_read_to_connection_count_ratio_;
}

void ActiveQuicListener::updateListenerConfig(Network::ListenerConfig& config) {
  config_ = &config;
  dynamic_cast<EnvoyQuicProofSource*>(crypto_config_->proof_source())
      ->updateFilterChainManager(config.filterChainManager());
  quic_dispatcher_->updateListenerConfig(config);
}

void ActiveQuicListener::onFilterChainDraining(
    const std::list<const Network::FilterChain*>& draining_filter_chains) {
  for (auto* filter_chain : draining_filter_chains) {
    closeConnectionsWithFilterChain(filter_chain);
  }
}

void ActiveQuicListener::closeConnectionsWithFilterChain(const Network::FilterChain* filter_chain) {
  quic_dispatcher_->closeConnectionsWithFilterChain(filter_chain);
}

ActiveQuicListenerFactory::ActiveQuicListenerFactory(
    const envoy::config::listener::v3::QuicProtocolOptions& config, uint32_t concurrency,
    QuicStatNames& quic_stat_names, ProtobufMessage::ValidationVisitor& validation_visitor,
    ProcessContextOptRef context)
    : concurrency_(concurrency), enabled_(config.enabled()), quic_stat_names_(quic_stat_names),
      packets_to_read_to_connection_count_ratio_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, packets_to_read_to_connection_count_ratio,
                                          DEFAULT_PACKETS_TO_READ_PER_CONNECTION)),
      context_(context) {
  const int64_t idle_network_timeout_ms =
      config.has_idle_timeout() ? DurationUtil::durationToMilliseconds(config.idle_timeout())
                                : 300000;
  const int64_t minimal_idle_network_timeout_ms = 1;
  quic_config_.SetIdleNetworkTimeout(quic::QuicTime::Delta::FromMilliseconds(
      std::max(minimal_idle_network_timeout_ms, idle_network_timeout_ms)));
  const int64_t max_time_before_crypto_handshake_ms =
      config.has_crypto_handshake_timeout()
          ? DurationUtil::durationToMilliseconds(config.crypto_handshake_timeout())
          : 20000;
  quic_config_.set_max_time_before_crypto_handshake(quic::QuicTime::Delta::FromMilliseconds(
      std::max(quic::kInitialIdleTimeoutSecs * 1000, max_time_before_crypto_handshake_ms)));
  convertQuicConfig(config.quic_protocol_options(), quic_config_);

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

  // Initialize connection ID generator factory.
  envoy::config::core::v3::TypedExtensionConfig cid_generator_config;
  if (!config.has_connection_id_generator_config()) {
    cid_generator_config.set_name("envoy.quic.deterministic_connection_id_generator");
    envoy::extensions::quic::connection_id_generator::v3::DeterministicConnectionIdGeneratorConfig
        empty_connection_id_generator_config;
    cid_generator_config.mutable_typed_config()->PackFrom(empty_connection_id_generator_config);
  } else {
    cid_generator_config = config.connection_id_generator_config();
  }
  auto& cid_generator_config_factory =
      Config::Utility::getAndCheckFactory<EnvoyQuicConnectionIdGeneratorConfigFactory>(
          cid_generator_config);
  quic_cid_generator_factory_ = cid_generator_config_factory.createQuicConnectionIdGeneratorFactory(
      *Config::Utility::translateToFactoryConfig(cid_generator_config, validation_visitor,
                                                 cid_generator_config_factory));

  if (config.has_server_preferred_address_config()) {
    const envoy::config::core::v3::TypedExtensionConfig& server_preferred_address_config =
        config.server_preferred_address_config();
    auto& server_preferred_address_config_factory =
        Config::Utility::getAndCheckFactory<EnvoyQuicServerPreferredAddressConfigFactory>(
            server_preferred_address_config);
    server_preferred_address_config_ =
        server_preferred_address_config_factory.createServerPreferredAddressConfig(
            *Config::Utility::translateToFactoryConfig(config.server_preferred_address_config(),
                                                       validation_visitor,
                                                       server_preferred_address_config_factory),
            validation_visitor, context_);
  }

  worker_selector_ =
      quic_cid_generator_factory_->getCompatibleConnectionIdWorkerSelector(concurrency_);
#if defined(SO_ATTACH_REUSEPORT_CBPF) && defined(__linux__)
  if (!disable_kernel_bpf_packet_routing_for_test_) {
    if (concurrency_ > 1) {
      options_->push_back(
          quic_cid_generator_factory_->createCompatibleLinuxBpfSocketOption(concurrency_));
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
    Runtime::Loader& runtime, uint32_t worker_index, Network::UdpConnectionHandler& parent,
    Network::SocketSharedPtr&& listen_socket_ptr, Event::Dispatcher& dispatcher,
    Network::ListenerConfig& config) {
  ASSERT(crypto_server_stream_factory_.has_value());
  if (server_preferred_address_config_ != nullptr) {
    std::pair<quic::QuicSocketAddress, quic::QuicSocketAddress> addresses =
        server_preferred_address_config_->getServerPreferredAddresses(
            listen_socket_ptr->connectionInfoProvider().localAddress());
    quic::QuicSocketAddress v4_address = addresses.first;
    if (v4_address.IsInitialized()) {
      ENVOY_BUG(v4_address.host().address_family() == quiche::IpAddressFamily::IP_V4,
                absl::StrCat("Configured IPv4 server's preferred address isn't a v4 address:",
                             v4_address.ToString()));
      quic_config_.SetIPv4AlternateServerAddressToSend(v4_address);
    }
    quic::QuicSocketAddress v6_address = addresses.second;
    if (v6_address.IsInitialized()) {
      ENVOY_BUG(v6_address.host().address_family() == quiche::IpAddressFamily::IP_V6,
                absl::StrCat("Configured IPv6 server's preferred address isn't a v6 address:",
                             v4_address.ToString()));
      quic_config_.SetIPv6AlternateServerAddressToSend(v6_address);
    }
  }

  return createActiveQuicListener(
      runtime, worker_index, concurrency_, dispatcher, parent, std::move(listen_socket_ptr), config,
      quic_config_, kernel_worker_routing_, enabled_, quic_stat_names_,
      packets_to_read_to_connection_count_ratio_, crypto_server_stream_factory_.value(),
      proof_source_factory_.value(),
      quic_cid_generator_factory_->createQuicConnectionIdGenerator(worker_index));
}
Network::ConnectionHandler::ActiveUdpListenerPtr
ActiveQuicListenerFactory::createActiveQuicListener(
    Runtime::Loader& runtime, uint32_t worker_index, uint32_t concurrency,
    Event::Dispatcher& dispatcher, Network::UdpConnectionHandler& parent,
    Network::SocketSharedPtr&& listen_socket, Network::ListenerConfig& listener_config,
    const quic::QuicConfig& quic_config, bool kernel_worker_routing,
    const envoy::config::core::v3::RuntimeFeatureFlag& enabled, QuicStatNames& quic_stat_names,
    uint32_t packets_to_read_to_connection_count_ratio,
    EnvoyQuicCryptoServerStreamFactoryInterface& crypto_server_stream_factory,
    EnvoyQuicProofSourceFactoryInterface& proof_source_factory,
    QuicConnectionIdGeneratorPtr&& cid_generator) {
  return std::make_unique<ActiveQuicListener>(
      runtime, worker_index, concurrency, dispatcher, parent, std::move(listen_socket),
      listener_config, quic_config, kernel_worker_routing, enabled, quic_stat_names,
      packets_to_read_to_connection_count_ratio, crypto_server_stream_factory, proof_source_factory,
      std::move(cid_generator), worker_selector_);
}

} // namespace Quic
} // namespace Envoy
