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

  if (!transport_socket_factory.has_value()) {
    return quic::CreateCryptoServerStream(crypto_config, compressed_certs_cache, session, helper);
  }

  // QUIC listeners always use QuicServerTransportSocketFactory; the handshaker
  // tolerates a null sslCtx() (e.g. before init completes), see its ctor.
  auto& factory = static_cast<const QuicServerTransportSocketFactory&>(*transport_socket_factory);

  auto ticket_config = factory.getSessionTicketConfig();
  // The runtime flag gates only Envoy's ticket key callback. When off, we
  // leave SSL_OP_NO_TICKET alone and let QUICHE/BoringSSL issue tickets via
  // the default path. disable_stateless_resumption is operator-set and
  // always wins. has_keys/handles_session_resumption are only meaningful
  // when Envoy's callback is in play.
  const bool envoy_ticket_key_cb_enabled =
      Runtime::runtimeFeatureEnabled("envoy.reloadable_features.quic_session_ticket_support");
  const bool disable_resumption =
      ticket_config.disable_stateless_resumption ||
      (envoy_ticket_key_cb_enabled &&
       (!ticket_config.has_keys || ticket_config.handles_session_resumption));

  return std::make_unique<EnvoyTlsServerHandshaker>(
      session, crypto_config, factory.sslCtx(), disable_resumption, envoy_ticket_key_cb_enabled);
}

REGISTER_FACTORY(EnvoyQuicCryptoServerStreamFactoryImpl,
                 EnvoyQuicCryptoServerStreamFactoryInterface);

} // namespace Quic
} // namespace Envoy
