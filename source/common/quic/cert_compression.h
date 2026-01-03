#pragma once

#include "source/common/runtime/runtime_features.h"
#include "source/common/tls/cert_compression.h"

namespace Envoy {
namespace Quic {

// QUIC wrapper for TLS certificate compression.
class CertCompression : protected Logger::Loggable<Logger::Id::quic> {
public:
  using Algorithm = Extensions::TransportSockets::Tls::CertCompression::Algorithm;

  static void registerSslContext(SSL_CTX* ssl_ctx, Stats::Scope* scope = nullptr) {
    if (Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.tls_support_certificate_compression")) {
      Extensions::TransportSockets::Tls::CertCompression::registerAlgorithms(
          ssl_ctx, {Algorithm::Brotli, Algorithm::Zstd, Algorithm::Zlib}, scope);
    } else {
      Extensions::TransportSockets::Tls::CertCompression::registerAlgorithms(
          ssl_ctx, {Algorithm::Zlib}, scope);
    }
  }

  static constexpr int SUCCESS = Extensions::TransportSockets::Tls::CertCompression::SUCCESS;
  static constexpr int FAILURE = Extensions::TransportSockets::Tls::CertCompression::FAILURE;
};

} // namespace Quic
} // namespace Envoy
