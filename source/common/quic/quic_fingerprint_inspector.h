#pragma once

#include <memory>
#include <string>

#include "envoy/config/listener/v3/quic_config.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/listener/tls_inspector/ja4_fingerprint.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Quic {

/**
 * All stats for the QUIC fingerprint inspector. @see stats_macros.h
 */
#define ALL_QUIC_FINGERPRINT_INSPECTOR_STATS(COUNTER)                                              \
  COUNTER(ja3_fingerprint_created)                                                                 \
  COUNTER(ja4_fingerprint_created)                                                                 \
  COUNTER(client_hello_processed)                                                                  \
  COUNTER(fingerprint_errors)

/**
 * Definition of all stats for the QUIC fingerprint inspector. @see stats_macros.h
 */
struct QuicFingerprintInspectorStats {
  ALL_QUIC_FINGERPRINT_INSPECTOR_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for QUIC fingerprint inspector.
 */
class QuicFingerprintInspectorConfig {
public:
  QuicFingerprintInspectorConfig(Stats::Scope& scope,
                                 const envoy::config::listener::v3::QuicProtocolOptions& config);

  const QuicFingerprintInspectorStats& stats() const { return stats_; }
  bool enableJA3Fingerprinting() const { return enable_ja3_fingerprinting_; }
  bool enableJA4Fingerprinting() const { return enable_ja4_fingerprinting_; }

private:
  QuicFingerprintInspectorStats stats_;
  const bool enable_ja3_fingerprinting_;
  const bool enable_ja4_fingerprinting_;
};

using QuicFingerprintInspectorConfigSharedPtr = std::shared_ptr<QuicFingerprintInspectorConfig>;

/**
 * QUIC fingerprint inspector that extracts ``JA3`` and ``JA4`` fingerprints from QUIC ClientHello
 * messages. This class provides similar functionality to the TLS inspector but specifically for
 * QUIC connections.
 */
class QuicFingerprintInspector : Logger::Loggable<Logger::Id::quic> {
public:
  QuicFingerprintInspector(QuicFingerprintInspectorConfigSharedPtr config);

  /**
   * Process a QUIC ClientHello message and extract ``JA3``/``JA4`` fingerprints.
   * @param ssl_client_hello the SSL ClientHello message from the QUIC handshake
   * @param ja3_hash output parameter for ``JA3`` hash (if enabled)
   * @param ja4_hash output parameter for ``JA4`` hash (if enabled)
   * @return true if processing was successful, false otherwise
   */
  bool processClientHello(const SSL_CLIENT_HELLO* ssl_client_hello, std::string& ja3_hash,
                          std::string& ja4_hash);

private:
  /**
   * Creates a ``JA3`` fingerprint from the ClientHello message.
   * @param ssl_client_hello the SSL ClientHello message
   * @param ja3_hash output parameter for the ``JA3`` hash
   */
  void createJA3Hash(const SSL_CLIENT_HELLO* ssl_client_hello, std::string& ja3_hash);

  /**
   * Creates a ``JA4`` fingerprint from the ClientHello message.
   * @param ssl_client_hello the SSL ClientHello message
   * @param ja4_hash output parameter for the ``JA4`` hash
   */
  void createJA4Hash(const SSL_CLIENT_HELLO* ssl_client_hello, std::string& ja4_hash);

  QuicFingerprintInspectorConfigSharedPtr config_;
};

} // namespace Quic
} // namespace Envoy
