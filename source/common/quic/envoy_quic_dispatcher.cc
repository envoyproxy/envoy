#include "source/common/quic/envoy_quic_dispatcher.h"

#include <openssl/crypto.h>

#include <functional>
#include <list>

#include "envoy/common/optref.h"

#include "source/common/common/safe_memcpy.h"
#include "source/common/http/utility.h"
#include "source/common/quic/envoy_quic_server_connection.h"
#include "source/common/quic/envoy_quic_utils.h"

namespace Envoy {
namespace Quic {

EnvoyQuicDispatcher::EnvoyQuicDispatcher(
    const quic::QuicCryptoServerConfig* crypto_config, const quic::QuicConfig& quic_config,
    quic::QuicVersionManager* version_manager,
    std::unique_ptr<quic::QuicConnectionHelperInterface> helper,
    std::unique_ptr<quic::QuicAlarmFactory> alarm_factory,
    uint8_t expected_server_connection_id_length, Network::ConnectionHandler& connection_handler,
    Network::ListenerConfig& listener_config, Server::ListenerStats& listener_stats,
    Server::PerHandlerListenerStats& per_worker_stats, Event::Dispatcher& dispatcher,
    Network::Socket& listen_socket, QuicStatNames& quic_stat_names,
    EnvoyQuicCryptoServerStreamFactoryInterface& crypto_server_stream_factory)
    : quic::QuicDispatcher(&quic_config, crypto_config, version_manager, std::move(helper),
                           std::make_unique<EnvoyQuicCryptoServerStreamHelper>(),
                           std::move(alarm_factory), expected_server_connection_id_length),
      connection_handler_(connection_handler), listener_config_(&listener_config),
      listener_stats_(listener_stats), per_worker_stats_(per_worker_stats), dispatcher_(dispatcher),
      listen_socket_(listen_socket), quic_stat_names_(quic_stat_names),
      crypto_server_stream_factory_(crypto_server_stream_factory) {
  // Set send buffer twice of max flow control window to ensure that stream send
  // buffer always takes all the data.
  // The max amount of data buffered is the per-stream high watermark + the max
  // flow control window of upstream. The per-stream high watermark should be
  // smaller than max flow control window to make sure upper stream can be flow
  // control blocked early enough not to send more than the threshold allows.
  // TODO(#8826) Ideally we should use the negotiated value from upstream which is not accessible
  // for now. 512MB is way to large, but the actual bytes buffered should be bound by the negotiated
  // upstream flow control window.
  SetQuicFlag(
      FLAGS_quic_buffered_data_threshold,
      2 * ::Envoy::Http2::Utility::OptionsLimits::DEFAULT_INITIAL_STREAM_WINDOW_SIZE); // 512MB
}

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

std::unique_ptr<quic::QuicSession> EnvoyQuicDispatcher::CreateQuicSession(
    quic::QuicConnectionId server_connection_id, const quic::QuicSocketAddress& self_address,
    const quic::QuicSocketAddress& peer_address, absl::string_view /*alpn*/,
    const quic::ParsedQuicVersion& version, const quic::ParsedClientHello& parsed_chlo) {
  quic::QuicConfig quic_config = config();
  // TODO(danzh) use passed-in ALPN instead of hard-coded h3 after proof source interfaces takes in
  // ALPN.
  Network::ConnectionSocketPtr connection_socket = createServerConnectionSocket(
      listen_socket_.ioHandle(), self_address, peer_address, std::string(parsed_chlo.sni), "h3");
  const Network::FilterChain* filter_chain =
      listener_config_->filterChainManager().findFilterChain(*connection_socket);

  auto quic_connection = std::make_unique<EnvoyQuicServerConnection>(
      server_connection_id, self_address, peer_address, *helper(), *alarm_factory(), writer(),
      /*owns_writer=*/false, quic::ParsedQuicVersionVector{version}, std::move(connection_socket));
  auto quic_session = std::make_unique<EnvoyQuicServerSession>(
      quic_config, quic::ParsedQuicVersionVector{version}, std::move(quic_connection), this,
      session_helper(), crypto_config(), compressed_certs_cache(), dispatcher_,
      listener_config_->perConnectionBufferLimitBytes(), quic_stat_names_,
      listener_config_->listenerScope(), crypto_server_stream_factory_);
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
  }
  quic_session->Initialize();
  connection_handler_.incNumConnections();
  listener_stats_.downstream_cx_active_.inc();
  listener_stats_.downstream_cx_total_.inc();
  per_worker_stats_.downstream_cx_active_.inc();
  per_worker_stats_.downstream_cx_total_.inc();
  return quic_session;
}

quic::QuicConnectionId EnvoyQuicDispatcher::ReplaceLongServerConnectionId(
    const quic::ParsedQuicVersion& version, const quic::QuicConnectionId& server_connection_id,
    uint8_t expected_server_connection_id_length) const {
  quic::QuicConnectionId new_connection_id = quic::QuicDispatcher::ReplaceLongServerConnectionId(
      version, server_connection_id, expected_server_connection_id_length);
  adjustNewConnectionIdForRoutine(new_connection_id, server_connection_id);
  return new_connection_id;
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
  }
}

void EnvoyQuicDispatcher::updateListenerConfig(Network::ListenerConfig& new_listener_config) {
  listener_config_ = &new_listener_config;
}

} // namespace Quic
} // namespace Envoy
