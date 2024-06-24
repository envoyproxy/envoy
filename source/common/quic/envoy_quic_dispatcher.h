#pragma once

#include <string>

#include "envoy/network/listener.h"

#include "source/common/quic/envoy_quic_connection_debug_visitor_factory_interface.h"
#include "source/common/quic/envoy_quic_server_crypto_stream_factory.h"
#include "source/common/quic/envoy_quic_server_session.h"
#include "source/common/quic/quic_stat_names.h"
#include "source/server/listener_stats.h"

#include "quiche/quic/core/quic_dispatcher.h"
#include "quiche/quic/core/quic_utils.h"

namespace Envoy {
namespace Quic {

#define QUIC_DISPATCHER_STATS(COUNTER) COUNTER(stateless_reset_packets_sent)

struct QuicDispatcherStats {
  QUIC_DISPATCHER_STATS(GENERATE_COUNTER_STRUCT)
};

// Dummy implementation only used by Google Quic.
class EnvoyQuicCryptoServerStreamHelper : public quic::QuicCryptoServerStreamBase::Helper {
public:
  // quic::QuicCryptoServerStream::Helper
  bool CanAcceptClientHello(const quic::CryptoHandshakeMessage& /*message*/,
                            const quic::QuicSocketAddress& /*client_address*/,
                            const quic::QuicSocketAddress& /*peer_address*/,
                            const quic::QuicSocketAddress& /*self_address*/,
                            std::string* /*error_details*/) const override {
    IS_ENVOY_BUG("Unexpected call to CanAcceptClientHello");
    return false;
  }
};

class EnvoyQuicTimeWaitListManager : public quic::QuicTimeWaitListManager {
public:
  EnvoyQuicTimeWaitListManager(quic::QuicPacketWriter* writer, Visitor* visitor,
                               const quic::QuicClock* clock, quic::QuicAlarmFactory* alarm_factory,
                               QuicDispatcherStats& stats);

  void SendPublicReset(const quic::QuicSocketAddress& self_address,
                       const quic::QuicSocketAddress& peer_address,
                       quic::QuicConnectionId connection_id, bool ietf_quic,
                       size_t received_packet_length,
                       std::unique_ptr<quic::QuicPerPacketContext> packet_context) override;

private:
  QuicDispatcherStats& stats_;
};

class EnvoyQuicDispatcher : public quic::QuicDispatcher {
public:
  EnvoyQuicDispatcher(
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
      EnvoyQuicConnectionDebugVisitorFactoryInterfaceOptRef&& debug_visitor_factory);

  // quic::QuicDispatcher
  void OnConnectionClosed(quic::QuicConnectionId connection_id, quic::QuicErrorCode error,
                          const std::string& error_details,
                          quic::ConnectionCloseSource source) override;
  quic::QuicTimeWaitListManager* CreateQuicTimeWaitListManager() override;

  void closeConnectionsWithFilterChain(const Network::FilterChain* filter_chain);

  void updateListenerConfig(Network::ListenerConfig& new_listener_config);

  // Similar to quic::QuicDispatcher's ProcessPacket, but returns a bool.
  // @return false if the packet failed to dispatch, true if it succeeded.
  bool processPacket(const quic::QuicSocketAddress& self_address,
                     const quic::QuicSocketAddress& peer_address,
                     const quic::QuicReceivedPacket& packet);

protected:
  // quic::QuicDispatcher
  std::unique_ptr<quic::QuicSession> CreateQuicSession(
      quic::QuicConnectionId server_connection_id, const quic::QuicSocketAddress& self_address,
      const quic::QuicSocketAddress& peer_address, absl::string_view alpn,
      const quic::ParsedQuicVersion& version, const quic::ParsedClientHello& parsed_chlo,
      quic::ConnectionIdGeneratorInterface& connection_id_generator) override;

  // quic::QuicDispatcher
  // Sets current_packet_dispatch_success_ to false for processPacket's return value,
  // then calls the parent class implementation.
  bool OnFailedToDispatchPacket(const quic::ReceivedPacketInfo& received_packet_info) override;

private:
  Network::ConnectionHandler& connection_handler_;
  Network::ListenerConfig* listener_config_{nullptr};
  Server::ListenerStats& listener_stats_;
  Server::PerHandlerListenerStats& per_worker_stats_;
  Event::Dispatcher& dispatcher_;
  Network::Socket& listen_socket_;
  QuicStatNames& quic_stat_names_;
  EnvoyQuicCryptoServerStreamFactoryInterface& crypto_server_stream_factory_;
  FilterChainToConnectionMap connections_by_filter_chain_;
  QuicDispatcherStats quic_stats_;
  QuicConnectionStats connection_stats_;
  bool current_packet_dispatch_success_;
  EnvoyQuicConnectionDebugVisitorFactoryInterfaceOptRef debug_visitor_factory_;
};

} // namespace Quic
} // namespace Envoy
