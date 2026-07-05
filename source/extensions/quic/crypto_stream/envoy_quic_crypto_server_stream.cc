#include "source/extensions/quic/crypto_stream/envoy_quic_crypto_server_stream.h"

#include "source/common/quic/envoy_tls_server_handshaker.h"
#include "source/common/quic/quic_server_transport_socket_factory.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Quic {

std::unique_ptr<quic::QuicCryptoServerStreamBase>
EnvoyQuicCryptoServerStreamFactoryImpl::createEnvoyQuicCryptoServerStream(
    const quic::QuicCryptoServerConfig* crypto_config,
    quic::QuicCompressedCertsCache* compressed_certs_cache, quic::QuicSession* session,
    quic::QuicCryptoServerStreamBase::Helper* helper,
    OptRef<const Network::DownstreamTransportSocketFactory> transport_socket_factory,
    // Though this extension doesn't use the dispatcher parameter, it might be used by
    // downstreams. Do not remove it.
    Envoy::Event::Dispatcher& /*dispatcher*/) {

  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.quic_session_ticket_support") ||
      !transport_socket_factory.has_value()) {
    return quic::CreateCryptoServerStream(crypto_config, compressed_certs_cache, session, helper);
  }

  // QUIC listeners always use QuicServerTransportSocketFactory. The factory's
  // ssl_ctx_ is set in the constructor and swapped (never nulled) on SDS updates.
  auto& factory = static_cast<const QuicServerTransportSocketFactory&>(*transport_socket_factory);

  auto ticket_config = factory.getSessionTicketConfig();
  bool disable_resumption = ticket_config.disable_stateless_resumption || !ticket_config.has_keys ||
                            ticket_config.handles_session_resumption;

  return std::make_unique<EnvoyTlsServerHandshaker>(session, crypto_config, factory.sslCtx(),
                                                    disable_resumption);
}

REGISTER_FACTORY(EnvoyQuicCryptoServerStreamFactoryImpl,
                 EnvoyQuicCryptoServerStreamFactoryInterface);

} // namespace Quic
} // namespace Envoy
