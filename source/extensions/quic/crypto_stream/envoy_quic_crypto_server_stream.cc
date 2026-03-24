#include "source/extensions/quic/crypto_stream/envoy_quic_crypto_server_stream.h"

#include <openssl/ssl.h>

#include "source/common/quic/envoy_quic_proof_source.h"
#include "source/common/quic/quic_server_transport_socket_factory.h"
#include "source/common/runtime/runtime_features.h"

#include "quiche/quic/core/tls_server_handshaker.h"

namespace Envoy {
namespace Quic {

std::unique_ptr<quic::QuicCryptoServerStreamBase>
EnvoyQuicCryptoServerStreamFactoryImpl::createEnvoyQuicCryptoServerStream(
    const quic::QuicCryptoServerConfig* crypto_config,
    quic::QuicCompressedCertsCache* compressed_certs_cache, quic::QuicSession* session,
    quic::QuicCryptoServerStreamBase::Helper* helper,
    // Though this extension doesn't use the parameters below, they might be used by
    // downstreams. Do not remove them.
    OptRef<const Network::DownstreamTransportSocketFactory> /*transport_socket_factory*/,
    Envoy::Event::Dispatcher& /*dispatcher*/, const Network::FilterChain* filter_chain) {
  auto stream =
      quic::CreateCryptoServerStream(crypto_config, compressed_certs_cache, session, helper);

  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.quic_session_ticket_support") &&
      filter_chain != nullptr && stream->GetSsl() != nullptr) {
    // Store filter chain in SSL ex_data for the per-connection ticket key callback
    // installed by EnvoyQuicProofSource::OnNewSslCtx.
    SSL_set_ex_data(stream->GetSsl(), EnvoyQuicProofSource::filterChainExDataIndex(),
                    const_cast<Network::FilterChain*>(filter_chain));

    // QUIC listeners always use QuicServerTransportSocketFactory.
    auto& factory = static_cast<const QuicServerTransportSocketFactory&>(
        filter_chain->transportSocketFactory());
    auto ticket_config = factory.getSessionTicketConfig();
    if (ticket_config.disable_stateless_resumption || !ticket_config.has_keys ||
        ticket_config.handles_session_resumption) {
      // GetSsl() returning non-null guarantees this is a TlsServerHandshaker (not the
      // legacy QuicCryptoServerStream which returns nullptr from GetSsl()).
      // DisableResumption() works here: can_disable_resumption_ is true at construction,
      // only set false in EarlySelectCertCallback which hasn't fired yet.
      static_cast<quic::TlsServerHandshaker*>(stream.get())->DisableResumption();
    }
  }

  return stream;
}

REGISTER_FACTORY(EnvoyQuicCryptoServerStreamFactoryImpl,
                 EnvoyQuicCryptoServerStreamFactoryInterface);

} // namespace Quic
} // namespace Envoy
