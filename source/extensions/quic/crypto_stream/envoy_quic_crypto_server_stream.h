#pragma once

#include "envoy/extensions/quic/crypto_stream/v3/crypto_stream.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/quic/envoy_quic_crypto_stream_factory.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicCryptoServerStreamFactoryImpl : public EnvoyQuicCryptoServerStreamFactoryInterface {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::quic::crypto_stream::v3::CryptoServerStreamConfig>();
  }
  std::string name() const override { return "envoy.quic.crypto_stream.server.quiche"; }
  std::unique_ptr<quic::QuicCryptoServerStreamBase> createEnvoyQuicCryptoServerStream(
      const quic::QuicCryptoServerConfig* crypto_config,
      quic::QuicCompressedCertsCache* compressed_certs_cache, quic::QuicSession* session,
      quic::QuicCryptoServerStreamBase::Helper* helper,
      OptRef<const Network::DownstreamTransportSocketFactory> transport_socket_factory,
      Envoy::Event::Dispatcher& dispatcher) override;
};

DECLARE_FACTORY(EnvoyQuicCryptoServerStreamFactoryImpl);

} // namespace Quic
} // namespace Envoy
