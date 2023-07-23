#include "source/extensions/quic/crypto_stream/envoy_quic_crypto_server_stream.h"

namespace Envoy {
namespace Quic {

std::unique_ptr<quic::QuicCryptoServerStreamBase>
EnvoyQuicCryptoServerStreamFactoryImpl::createEnvoyQuicCryptoServerStream(
    const quic::QuicCryptoServerConfig* crypto_config,
    quic::QuicCompressedCertsCache* compressed_certs_cache, quic::QuicSession* session,
    quic::QuicCryptoServerStreamBase::Helper* helper,
    // Though this extension doesn't use the two parameters below, they might be used by
    // downstreams. Do not remove them.
    OptRef<const Network::DownstreamTransportSocketFactory> /*transport_socket_factory*/,
    Envoy::Event::Dispatcher& /*dispatcher*/) {
  return quic::CreateCryptoServerStream(crypto_config, compressed_certs_cache, session, helper);
}

REGISTER_FACTORY(EnvoyQuicCryptoServerStreamFactoryImpl,
                 EnvoyQuicCryptoServerStreamFactoryInterface);

} // namespace Quic
} // namespace Envoy
