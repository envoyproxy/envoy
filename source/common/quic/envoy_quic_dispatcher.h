#pragma once

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#pragma GCC diagnostic ignored "-Wtype-limits"
#endif

#include "quiche/quic/core/quic_dispatcher.h"
#include "quiche/quic/core/quic_utils.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <string>

#include "envoy/network/listener.h"
#include "source/server/connection_handler_impl.h"
#include "source/server/active_listener_base.h"
#include "source/common/quic/envoy_quic_crypto_stream_factory.h"
#include "source/common/quic/quic_stat_names.h"

namespace Envoy {
namespace Quic {

// Dummy implementation only used by Google Quic.
class EnvoyQuicCryptoServerStreamHelper : public quic::QuicCryptoServerStreamBase::Helper {
public:
  // quic::QuicCryptoServerStream::Helper
  bool CanAcceptClientHello(const quic::CryptoHandshakeMessage& /*message*/,
                            const quic::QuicSocketAddress& /*client_address*/,
                            const quic::QuicSocketAddress& /*peer_address*/,
                            const quic::QuicSocketAddress& /*self_address*/,
                            std::string* /*error_details*/) const override {
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
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
      EnvoyQuicCryptoServerStreamFactoryInterface& crypto_server_stream_factory);

  void OnConnectionClosed(quic::QuicConnectionId connection_id, quic::QuicErrorCode error,
                          const std::string& error_details,
                          quic::ConnectionCloseSource source) override;

protected:
  // quic::QuicDispatcher
  std::unique_ptr<quic::QuicSession> CreateQuicSession(quic::QuicConnectionId server_connection_id,
                                                       const quic::QuicSocketAddress& self_address,
                                                       const quic::QuicSocketAddress& peer_address,
                                                       absl::string_view alpn,
                                                       const quic::ParsedQuicVersion& version,
                                                       absl::string_view sni) override;
  // Overridden to restore the first 4 bytes of the connection ID because our BPF filter only looks
  // at the first 4 bytes. This ensures that the replacement routes to the same quic dispatcher.
  quic::QuicConnectionId
  ReplaceLongServerConnectionId(const quic::ParsedQuicVersion& version,
                                const quic::QuicConnectionId& server_connection_id,
                                uint8_t expected_server_connection_id_length) const override;

private:
  Network::ConnectionHandler& connection_handler_;
  Network::ListenerConfig& listener_config_;
  Server::ListenerStats& listener_stats_;
  Server::PerHandlerListenerStats& per_worker_stats_;
  Event::Dispatcher& dispatcher_;
  Network::Socket& listen_socket_;
  QuicStatNames& quic_stat_names_;
  EnvoyQuicCryptoServerStreamFactoryInterface& crypto_server_stream_factory_;
};

} // namespace Quic
} // namespace Envoy
