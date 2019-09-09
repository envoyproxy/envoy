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
class EnvoyQuicCryptoServerStreamHelper : public quic::QuicCryptoServerStream::Helper {
public:
  ~EnvoyQuicCryptoServerStreamHelper() override = default;

  // quic::QuicCryptoServerStream::Helper
  quic::QuicConnectionId
  GenerateConnectionIdForReject(quic::QuicTransportVersion /*version*/,
                                quic::QuicConnectionId /*connection_id*/) const override {
    // TODO(danzh): create reject connection id based on given connection_id.
    return quic::QuicUtils::CreateRandomConnectionId();
  }

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
                      quic::QuicVersionManager* version_manager,
                      std::unique_ptr<quic::QuicConnectionHelperInterface> helper,
                      std::unique_ptr<quic::QuicAlarmFactory> alarm_factory,
                      uint8_t expected_server_connection_id_length,
                      Server::ConnectionHandlerImpl& connection_handler,
                      Network::ListenerConfig& listener_config,
                      Server::ListenerStats& listener_stats);

  void OnConnectionClosed(quic::QuicConnectionId connection_id, quic::QuicErrorCode error,
                          const std::string& error_details,
                          quic::ConnectionCloseSource source) override;

protected:
  quic::QuicSession* CreateQuicSession(quic::QuicConnectionId server_connection_id,
                                       const quic::QuicSocketAddress& peer_address,
                                       quic::QuicStringPiece alpn,
                                       const quic::ParsedQuicVersion& version) override;

private:
  // TODO(danzh): initialize from Envoy config.
  quic::QuicConfig quic_config_;
  Server::ConnectionHandlerImpl& connection_handler_;
  Network::ListenerConfig& listener_config_;
  Server::ListenerStats& listener_stats_;
};

} // namespace Quic
} // namespace Envoy
