#include "source/extensions/quic/crypto_stream/envoy_quic_crypto_server_stream.h"

#include "source/common/quic/envoy_quic_proof_source.h"
#include "source/common/quic/envoy_tls_server_handshaker.h"
#include "source/common/runtime/runtime_features.h"

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
    Envoy::Event::Dispatcher& /*dispatcher*/) {
  if (!Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.quic_session_ticket_support")) {
    // Use default QUICHE TLS server handshaker when session ticket support is disabled.
    return quic::CreateCryptoServerStream(crypto_config, compressed_certs_cache, session, helper);
  }

  auto* proof_source = dynamic_cast<EnvoyQuicProofSource*>(crypto_config->proof_source());
  ASSERT(proof_source != nullptr, "Expected EnvoyQuicProofSource in crypto config");

  return std::make_unique<EnvoyTlsServerHandshaker>(session, crypto_config, proof_source);
}

REGISTER_FACTORY(EnvoyQuicCryptoServerStreamFactoryImpl,
                 EnvoyQuicCryptoServerStreamFactoryInterface);

} // namespace Quic
} // namespace Envoy
