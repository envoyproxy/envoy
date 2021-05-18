#include "test/common/quic/envoy_quic_no_handshake_crypto_server_stream.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "quiche/quic/core/crypto/null_encrypter.h"
#include "quiche/quic/core/crypto/null_decrypter.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace Envoy {
namespace Quic {

std::unique_ptr<quic::QuicCryptoServerStreamBase>
EnvoyQuicNoHandshakeCryptoServerStreamFactory::createEnvoyQuicCryptoServerStream(
    const quic::QuicCryptoServerConfig* crypto_config,
    quic::QuicCompressedCertsCache* compressed_certs_cache, quic::QuicSession* session,
    quic::QuicCryptoServerStreamBase::Helper* helper) {
  switch (session->connection()->version().handshake_protocol) {
  case quic::PROTOCOL_QUIC_CRYPTO:
    return std::make_unique<EnvoyQuicNoHandshakeCryptoServerStream>(
        crypto_config, compressed_certs_cache, session, helper);
  case quic::PROTOCOL_TLS1_3:
    return std::make_unique<EnvoyQuicNoHandshakeTlsServerStream>(session, crypto_config);
  case quic::PROTOCOL_UNSUPPORTED:
    ASSERT(false, "Unknown handshake protocol");
    return nullptr;
  }
}

REGISTER_FACTORY(EnvoyQuicNoHandshakeCryptoServerStreamFactory, EnvoyQuicCryptoServerStreamFactory);

void EnvoyQuicNoHandshakeCryptoServerStream::OnHandshakeMessage(
    const quic::CryptoHandshakeMessage& message) {
  quic::QuicConfig* config = session()->config();
  // Skip handshake.
  OverrideQuicConfigDefaults(config);

  std::string process_error_details;
  const quic::QuicErrorCode process_error =
      config->ProcessPeerHello(message, quic::CLIENT, &process_error_details);
  if (process_error != quic::QUIC_NO_ERROR) {
    session()->connection()->CloseConnection(
        process_error, process_error_details,
        quic::ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }
  ASSERT(config->negotiated());

  session()->OnConfigNegotiated();

  // Use NullEncrypter/Decrypter to make it possible to mutate payload while
  // fuzzing.
  session()->connection()->SetEncrypter(
      quic::ENCRYPTION_FORWARD_SECURE,
      std::make_unique<quic::NullEncrypter>(quic::Perspective::IS_SERVER));
  if (session()->connection()->version().KnowsWhichDecrypterToUse()) {
    session()->connection()->InstallDecrypter(
        quic::ENCRYPTION_FORWARD_SECURE,
        std::make_unique<quic::NullDecrypter>(quic::Perspective::IS_SERVER));
    session()->connection()->RemoveDecrypter(quic::ENCRYPTION_INITIAL);
  } else {
    session()->connection()->SetDecrypter(
        quic::ENCRYPTION_FORWARD_SECURE,
        std::make_unique<quic::NullDecrypter>(quic::Perspective::IS_SERVER));
  }
  set_encryption_established(true);
  set_one_rtt_keys_available(true);
  session()->SetDefaultEncryptionLevel(quic::ENCRYPTION_FORWARD_SECURE);
  session()->DiscardOldEncryptionKey(quic::ENCRYPTION_INITIAL);
  session()->connection()->OnDecryptedPacket(0, quic::ENCRYPTION_FORWARD_SECURE);
}

void EnvoyQuicNoHandshakeTlsServerStream::SetWriteSecret(quic::EncryptionLevel level,
                                                         const SSL_CIPHER* cipher,
                                                         const std::vector<uint8_t>& write_secret) {
  quic::TlsServerHandshaker::SetWriteSecret(level, cipher, write_secret);
  session()->connection()->SetEncrypter(
      level, std::make_unique<quic::NullEncrypter>(quic::Perspective::IS_SERVER));
}

bool EnvoyQuicNoHandshakeTlsServerStream::SetReadSecret(
    quic::EncryptionLevel level, const SSL_CIPHER* /*cipher*/,
    const std::vector<uint8_t>& /*read_secret*/) {
  session()->connection()->InstallDecrypter(
      level, std::make_unique<quic::NullDecrypter>(quic::Perspective::IS_SERVER));
  return true;
}

} // namespace Quic
} // namespace Envoy
