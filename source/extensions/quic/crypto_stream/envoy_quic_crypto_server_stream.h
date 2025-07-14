#pragma once

#include "envoy/config/listener/v3/quic_config.pb.h"
#include "envoy/extensions/quic/crypto_stream/v3/crypto_stream.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/common/logger.h"
#include "source/common/quic/envoy_quic_server_crypto_stream_factory.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicCryptoServerStreamFactoryImpl : public EnvoyQuicCryptoServerStreamFactoryInterface,
                                               Logger::Loggable<Logger::Id::quic> {
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

  // EnvoyQuicCryptoServerStreamFactoryInterface
  void setQuicConfig(const envoy::config::listener::v3::QuicProtocolOptions& quic_config) override;

  /**
   * Get the current QUIC configuration.
   * @return the QUIC configuration if set, otherwise nullopt.
   */
  const absl::optional<envoy::config::listener::v3::QuicProtocolOptions>& getQuicConfig() const {
    return quic_config_;
  }

private:
  absl::optional<envoy::config::listener::v3::QuicProtocolOptions> quic_config_;
};

DECLARE_FACTORY(EnvoyQuicCryptoServerStreamFactoryImpl);

} // namespace Quic
} // namespace Envoy
