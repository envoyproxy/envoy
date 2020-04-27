#pragma once

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#pragma GCC diagnostic ignored "-Wtype-limits"

#include "quiche/quic/core/quic_dispatcher.h"
#include "quiche/quic/core/quic_utils.h"

#pragma GCC diagnostic pop

#include <string>

#include "envoy/network/listener.h"
#include "server/connection_handler_impl.h"

namespace Envoy {
namespace Quic {

// Envoy specific provider of server connection id and decision maker of
// accepting new connection or not.
class EnvoyQuicCryptoServerStreamHelper : public quic::QuicCryptoServerStreamBase::Helper {
public:
  ~EnvoyQuicCryptoServerStreamHelper() override = default;

  // quic::QuicCryptoServerStream::Helper
  bool CanAcceptClientHello(const quic::CryptoHandshakeMessage& /*message*/,
                            const quic::QuicSocketAddress& /*client_address*/,
                            const quic::QuicSocketAddress& /*peer_address*/,
                            const quic::QuicSocketAddress& /*self_address*/,
                            std::string* /*error_details*/) const override {
    // TODO(danzh): decide to accept or not based on information from given handshake message, i.e.
    // user agent and SNI.
    return true;
  }
};

class EnvoyQuicDispatcher : public quic::QuicDispatcher {
public:
  EnvoyQuicDispatcher(const quic::QuicCryptoServerConfig* crypto_config,
                      const quic::QuicConfig& quic_config,
                      quic::QuicVersionManager* version_manager,
                      std::unique_ptr<quic::QuicConnectionHelperInterface> helper,
                      std::unique_ptr<quic::QuicAlarmFactory> alarm_factory,
                      uint8_t expected_server_connection_id_length,
                      Network::ConnectionHandler& connection_handler,
                      Network::ListenerConfig& listener_config,
                      Server::ListenerStats& listener_stats,
                      Server::PerHandlerListenerStats& per_worker_stats,
                      Event::Dispatcher& dispatcher, Network::Socket& listen_socket);

  void OnConnectionClosed(quic::QuicConnectionId connection_id, quic::QuicErrorCode error,
                          const std::string& error_details,
                          quic::ConnectionCloseSource source) override;

  quic::QuicConnectionId
  GenerateNewServerConnectionId(quic::ParsedQuicVersion /*version*/,
                                quic::QuicConnectionId /*connection_id*/) const override {
    // TODO(danzh): create reject connection id based on given connection_id.
    return quic::QuicUtils::CreateRandomConnectionId();
  }

protected:
  std::unique_ptr<quic::QuicSession>
  CreateQuicSession(quic::QuicConnectionId server_connection_id,
                    const quic::QuicSocketAddress& peer_address, quiche::QuicheStringPiece alpn,
                    const quic::ParsedQuicVersion& version) override;

private:
  Network::ConnectionHandler& connection_handler_;
  Network::ListenerConfig& listener_config_;
  Server::ListenerStats& listener_stats_;
  Server::PerHandlerListenerStats& per_worker_stats_;
  Event::Dispatcher& dispatcher_;
  Network::Socket& listen_socket_;
};

} // namespace Quic
} // namespace Envoy
