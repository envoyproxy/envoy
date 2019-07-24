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

#include "server/connection_handler_impl.h"
#include "extensions/quic_listeners/quiche/active_quic_listener.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicCryptoServerStreamHelper : public quic::QuicCryptoServerStream::Helper {
public:
  ~EnvoyQuicCryptoServerStreamHelper() override {}

  quic::QuicConnectionId
  GenerateConnectionIdForReject(quic::QuicTransportVersion /*version*/,
                                quic::QuicConnectionId /*connection_id*/) const override {
    return quic::QuicUtils::CreateRandomConnectionId();
  }

  bool CanAcceptClientHello(const quic::CryptoHandshakeMessage& /*message*/,
                            const quic::QuicSocketAddress& /*client_address*/,
                            const quic::QuicSocketAddress& /*peer_address*/,
                            const quic::QuicSocketAddress& /*self_address*/,
                            std::string* /*error_details*/) const override {
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
                      ActiveQuicListener& listener);

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
  ActiveQuicListener& listener_;
};

} // namespace Quic
} // namespace Envoy
