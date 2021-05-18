#pragma once

#include "envoy/registry/registry.h"

#include "common/quic/envoy_quic_crypto_stream_factory.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "quiche/quic/core/quic_crypto_server_stream.h"
#include "quiche/quic/core/tls_server_handshaker.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace Envoy {
namespace Quic {

class EnvoyQuicNoHandshakeCryptoServerStreamFactory : public EnvoyQuicCryptoServerStreamFactory {
public:
  EnvoyQuicNoHandshakeCryptoServerStreamFactory() : EnvoyQuicCryptoServerStreamFactory() {}

  ProtobufTypes::MessagePtr createEmptyConfigProto() override { return nullptr; }
  std::string name() const override { return "quic.no_handshake_crypto_server_stream"; }

  std::unique_ptr<quic::QuicCryptoServerStreamBase>
  createEnvoyQuicCryptoServerStream(const quic::QuicCryptoServerConfig* crypto_config,
                                    quic::QuicCompressedCertsCache* compressed_certs_cache,
                                    quic::QuicSession* session,
                                    quic::QuicCryptoServerStreamBase::Helper* helper) override;
};

DECLARE_FACTORY(EnvoyQuicNoHandshakeCryptoServerStreamFactory);

// A Google quic crypto stream which bypasses handshakes.
class EnvoyQuicNoHandshakeCryptoServerStream : public quic::QuicCryptoServerStream {
public:
  EnvoyQuicNoHandshakeCryptoServerStream(const quic::QuicCryptoServerConfig* crypto_config,
                                         quic::QuicCompressedCertsCache* compressed_certs_cache,
                                         quic::QuicSession* session,
                                         quic::QuicCryptoServerStreamBase::Helper* helper)
      : quic::QuicCryptoServerStream(crypto_config, compressed_certs_cache, session, helper) {}

  void OnHandshakeMessage(const quic::CryptoHandshakeMessage& message) override;
};

// A TLS quic crypto stream which bypasses handshakes.
class EnvoyQuicNoHandshakeTlsServerStream : public quic::TlsServerHandshaker {
public:
  EnvoyQuicNoHandshakeTlsServerStream(quic::QuicSession* session,
                                      const quic::QuicCryptoServerConfig* crypto_config)
      : quic::TlsServerHandshaker(session, crypto_config) {}

  void ProcessAdditionalTransportParameters(const quic::TransportParameters& params) override;

private:
  void SetWriteSecret(quic::EncryptionLevel level, const SSL_CIPHER* cipher,
                      const std::vector<uint8_t>& write_secret) override;
  bool SetReadSecret(quic::EncryptionLevel level, const SSL_CIPHER* cipher,
                     const std::vector<uint8_t>& read_secret) override;
};

} // namespace Quic
} // namespace Envoy
