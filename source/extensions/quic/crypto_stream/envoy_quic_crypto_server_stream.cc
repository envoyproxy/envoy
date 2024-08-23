#include "source/extensions/quic/crypto_stream/envoy_quic_crypto_server_stream.h"

#include "source/common/quic/envoy_tls_server_handshaker.h"

namespace Envoy {
namespace Quic {

std::unique_ptr<quic::QuicCryptoServerStreamBase>
EnvoyQuicCryptoServerStreamFactoryImpl::createEnvoyQuicCryptoServerStream(
    const quic::QuicCryptoServerConfig* crypto_config,
    quic::QuicCompressedCertsCache* /*compressed_certs_cache*/, quic::QuicSession* session,
    quic::QuicCryptoServerStreamBase::Helper* /*helper*/,
    OptRef<const Network::DownstreamTransportSocketFactory> transport_socket_factory,
    Envoy::Event::Dispatcher& dispatcher) {
  ASSERT(transport_socket_factory.has_value());
  return std::unique_ptr<EnvoyTlsServerHandshaker>(new EnvoyTlsServerHandshaker(
      session, crypto_config, dispatcher, transport_socket_factory.value()));
}

REGISTER_FACTORY(EnvoyQuicCryptoServerStreamFactoryImpl,
                 EnvoyQuicCryptoServerStreamFactoryInterface);

} // namespace Quic
} // namespace Envoy
