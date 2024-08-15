#include "source/common/quic/envoy_quic_dispatcher.h"

#include <openssl/crypto.h>

#include <functional>
#include <list>

#include "envoy/common/optref.h"

#include "source/common/common/safe_memcpy.h"
#include "source/common/quic/envoy_quic_connection_debug_visitor_factory_interface.h"
#include "source/common/quic/envoy_quic_server_connection.h"
#include "source/common/quic/envoy_quic_utils.h"

namespace Envoy {
namespace Quic {

namespace {

QuicDispatcherStats generateStats(Stats::Scope& store) {
  return {QUIC_DISPATCHER_STATS(POOL_COUNTER_PREFIX(store, "quic.dispatcher"))};
}

} // namespace

EnvoyQuicTimeWaitListManager::EnvoyQuicTimeWaitListManager(quic::QuicPacketWriter* writer,
                                                           Visitor* visitor,
                                                           const quic::QuicClock* clock,
                                                           quic::QuicAlarmFactory* alarm_factory,
                                                           QuicDispatcherStats& stats)
    : quic::QuicTimeWaitListManager(writer, visitor, clock, alarm_factory), stats_(stats) {}

void EnvoyQuicTimeWaitListManager::SendPublicReset(
    const quic::QuicSocketAddress& self_address, const quic::QuicSocketAddress& peer_address,
    quic::QuicConnectionId connection_id, bool ietf_quic, size_t received_packet_length,
    std::unique_ptr<quic::QuicPerPacketContext> packet_context) {
  ENVOY_LOG_EVERY_POW_2_MISC(info, "Sending Stateless Reset on connection {}",
                             connection_id.ToString());
  stats_.stateless_reset_packets_sent_.inc();
  quic::QuicTimeWaitListManager::SendPublicReset(self_address, peer_address, connection_id,
                                                 ietf_quic, received_packet_length,
                                                 std::move(packet_context));
}

EnvoyQuicDispatcher::EnvoyQuicDispatcher(
    const quic::QuicCryptoServerConfig* crypto_config, const quic::QuicConfig& quic_config,
    quic::QuicVersionManager* version_manager,
    std::unique_ptr<quic::QuicConnectionHelperInterface> helper,
    std::unique_ptr<quic::QuicAlarmFactory> alarm_factory,
    uint8_t expected_server_connection_id_length, Network::ConnectionHandler& connection_handler,
    Network::ListenerConfig& listener_config, Server::ListenerStats& listener_stats,
    Server::PerHandlerListenerStats& per_worker_stats, Event::Dispatcher& dispatcher,
    Network::Socket& listen_socket, QuicStatNames& quic_stat_names,
    EnvoyQuicCryptoServerStreamFactoryInterface& crypto_server_stream_factory,
    quic::ConnectionIdGeneratorInterface& generator,
    EnvoyQuicConnectionDebugVisitorFactoryInterfaceOptRef&& debug_visitor_factory)
    : quic::QuicDispatcher(&quic_config, crypto_config, version_manager, std::move(helper),
                           std::make_unique<EnvoyQuicCryptoServerStreamHelper>(),
                           std::move(alarm_factory), expected_server_connection_id_length,
                           generator),
      connection_handler_(connection_handler), listener_config_(&listener_config),
      listener_stats_(listener_stats), per_worker_stats_(per_worker_stats), dispatcher_(dispatcher),
      listen_socket_(listen_socket), quic_stat_names_(quic_stat_names),
      crypto_server_stream_factory_(crypto_server_stream_factory),
      quic_stats_(generateStats(listener_config.listenerScope())),
      connection_stats_({QUIC_CONNECTION_STATS(
          POOL_COUNTER_PREFIX(listener_config.listenerScope(), "quic.connection"))}),
      debug_visitor_factory_(std::move(debug_visitor_factory)) {}

void EnvoyQuicDispatcher::OnConnectionClosed(quic::QuicConnectionId connection_id,
                                             quic::QuicErrorCode error,
                                             const std::string& error_details,
                                             quic::ConnectionCloseSource source) {
  quic::QuicDispatcher::OnConnectionClosed(connection_id, error, error_details, source);
  listener_stats_.downstream_cx_active_.dec();
  per_worker_stats_.downstream_cx_active_.dec();
  connection_handler_.decNumConnections();
  quic_stat_names_.chargeQuicConnectionCloseStats(listener_config_->listenerScope(), error, source,
                                                  /*is_upstream*/ false);
}

quic::QuicTimeWaitListManager* EnvoyQuicDispatcher::CreateQuicTimeWaitListManager() {
  return new EnvoyQuicTimeWaitListManager(writer(), this, helper()->GetClock(), alarm_factory(),
                                          quic_stats_);
}

std::unique_ptr<quic::QuicSession> EnvoyQuicDispatcher::CreateQuicSession(
    quic::QuicConnectionId server_connection_id, const quic::QuicSocketAddress& self_address,
    const quic::QuicSocketAddress& peer_address, absl::string_view /*alpn*/,
    const quic::ParsedQuicVersion& version, const quic::ParsedClientHello& parsed_chlo,
    quic::ConnectionIdGeneratorInterface& connection_id_generator) {
  quic::QuicConfig quic_config = config();
  // TODO(danzh) use passed-in ALPN instead of hard-coded h3 after proof source interfaces takes in
  // ALPN.
  Network::ConnectionSocketPtr connection_socket = createServerConnectionSocket(
      listen_socket_.ioHandle(), self_address, peer_address, std::string(parsed_chlo.sni), "h3");
  auto stream_info = std::make_unique<StreamInfo::StreamInfoImpl>(
      dispatcher_.timeSource(), connection_socket->connectionInfoProviderSharedPtr(),
      StreamInfo::FilterState::LifeSpan::Connection);

  auto listener_filter_manager = std::make_unique<QuicListenerFilterManagerImpl>(
      dispatcher_, *connection_socket, *stream_info);
  const bool success = listener_config_->filterChainFactory().createQuicListenerFilterChain(
      *listener_filter_manager);
  const Network::FilterChain* filter_chain = nullptr;
  if (success) {
    listener_filter_manager->startFilterChain();
    // Quic listener filters are not supposed to pause the filter chain iteration unless it closes
    // the connection socket. If any listener filter have closed the socket, do not get a network
    // filter chain. Thus early fail the connection.
    if (connection_socket->ioHandle().isOpen()) {
      for (auto address_family : {quiche::IpAddressFamily::IP_V4, quiche::IpAddressFamily::IP_V6}) {
        absl::optional<quic::QuicSocketAddress> address =
            quic_config.GetPreferredAddressToSend(address_family);
        if (address.has_value() && address->IsInitialized() &&
            !listener_filter_manager->shouldAdvertiseServerPreferredAddress(address.value())) {
          quic_config.ClearAlternateServerAddressToSend(address_family);
        }
      }
      filter_chain =
          listener_config_->filterChainManager().findFilterChain(*connection_socket, *stream_info);
    }
  }

  auto quic_connection = std::make_unique<EnvoyQuicServerConnection>(
      server_connection_id, self_address, peer_address, *helper(), *alarm_factory(), writer(),
      /*owns_writer=*/false, quic::ParsedQuicVersionVector{version}, std::move(connection_socket),
      connection_id_generator, std::move(listener_filter_manager));
  auto quic_session = std::make_unique<EnvoyQuicServerSession>(
      quic_config, quic::ParsedQuicVersionVector{version}, std::move(quic_connection), this,
      session_helper(), crypto_config(), compressed_certs_cache(), dispatcher_,
      listener_config_->perConnectionBufferLimitBytes(), quic_stat_names_,
      listener_config_->listenerScope(), crypto_server_stream_factory_, std::move(stream_info),
      connection_stats_, debug_visitor_factory_);
  if (filter_chain != nullptr) {
    // Setup filter chain before Initialize().
    const bool has_filter_initialized =
        listener_config_->filterChainFactory().createNetworkFilterChain(
            *quic_session, filter_chain->networkFilterFactories());
    // QUIC listener must have HCM filter configured. Otherwise, stream creation later will fail.
    ASSERT(has_filter_initialized);
    connections_by_filter_chain_[filter_chain].push_front(
        std::reference_wrapper<Network::Connection>(*quic_session));
    quic_session->storeConnectionMapPosition(connections_by_filter_chain_, *filter_chain,
                                             connections_by_filter_chain_[filter_chain].begin());
  } else {
    quic_session->close(Network::ConnectionCloseType::FlushWrite, "no filter chain found");
  }
  quic_session->Initialize();
  connection_handler_.incNumConnections();
  listener_stats_.downstream_cx_active_.inc();
  listener_stats_.downstream_cx_total_.inc();
  per_worker_stats_.downstream_cx_active_.inc();
  per_worker_stats_.downstream_cx_total_.inc();
  return quic_session;
}

bool EnvoyQuicDispatcher::processPacket(const quic::QuicSocketAddress& self_address,
                                        const quic::QuicSocketAddress& peer_address,
                                        const quic::QuicReceivedPacket& packet) {
  // Assume a packet was dispatched successfully, if OnFailedToDispatchPacket is not called.
  current_packet_dispatch_success_ = true;
  ProcessPacket(self_address, peer_address, packet);
  return current_packet_dispatch_success_;
}

bool EnvoyQuicDispatcher::OnFailedToDispatchPacket(
    const quic::ReceivedPacketInfo& received_packet_info) {
  if (!accept_new_connections()) {
    current_packet_dispatch_success_ = false;
    return true;
  }
  return quic::QuicDispatcher::OnFailedToDispatchPacket(received_packet_info);
}

void EnvoyQuicDispatcher::closeConnectionsWithFilterChain(
    const Network::FilterChain* filter_chain) {
  auto iter = connections_by_filter_chain_.find(filter_chain);
  if (iter != connections_by_filter_chain_.end()) {
    std::list<std::reference_wrapper<Network::Connection>>& connections = iter->second;
    // Retain the number of connections in the list early because closing the connection will change
    // the size.
    const size_t num_connections = connections.size();
    for (size_t i = 0; i < num_connections; ++i) {
      Network::Connection& connection = connections.front().get();
      // This will remove the connection from the list. And the last removal will remove connections
      // from the map as well.
      connection.close(Network::ConnectionCloseType::NoFlush);
    }
    ASSERT(connections_by_filter_chain_.find(filter_chain) == connections_by_filter_chain_.end());
    if (num_connections > 0) {
      // Explicitly destroy closed sessions in the current call stack. Because upon
      // returning the filter chain configs will be destroyed, and no longer safe to be accessed.
      // If any filters access those configs during destruction, it'll be use-after-free
      DeleteSessions();
    }
  }
}

void EnvoyQuicDispatcher::updateListenerConfig(Network::ListenerConfig& new_listener_config) {
  listener_config_ = &new_listener_config;
}

} // namespace Quic
} // namespace Envoy
