#pragma once

#include "envoy/extensions/quic/v3/crypto_stream.pb.h"
#include "envoy/registry/registry.h"

#include "common/quic/envoy_quic_crypto_stream_factory.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicCryptoServerStreamFactoryImpl : public EnvoyQuicCryptoServerStreamFactoryInterface {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::quic::v3::CryptoServerStreamConfig>();
  }
  std::string name() const override { return "quic.quiche_crypto_server_stream"; }
  std::unique_ptr<quic::QuicCryptoServerStreamBase>
  createEnvoyQuicCryptoServerStream(const quic::QuicCryptoServerConfig* crypto_config,
                                    quic::QuicCompressedCertsCache* compressed_certs_cache,
                                    quic::QuicSession* session,
                                    quic::QuicCryptoServerStreamBase::Helper* helper) override;
};

DECLARE_FACTORY(EnvoyQuicCryptoServerStreamFactoryImpl);

} // namespace Quic
} // namespace Envoy
