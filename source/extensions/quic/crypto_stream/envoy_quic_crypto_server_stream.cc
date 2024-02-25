#include "source/extensions/quic/crypto_stream/envoy_quic_crypto_server_stream.h"

#include "envoy_tls_server_handshaker.h"
#include "quiche/quic/core/tls_server_handshaker.h"

namespace Envoy {
namespace Quic {

std::unique_ptr<quic::QuicCryptoServerStreamBase>
EnvoyQuicCryptoServerStreamFactoryImpl::createEnvoyQuicCryptoServerStream(
    const quic::QuicCryptoServerConfig* crypto_config,
    quic::QuicCompressedCertsCache* compressed_certs_cache, quic::QuicSession* session,
    quic::QuicCryptoServerStreamBase::Helper* helper,
    OptRef<const Network::DownstreamTransportSocketFactory> transport_socket_factory,
    Envoy::Event::Dispatcher& dispatcher) {
  if (session->connection()->version().handshake_protocol == quic::PROTOCOL_TLS1_3) {
    return std::unique_ptr<EnvoyTlsServerHandshaker>(
        new EnvoyTlsServerHandshaker(session, crypto_config, transport_socket_factory, dispatcher));
  }
  return quic::CreateCryptoServerStream(crypto_config, compressed_certs_cache, session, helper);
}

REGISTER_FACTORY(EnvoyQuicCryptoServerStreamFactoryImpl,
                 EnvoyQuicCryptoServerStreamFactoryInterface);

} // namespace Quic
} // namespace Envoy
