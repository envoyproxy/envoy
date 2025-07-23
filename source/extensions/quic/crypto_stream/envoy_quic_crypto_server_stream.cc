#include "source/extensions/quic/crypto_stream/envoy_quic_crypto_server_stream.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/utility.h"
#include "source/common/quic/envoy_quic_crypto_server_stream_with_fingerprinting.h"

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

  if (session == nullptr) {
    ENVOY_LOG(error, "QUIC crypto stream factory called with null session");
    return nullptr;
  }

  if (crypto_config == nullptr) {
    ENVOY_LOG(error, "QUIC crypto stream factory called with null crypto config");
    return nullptr;
  }

  // Check if fingerprinting is enabled in the configuration.
  if (quic_config_.has_value()) {
    const bool ja3_enabled =
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(quic_config_.value(), enable_ja3_fingerprinting, false);
    const bool ja4_enabled =
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(quic_config_.value(), enable_ja4_fingerprinting, false);

    if (ja3_enabled || ja4_enabled) {
      // For now, we only support TLS 1.3 for fingerprinting.
      if (session->connection()->version().handshake_protocol != quic::PROTOCOL_TLS1_3) {
        ENVOY_LOG(debug, "QUIC crypto stream factory: non-TLS1.3 connection, using default stream");
        // Fall back to the default crypto stream for non-TLS1.3 connections.
        return quic::CreateCryptoServerStream(crypto_config, compressed_certs_cache, session,
                                              helper);
      }

      ENVOY_LOG(info, "QUIC fingerprinting enabled (JA3: {}, JA4: {})", ja3_enabled, ja4_enabled);

      TRY_ASSERT_MAIN_THREAD {
        // Pass the QUIC config to the crypto stream, which will handle fingerprint config creation.
        return std::make_unique<EnvoyQuicCryptoServerStreamWithFingerprinting>(
            session, *crypto_config, quic_config_.value());
      }
      END_TRY
      CATCH(const std::exception& e, {
        ENVOY_LOG(error, "Failed to create QUIC crypto stream with fingerprinting: {}", e.what());
        // Fall back to default stream on error.
        return quic::CreateCryptoServerStream(crypto_config, compressed_certs_cache, session,
                                              helper);
      });
    } else {
      ENVOY_LOG(debug, "QUIC fingerprinting disabled in configuration");
    }
  } else {
    ENVOY_LOG(debug, "QUIC configuration not available, using default crypto stream");
  }

  // Fall back to the default crypto stream if fingerprinting is not enabled.
  ENVOY_LOG(debug, "QUIC crypto stream factory: creating default stream");
  return quic::CreateCryptoServerStream(crypto_config, compressed_certs_cache, session, helper);
}

void EnvoyQuicCryptoServerStreamFactoryImpl::setQuicConfig(
    const envoy::config::listener::v3::QuicProtocolOptions& quic_config) {
  quic_config_ = quic_config;
  ENVOY_LOG(debug, "QUIC crypto stream factory: configuration set (JA3: {}, JA4: {})",
            PROTOBUF_GET_WRAPPED_OR_DEFAULT(quic_config, enable_ja3_fingerprinting, false),
            PROTOBUF_GET_WRAPPED_OR_DEFAULT(quic_config, enable_ja4_fingerprinting, false));
}

REGISTER_FACTORY(EnvoyQuicCryptoServerStreamFactoryImpl,
                 EnvoyQuicCryptoServerStreamFactoryInterface);

} // namespace Quic
} // namespace Envoy
