#include "source/common/quic/envoy_quic_dispatcher.h"

#include <openssl/crypto.h>

#include <functional>
#include <list>

#include "envoy/common/optref.h"

#include "source/common/common/safe_memcpy.h"
#include "source/common/network/listener_filter_manager_impl_base.h"
#include "source/common/quic/envoy_quic_server_connection.h"
#include "source/common/quic/envoy_quic_utils.h"

namespace Envoy {
namespace Quic {

namespace {

QuicDispatcherStats generateStats(Stats::Scope& store) {
  return {QUIC_DISPATCHER_STATS(POOL_COUNTER_PREFIX(store, "quic.dispatcher"))};
}

class GenericListenerFilter : public Network::QuicListenerFilter {
public:
  GenericListenerFilter(const Network::ListenerFilterMatcherSharedPtr& matcher,
                        Network::QuicListenerFilterPtr listener_filter)
      : listener_filter_(std::move(listener_filter)), matcher_(std::move(matcher)) {}

  // Network::QuicListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override {
    if (isDisabled(cb)) {
      return Network::FilterStatus::Continue;
    }
    return listener_filter_->onAccept(cb);
  }
  bool isCompatibleWithServerPreferredAddress(
      const quic::QuicSocketAddress& server_preferred_address) const override {
    return listener_filter_->isCompatibleWithServerPreferredAddress(server_preferred_address);
  }
  Network::FilterStatus onPeerAddressChanged(const quic::QuicSocketAddress& new_address,
                                             Network::Connection& connection) override {
    return listener_filter_->onPeerAddressChanged(new_address, connection);
  }

private:
  /**
   * Check if this filter filter should be disabled on the incoming socket.
   * @param cb the callbacks the filter instance can use to communicate with the filter chain.
   **/
  bool isDisabled(Network::ListenerFilterCallbacks& cb) {
    if (matcher_ == nullptr) {
      return false;
    } else {
      return matcher_->matches(cb);
    }
  }

  const Network::QuicListenerFilterPtr listener_filter_;
  const Network::ListenerFilterMatcherSharedPtr matcher_;
};

} // namespace

// An object created on the stack during QUIC connection creation to apply listener filters, if
// there is any, within the call stack.
class QuicListenerFilterManagerImpl : public Network::QuicListenerFilterManager,
                                      public Network::ListenerFilterCallbacks {
public:
  QuicListenerFilterManagerImpl(Event::Dispatcher& dispatcher, Network::ConnectionSocket& socket,
                                StreamInfo::StreamInfo& stream_info)
      : socket_(socket), stream_info_(stream_info), dispatcher_(dispatcher) {}

  // Network::ListenerFilterCallbacks
  Network::ConnectionSocket& socket() override { return socket_; }
  Event::Dispatcher& dispatcher() override { return dispatcher_; }
  void continueFilterChain(bool /*success*/) override { IS_ENVOY_BUG("Should not be used."); }
  void setDynamicMetadata(const std::string& name, const ProtobufWkt::Struct& value) override {
    stream_info_.setDynamicMetadata(name, value);
  }
  envoy::config::core::v3::Metadata& dynamicMetadata() override {
    return stream_info_.dynamicMetadata();
  };
  const envoy::config::core::v3::Metadata& dynamicMetadata() const override {
    return stream_info_.dynamicMetadata();
  };
  StreamInfo::FilterState& filterState() override { return *stream_info_.filterState().get(); }

  // Network::QuicListenerFilterManager
  void addAcceptFilter(const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
                       Network::QuicListenerFilterPtr&& filter) override {
    accept_filters_.emplace_back(
        std::make_unique<GenericListenerFilter>(listener_filter_matcher, std::move(filter)));
  }
  bool shouldAdvertiseServerPreferredAddress(
      const quic::QuicSocketAddress& server_preferred_address) const override {
    for (auto iter = accept_filters_.begin(); iter != accept_filters_.end(); iter++) {
      if (!(*iter)->isCompatibleWithServerPreferredAddress(server_preferred_address)) {
        return false;
      }
    }
    return true;
  }
  void onPeerAddressChanged(const quic::QuicSocketAddress& new_address,
                            Network::Connection& connection) override {
    for (auto iter = accept_filters_.begin(); iter != accept_filters_.end(); iter++) {
      Network::FilterStatus status = (*iter)->onPeerAddressChanged(new_address, connection);
      if (status == Network::FilterStatus::StopIteration ||
          connection.state() == Network::Connection::State::Closed) {
        return;
      }
    }
  }

  void startFilterChain() {
    for (auto iter = accept_filters_.begin(); iter != accept_filters_.end(); iter++) {
      Network::FilterStatus status = (*iter)->onAccept(*this);
      if (status == Network::FilterStatus::StopIteration || !socket().ioHandle().isOpen()) {
        break;
      }
    }
  }

private:
  Network::ConnectionSocket& socket_;
  StreamInfo::StreamInfo& stream_info_;
  Event::Dispatcher& dispatcher_;
  std::list<Network::QuicListenerFilterPtr> accept_filters_;
};

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
    quic::ConnectionIdGeneratorInterface& generator)
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
          POOL_COUNTER_PREFIX(listener_config.listenerScope(), "quic.connection"))}) {}

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
    const quic::ParsedQuicVersion& version, const quic::ParsedClientHello& parsed_chlo) {
  quic::QuicConfig quic_config = config();
  // TODO(danzh) use passed-in ALPN instead of hard-coded h3 after proof source interfaces takes in
  // ALPN.
  Network::ConnectionSocketPtr connection_socket = createServerConnectionSocket(
      listen_socket_.ioHandle(), self_address, peer_address, std::string(parsed_chlo.sni), "h3");
  auto stream_info = std::make_unique<StreamInfo::StreamInfoImpl>(
      dispatcher_.timeSource(), connection_socket->connectionInfoProviderSharedPtr());

  QuicListenerFilterManagerImpl listener_filter_manager(dispatcher_, *connection_socket,
                                                        *stream_info);
  const bool success =
      listener_config_->filterChainFactory().createQuicListenerFilterChain(listener_filter_manager);
  const Network::FilterChain* filter_chain = nullptr;
  if (success) {
    // Quic listener filters are not supposed to pause the filter chain iteration. So this call
    // should finish iterating through all filters.
    listener_filter_manager.startFilterChain();
    // If any listener filter closed the socket, do not get a network filter chain. Thus early fail
    // the connection.
    if (connection_socket->ioHandle().isOpen()) {
      for (auto address_family : {quiche::IpAddressFamily::IP_V4, quiche::IpAddressFamily::IP_V6}) {
        absl::optional<quic::QuicSocketAddress> address =
            quic_config.GetPreferredAddressToSend(address_family);
        if (address.has_value() && address->IsInitialized() &&
            !listener_filter_manager.shouldAdvertiseServerPreferredAddress(address.value())) {
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
      connection_id_generator());
  auto quic_session = std::make_unique<EnvoyQuicServerSession>(
      quic_config, quic::ParsedQuicVersionVector{version}, std::move(quic_connection), this,
      session_helper(), crypto_config(), compressed_certs_cache(), dispatcher_,
      listener_config_->perConnectionBufferLimitBytes(), quic_stat_names_,
      listener_config_->listenerScope(), crypto_server_stream_factory_, std::move(stream_info),
      connection_stats_);
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
