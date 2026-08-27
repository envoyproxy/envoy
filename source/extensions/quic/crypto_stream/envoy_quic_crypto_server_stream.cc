#include "source/extensions/quic/crypto_stream/envoy_quic_crypto_server_stream.h"

#include "source/common/quic/envoy_tls_server_handshaker.h"
#include "source/common/quic/quic_server_transport_socket_factory.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/tls/server_context_impl.h"

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

  const bool ticket_support =
      Runtime::runtimeFeatureEnabled("envoy.reloadable_features.quic_session_ticket_support");
  const bool keylog_support =
      Runtime::runtimeFeatureEnabled("envoy.restart_features.quic_keylog_support");
  if (!transport_socket_factory.has_value() || (!ticket_support && !keylog_support)) {
    return quic::CreateCryptoServerStream(crypto_config, compressed_certs_cache, session, helper);
  }

  auto& factory = static_cast<const QuicServerTransportSocketFactory&>(*transport_socket_factory);
  Ssl::ServerContextSharedPtr pinned_ssl_ctx = factory.sslCtx();
  bool disable_resumption = false;
  if (ticket_support) {
    const auto ticket_config = factory.getSessionTicketConfig();
    // Also check the pinned context for keys: the factory is shared across
    // workers and the config snapshot may reflect an SDS update before
    // ssl_ctx_ is swapped on the main thread. Checking the same instance the
    // handshaker pins guards against that window. The factory always creates
    // ServerContextImpl, so the downcast is safe for non-null contexts.
    auto* server_ctx =
        static_cast<Extensions::TransportSockets::Tls::ServerContextImpl*>(pinned_ssl_ctx.get());
    disable_resumption = !ticket_config.has_keys || ticket_config.disable_stateless_resumption ||
                         ticket_config.handles_session_resumption || server_ctx == nullptr ||
                         !server_ctx->hasSessionTicketKeys();
  }

  return std::make_unique<EnvoyTlsServerHandshaker>(session, crypto_config,
                                                    std::move(pinned_ssl_ctx), disable_resumption);
}

REGISTER_FACTORY(EnvoyQuicCryptoServerStreamFactoryImpl,
                 EnvoyQuicCryptoServerStreamFactoryInterface);

} // namespace Quic
} // namespace Envoy
