#pragma once

#include <memory>
#include <string>

#include "envoy/config/listener/v3/quic_config.pb.h"

#include "source/common/common/logger.h"
#include "source/common/quic/quic_fingerprint_inspector.h"

#include "quiche/quic/core/crypto/quic_crypto_server_config.h"
#include "quiche/quic/core/quic_crypto_server_stream_base.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/tls_server_handshaker.h"

namespace Envoy {
namespace Quic {

// Forward declaration.
class EnvoyQuicServerSession;

/**
 * A custom QUIC crypto server stream that integrates ``JA3``/``JA4`` fingerprinting.
 * This stream extends the TLS server handshaker to add certificate selection
 * callbacks that extract fingerprints from the ClientHello message.
 */
class EnvoyQuicCryptoServerStreamWithFingerprinting : public quic::TlsServerHandshaker,
                                                      Logger::Loggable<Logger::Id::quic> {
public:
  EnvoyQuicCryptoServerStreamWithFingerprinting(
      quic::QuicSession* session, const quic::QuicCryptoServerConfig& crypto_config,
      const envoy::config::listener::v3::QuicProtocolOptions& quic_config);

  ~EnvoyQuicCryptoServerStreamWithFingerprinting() override = default;

  /**
   * Get the current QUIC configuration.
   * @return the QUIC configuration reference.
   */
  const envoy::config::listener::v3::QuicProtocolOptions& getQuicConfig() const {
    return quic_config_;
  }

  /**
   * Get the fingerprint inspector configuration.
   * @return the fingerprint inspector configuration if created, otherwise nullptr.
   */
  const QuicFingerprintInspectorConfigSharedPtr& getFingerprintConfig() const {
    return fingerprint_config_;
  }

protected:
  /**
   * Override the early certificate selection callback to extract fingerprints.
   * This is called during the TLS handshake when ClientHello is received.
   * @param client_hello the SSL ClientHello message.
   * @return the result of certificate selection.
   */
  ssl_select_cert_result_t EarlySelectCertCallback(const SSL_CLIENT_HELLO* client_hello) override;

private:
  /**
   * Process the ClientHello message to extract ``JA3``/``JA4`` fingerprints.
   * @param client_hello the SSL ClientHello message.
   */
  void processClientHelloForFingerprinting(const SSL_CLIENT_HELLO* client_hello);

  /**
   * Create the fingerprint inspector configuration when needed.
   * @return true if successful, false otherwise.
   */
  bool createFingerprintInspectorConfig();

  const envoy::config::listener::v3::QuicProtocolOptions quic_config_;
  QuicFingerprintInspectorConfigSharedPtr fingerprint_config_;
  std::unique_ptr<QuicFingerprintInspector> fingerprint_inspector_;
  EnvoyQuicServerSession* envoy_session_;
  bool fingerprint_config_created_;
};

} // namespace Quic
} // namespace Envoy
