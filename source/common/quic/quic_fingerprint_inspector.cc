#include "source/common/quic/quic_fingerprint_inspector.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/stats/scope.h"

#include "source/common/common/assert.h"
#include "source/common/common/hex.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/ascii.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "openssl/md5.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Quic {

QuicFingerprintInspectorConfig::QuicFingerprintInspectorConfig(
    Stats::Scope& scope, const envoy::config::listener::v3::QuicProtocolOptions& config)
    : stats_{ALL_QUIC_FINGERPRINT_INSPECTOR_STATS(
          POOL_COUNTER_PREFIX(scope, "quic_fingerprint_inspector."))},
      enable_ja3_fingerprinting_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, enable_ja3_fingerprinting, false)),
      enable_ja4_fingerprinting_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, enable_ja4_fingerprinting, false)) {}

QuicFingerprintInspector::QuicFingerprintInspector(QuicFingerprintInspectorConfigSharedPtr config)
    : config_(config) {}

bool QuicFingerprintInspector::processClientHello(const SSL_CLIENT_HELLO* ssl_client_hello,
                                                  std::string& ja3_hash, std::string& ja4_hash) {
  // Always clear output parameters first.
  ja3_hash.clear();
  ja4_hash.clear();

  if (ssl_client_hello == nullptr) {
    ENVOY_LOG(debug, "QUIC fingerprint inspector: SSL_CLIENT_HELLO is null");
    config_->stats().fingerprint_errors_.inc();
    return false;
  }

  TRY_ASSERT_MAIN_THREAD {
    config_->stats().client_hello_processed_.inc();

    // Process ``JA3`` fingerprint if enabled.
    if (config_->enableJA3Fingerprinting()) {
      createJA3Hash(ssl_client_hello, ja3_hash);
      if (!ja3_hash.empty()) {
        config_->stats().ja3_fingerprint_created_.inc();
        ENVOY_LOG(trace, "QUIC fingerprint inspector: ``JA3`` hash created: {}", ja3_hash);
      }
    }

    // Process ``JA4`` fingerprint if enabled.
    if (config_->enableJA4Fingerprinting()) {
      createJA4Hash(ssl_client_hello, ja4_hash);
      if (!ja4_hash.empty()) {
        config_->stats().ja4_fingerprint_created_.inc();
        ENVOY_LOG(trace, "QUIC fingerprint inspector: ``JA4`` hash created: {}", ja4_hash);
      }
    }

    return true;
  }
  END_TRY
  CATCH(const std::exception& e, {
    ENVOY_LOG(debug, "QUIC fingerprint inspector: Exception during processing: {}", e.what());
    config_->stats().fingerprint_errors_.inc();
    // Clear output parameters on exception as well.
    ja3_hash.clear();
    ja4_hash.clear();
    return false;
  });
}

void QuicFingerprintInspector::createJA3Hash(const SSL_CLIENT_HELLO* ssl_client_hello,
                                             std::string& ja3_hash) {
  // Reuse the existing ``JA3`` logic from TLS inspector.
  std::string fingerprint;
  const uint16_t client_version = ssl_client_hello->version;
  absl::StrAppendFormat(&fingerprint, "%d,", client_version);

  // Write cipher suites.
  CBS cipher_suites;
  CBS_init(&cipher_suites, ssl_client_hello->cipher_suites, ssl_client_hello->cipher_suites_len);

  bool write_cipher = true;
  bool first = true;
  while (write_cipher && CBS_len(&cipher_suites) > 0) {
    uint16_t id;
    write_cipher = CBS_get_u16(&cipher_suites, &id);
    if (write_cipher &&
        Extensions::ListenerFilters::TlsInspector::JA4Fingerprinter::isNotGrease(id)) {
      if (!first) {
        absl::StrAppend(&fingerprint, "-");
      }
      absl::StrAppendFormat(&fingerprint, "%d", id);
      first = false;
    }
  }
  absl::StrAppend(&fingerprint, ",");

  // Write extensions.
  CBS extensions;
  CBS_init(&extensions, ssl_client_hello->extensions, ssl_client_hello->extensions_len);

  bool write_extension = true;
  first = true;
  while (write_extension && CBS_len(&extensions) > 0) {
    uint16_t id;
    CBS extension;

    write_extension =
        (CBS_get_u16(&extensions, &id) && CBS_get_u16_length_prefixed(&extensions, &extension));
    if (write_extension &&
        Extensions::ListenerFilters::TlsInspector::JA4Fingerprinter::isNotGrease(id)) {
      if (!first) {
        absl::StrAppend(&fingerprint, "-");
      }
      absl::StrAppendFormat(&fingerprint, "%d", id);
      first = false;
    }
  }
  absl::StrAppend(&fingerprint, ",");

  // Write elliptic curves.
  const uint8_t* ec_data;
  size_t ec_len;
  if (SSL_early_callback_ctx_extension_get(ssl_client_hello, TLSEXT_TYPE_supported_groups, &ec_data,
                                           &ec_len)) {
    CBS ec;
    CBS_init(&ec, ec_data, ec_len);

    // Skip list length.
    uint16_t id;
    bool write_elliptic_curve = CBS_get_u16(&ec, &id);

    first = true;
    while (write_elliptic_curve && CBS_len(&ec) > 0) {
      write_elliptic_curve = CBS_get_u16(&ec, &id);
      if (write_elliptic_curve) {
        if (!first) {
          absl::StrAppend(&fingerprint, "-");
        }
        absl::StrAppendFormat(&fingerprint, "%d", id);
        first = false;
      }
    }
  }
  absl::StrAppend(&fingerprint, ",");

  // Write elliptic curve point formats.
  const uint8_t* ecpf_data;
  size_t ecpf_len;
  if (SSL_early_callback_ctx_extension_get(ssl_client_hello, TLSEXT_TYPE_ec_point_formats,
                                           &ecpf_data, &ecpf_len)) {
    CBS ecpf;
    CBS_init(&ecpf, ecpf_data, ecpf_len);

    // Skip list length.
    uint8_t id;
    bool write_point_format = CBS_get_u8(&ecpf, &id);

    first = true;
    while (write_point_format && CBS_len(&ecpf) > 0) {
      write_point_format = CBS_get_u8(&ecpf, &id);
      if (write_point_format) {
        if (!first) {
          absl::StrAppend(&fingerprint, "-");
        }
        absl::StrAppendFormat(&fingerprint, "%d", id);
        first = false;
      }
    }
  }

  ENVOY_LOG(trace, "QUIC fingerprint inspector: ``JA3`` fingerprint: {}", fingerprint);

  // Create MD5 hash.
  uint8_t buf[MD5_DIGEST_LENGTH];
  MD5(reinterpret_cast<const uint8_t*>(fingerprint.data()), fingerprint.size(), buf);
  ja3_hash = Envoy::Hex::encode(buf, MD5_DIGEST_LENGTH);
}

void QuicFingerprintInspector::createJA4Hash(const SSL_CLIENT_HELLO* ssl_client_hello,
                                             std::string& ja4_hash) {
  // Use the existing ``JA4`` implementation but adapt for QUIC.
  // Note: ``JA4`` for QUIC starts with 'q' instead of 't' to indicate QUIC protocol.
  std::string fingerprint =
      Extensions::ListenerFilters::TlsInspector::JA4Fingerprinter::create(ssl_client_hello);

  // Modify the protocol indicator from 't' to 'q' for QUIC.
  if (!fingerprint.empty() && fingerprint[0] == 't') {
    fingerprint[0] = 'q';
  }

  ja4_hash = fingerprint;
}

} // namespace Quic
} // namespace Envoy
