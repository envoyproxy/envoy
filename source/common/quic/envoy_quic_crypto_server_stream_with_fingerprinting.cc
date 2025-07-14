#include "source/common/quic/envoy_quic_crypto_server_stream_with_fingerprinting.h"

#include "source/common/protobuf/utility.h"
#include "source/common/quic/envoy_quic_server_session.h"
#include "source/common/quic/quic_filter_manager_connection_impl.h"

namespace Envoy {
namespace Quic {

EnvoyQuicCryptoServerStreamWithFingerprinting::EnvoyQuicCryptoServerStreamWithFingerprinting(
    quic::QuicSession* session, const quic::QuicCryptoServerConfig& crypto_config,
    const envoy::config::listener::v3::QuicProtocolOptions& quic_config)
    : quic::TlsServerHandshaker(session, &crypto_config), quic_config_(quic_config),
      envoy_session_(dynamic_cast<EnvoyQuicServerSession*>(session)),
      fingerprint_config_created_(false) {

  if (envoy_session_ == nullptr) {
    ENVOY_LOG(error, "QUIC crypto stream initialized with invalid session type");
    return;
  }

  ENVOY_LOG(debug, "QUIC crypto stream initialized with fingerprinting configuration");
}

ssl_select_cert_result_t EnvoyQuicCryptoServerStreamWithFingerprinting::EarlySelectCertCallback(
    const SSL_CLIENT_HELLO* client_hello) {
  // Process fingerprinting first if enabled.
  if (client_hello != nullptr) {
    // Create fingerprint inspector config if needed.
    if (!fingerprint_config_created_) {
      if (!createFingerprintInspectorConfig()) {
        ENVOY_LOG(warn, "Failed to create fingerprint inspector configuration");
      }
      fingerprint_config_created_ = true;
    }

    // Process fingerprinting if inspector is available.
    if (fingerprint_inspector_ != nullptr) {
      processClientHelloForFingerprinting(client_hello);
    } else {
      ENVOY_LOG(debug, "QUIC fingerprint inspector not available");
    }
  } else {
    ENVOY_LOG(debug, "QUIC EarlySelectCertCallback called with null ClientHello");
  }

  // Call the parent implementation for normal certificate selection.
  return quic::TlsServerHandshaker::EarlySelectCertCallback(client_hello);
}

void EnvoyQuicCryptoServerStreamWithFingerprinting::processClientHelloForFingerprinting(
    const SSL_CLIENT_HELLO* client_hello) {
  ENVOY_LOG(debug, "Processing ClientHello for QUIC fingerprinting");

  std::string ja3_hash;
  std::string ja4_hash;

  // Extract fingerprints using the inspector.
  const bool success = fingerprint_inspector_->processClientHello(client_hello, ja3_hash, ja4_hash);

  if (success) {
    ENVOY_LOG(debug, "QUIC fingerprints extracted successfully (JA3: {}, JA4: {})",
              !ja3_hash.empty() ? "present" : "empty", !ja4_hash.empty() ? "present" : "empty");

    // Get the socket's connection info setter and store the fingerprints.
    if (envoy_session_ != nullptr) {
      auto* quic_connection_impl = dynamic_cast<QuicFilterManagerConnectionImpl*>(envoy_session_);
      if (quic_connection_impl != nullptr) {
        // Get non-const reference to connection info setter.
        auto& connection_info_setter = quic_connection_impl->connectionInfoSetter();

        if (!ja3_hash.empty()) {
          connection_info_setter.setJA3Hash(ja3_hash);
          ENVOY_LOG(debug, "QUIC ``JA3`` fingerprint set: {}", ja3_hash);
        }

        if (!ja4_hash.empty()) {
          connection_info_setter.setJA4Hash(ja4_hash);
          ENVOY_LOG(debug, "QUIC ``JA4`` fingerprint set: {}", ja4_hash);
        }
      } else {
        ENVOY_LOG(error, "QUIC filter manager connection not available for fingerprint storage");
      }
    } else {
      ENVOY_LOG(error, "QUIC session not available for fingerprint storage");
    }
  } else {
    ENVOY_LOG(warn, "QUIC fingerprint extraction failed");
  }
}

bool EnvoyQuicCryptoServerStreamWithFingerprinting::createFingerprintInspectorConfig() {
  // Check if either ``JA3`` or ``JA4`` fingerprinting is enabled.
  const bool ja3_enabled =
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(quic_config_, enable_ja3_fingerprinting, false);
  const bool ja4_enabled =
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(quic_config_, enable_ja4_fingerprinting, false);

  if (!ja3_enabled && !ja4_enabled) {
    ENVOY_LOG(debug, "QUIC fingerprinting disabled in configuration");
    return false;
  }

  // Get the stats scope from the Envoy session.
  if (envoy_session_ == nullptr) {
    ENVOY_LOG(error, "QUIC session not available for fingerprint stats");
    return false;
  }

  // Cast to QuicFilterManagerConnectionImpl to access stats scope.
  auto* quic_connection_impl = dynamic_cast<QuicFilterManagerConnectionImpl*>(envoy_session_);
  if (quic_connection_impl == nullptr) {
    ENVOY_LOG(error, "QUIC filter manager connection not available for fingerprint stats");
    return false;
  }

  try {
    // Get the stats scope.
    Stats::Scope& stats_scope = quic_connection_impl->statsScope();

    // Create the fingerprint inspector configuration.
    fingerprint_config_ =
        std::make_shared<QuicFingerprintInspectorConfig>(stats_scope, quic_config_);

    // Create the fingerprint inspector.
    fingerprint_inspector_ = std::make_unique<QuicFingerprintInspector>(fingerprint_config_);

    ENVOY_LOG(debug, "QUIC fingerprint inspector created (JA3: {}, JA4: {})", ja3_enabled,
              ja4_enabled);
    return true;
  } catch (const std::exception& e) {
    ENVOY_LOG(error, "Failed to create QUIC fingerprint inspector: {}", e.what());
    return false;
  }
}

} // namespace Quic
} // namespace Envoy
