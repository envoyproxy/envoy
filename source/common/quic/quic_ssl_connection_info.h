#pragma once

#include "source/common/common/empty_string.h"
#include "source/common/common/hex.h"
#include "source/common/common/logger.h"
#include "source/common/http/utility.h"
#include "source/common/tls/cert_validator/san_matcher.h"
#include "source/common/tls/connection_info_impl_base.h"
#include "source/common/tls/utility.h"

#include "openssl/x509v3.h"
#include "quiche/quic/core/quic_session.h"

namespace Envoy {
namespace Quic {

// A wrapper of a QUIC session to be passed around as an indicator of ssl support and to provide
// access to the SSL object in QUIC crypto stream.
class QuicSslConnectionInfo : public Extensions::TransportSockets::Tls::ConnectionInfoImplBase,
                              public Logger::Loggable<Logger::Id::quic> {
public:
  QuicSslConnectionInfo(quic::QuicSession& session) : session_(session) {}

  // Ssl::ConnectionInfo
  bool peerCertificateValidated() const override { return cert_validated_; };
  // Extensions::TransportSockets::Tls::ConnectionInfoImplBase
  SSL* ssl() const override {
    ASSERT(session_.GetCryptoStream() != nullptr);
    ASSERT(session_.GetCryptoStream()->GetSsl() != nullptr);
    return session_.GetCryptoStream()->GetSsl();
  }

  // Extensions::TransportSockets::Tls::ConnectionInfoImplBase
  // QUIC-safe certificate detection that avoids BoringSSL X.509 assertion failures
  bool peerCertificatePresented() const override {
    SSL* ssl_conn = ssl();
    if (ssl_conn == nullptr) {
      ENVOY_LOG(debug, "QuicSslConnectionInfo: No SSL connection");
      return false;
    }

    // Debug: Check SSL connection state
    int ssl_state = SSL_get_state(ssl_conn);
    ENVOY_LOG(debug, "QuicSslConnectionInfo: SSL state = {}", ssl_state);

    // Check if handshake is complete
    bool handshake_complete = SSL_is_init_finished(ssl_conn);
    ENVOY_LOG(debug, "QuicSslConnectionInfo: Handshake complete = {}", handshake_complete);

    // Check verification mode
    int verify_mode = SSL_get_verify_mode(ssl_conn);
    ENVOY_LOG(debug, "QuicSslConnectionInfo: SSL verify mode = {}", verify_mode);

    // For QUIC connections, use SSL_get0_peer_certificates (CRYPTO_BUFFER stack)
    // This is the same approach used by Quiche's TlsClientHandshaker::VerifyCert
    const STACK_OF(CRYPTO_BUFFER)* cert_stack = SSL_get0_peer_certificates(ssl_conn);
    ENVOY_LOG(debug, "QuicSslConnectionInfo: SSL_get0_peer_certificates returned {}",
              cert_stack ? "non-null" : "null");

    if (cert_stack != nullptr) {
      int cert_count = sk_CRYPTO_BUFFER_num(cert_stack);
      ENVOY_LOG(debug, "QuicSslConnectionInfo: CRYPTO_BUFFER cert count = {}", cert_count);

      if (cert_count > 0) {
        // Log certificate details using Quiche's proven pattern
        for (int i = 0; i < cert_count; i++) {
          const CRYPTO_BUFFER* cert = sk_CRYPTO_BUFFER_value(cert_stack, i);
          if (cert) {
            size_t cert_len = CRYPTO_BUFFER_len(cert);
            ENVOY_LOG(debug, "QuicSslConnectionInfo: Certificate {} length = {} bytes", i,
                      cert_len);
          }
        }

        ENVOY_LOG(debug, "QuicSslConnectionInfo: Found {} client certificates via CRYPTO_BUFFER",
                  cert_count);
        return true;
      }
    }

    // Note: Cannot safely call SSL_get_client_CA_list() on QUIC SSL objects
    // as it causes BoringSSL assertion failures. QUIC uses different certificate handling.

    // For QUIC connections, we cannot safely call X.509 functions like:
    // - SSL_get_peer_cert_chain() -> causes BoringSSL assertion failure
    // - SSL_get_peer_certificate() -> causes BoringSSL assertion failure
    // This is because QUIC SSL objects use different certificate handling

    ENVOY_LOG(debug, "QuicSslConnectionInfo: No client certificates found via QUIC-safe methods");
    return false;
  }

  // Implement QUIC-safe certificate digest extraction
  const std::string& sha256PeerCertificateDigest() const override {
    return getCachedCertificateValue<std::string>(&cached_sha256_digest_, [this]() -> std::string {
      const CRYPTO_BUFFER* cert = getPeerLeafCertificate();
      if (!cert) {
        ENVOY_LOG(debug, "QuicSslConnectionInfo: No peer certificate for SHA256 digest");
        return EMPTY_STRING;
      }

      std::vector<uint8_t> computed_hash(SHA256_DIGEST_LENGTH);
      SHA256(CRYPTO_BUFFER_data(cert), CRYPTO_BUFFER_len(cert), computed_hash.data());
      std::string digest = Hex::encode(computed_hash);
      ENVOY_LOG(debug, "QuicSslConnectionInfo: Extracted SHA256 digest: {}", digest);
      return digest;
    });
  }

  const std::string& sha1PeerCertificateDigest() const override {
    return getCachedCertificateValue<std::string>(&cached_sha1_digest_, [this]() -> std::string {
      const CRYPTO_BUFFER* cert = getPeerLeafCertificate();
      if (!cert) {
        return EMPTY_STRING;
      }

      std::vector<uint8_t> computed_hash(SHA_DIGEST_LENGTH);
      SHA1(CRYPTO_BUFFER_data(cert), CRYPTO_BUFFER_len(cert), computed_hash.data());
      return Hex::encode(computed_hash);
    });
  }

  absl::Span<const std::string> uriSanPeerCertificate() const override {
    return getCachedCertificateValue<std::vector<std::string>>(
        &cached_uri_sans_, [this]() -> std::vector<std::string> {
          X509* x509_cert = getPeerLeafCertificateAsX509();
          if (!x509_cert) {
            return {};
          }

          auto uri_sans =
              Extensions::TransportSockets::Tls::Utility::getSubjectAltNames(*x509_cert, GEN_URI);
          X509_free(x509_cert);
          ENVOY_LOG(debug, "QuicSslConnectionInfo: Extracted {} URI SANs", uri_sans.size());
          return uri_sans;
        });
  }

  const std::string& serialNumberPeerCertificate() const override {
    return getCachedCertificateValue<std::string>(&cached_serial_number_, [this]() -> std::string {
      X509* x509_cert = getPeerLeafCertificateAsX509();
      if (!x509_cert) {
        return EMPTY_STRING;
      }

      std::string serial_number =
          Extensions::TransportSockets::Tls::Utility::getSerialNumberFromCertificate(*x509_cert);
      X509_free(x509_cert);
      ENVOY_LOG(debug, "QuicSslConnectionInfo: Extracted certificate serial number: {}",
                serial_number);
      return serial_number;
    });
  }

  const std::string& issuerPeerCertificate() const override {
    return getCachedCertificateValue<std::string>(&cached_issuer_, [this]() -> std::string {
      X509* x509_cert = getPeerLeafCertificateAsX509();
      if (!x509_cert) {
        return EMPTY_STRING;
      }

      std::string issuer =
          Extensions::TransportSockets::Tls::Utility::getIssuerFromCertificate(*x509_cert);
      X509_free(x509_cert);
      ENVOY_LOG(debug, "QuicSslConnectionInfo: Extracted certificate issuer: {}", issuer);
      return issuer;
    });
  }

  const std::string& subjectPeerCertificate() const override {
    return getCachedCertificateValue<std::string>(&cached_subject_, [this]() -> std::string {
      X509* x509_cert = getPeerLeafCertificateAsX509();
      if (!x509_cert) {
        return EMPTY_STRING;
      }

      std::string subject =
          Extensions::TransportSockets::Tls::Utility::getSubjectFromCertificate(*x509_cert);
      X509_free(x509_cert);
      ENVOY_LOG(debug, "QuicSslConnectionInfo: Extracted certificate subject: {}", subject);
      return subject;
    });
  }

  Ssl::ParsedX509NameOptConstRef parsedSubjectPeerCertificate() const override {
    const auto& parsed_name = getCachedCertificateValue<Ssl::ParsedX509NamePtr>(
        &cached_parsed_subject_, [this]() -> Ssl::ParsedX509NamePtr {
          X509* x509_cert = getPeerLeafCertificateAsX509();
          if (!x509_cert) {
            return nullptr;
          }

          auto parsed_subject =
              Extensions::TransportSockets::Tls::Utility::parseSubjectFromCertificate(*x509_cert);
          X509_free(x509_cert);
          ENVOY_LOG(debug, "QuicSslConnectionInfo: Parsed certificate subject structure");
          return parsed_subject;
        });

    if (parsed_name) {
      return {*parsed_name};
    }
    return absl::nullopt;
  }

  const std::string& urlEncodedPemEncodedPeerCertificate() const override {
    return getCachedCertificateValue<std::string>(&cached_pem_cert_, [this]() -> std::string {
      X509* x509_cert = getPeerLeafCertificateAsX509();
      if (!x509_cert) {
        return EMPTY_STRING;
      }

      bssl::UniquePtr<BIO> buf(BIO_new(BIO_s_mem()));
      if (!buf || PEM_write_bio_X509(buf.get(), x509_cert) != 1) {
        X509_free(x509_cert);
        return EMPTY_STRING;
      }

      const uint8_t* output;
      size_t length;
      if (BIO_mem_contents(buf.get(), &output, &length) != 1) {
        X509_free(x509_cert);
        return EMPTY_STRING;
      }

      absl::string_view pem(reinterpret_cast<const char*>(output), length);
      std::string encoded_pem = Envoy::Http::Utility::PercentEncoding::urlEncode(pem);

      X509_free(x509_cert);
      ENVOY_LOG(debug, "QuicSslConnectionInfo: Generated URL-encoded PEM certificate ({} bytes)",
                encoded_pem.length());
      return encoded_pem;
    });
  }

  const std::string& urlEncodedPemEncodedPeerCertificateChain() const override {
    return getCachedCertificateValue<std::string>(&cached_pem_chain_, [this]() -> std::string {
      SSL* ssl_conn = ssl();
      if (!ssl_conn) {
        return EMPTY_STRING;
      }

      const STACK_OF(CRYPTO_BUFFER)* cert_stack = SSL_get0_peer_certificates(ssl_conn);
      if (!cert_stack || sk_CRYPTO_BUFFER_num(cert_stack) == 0) {
        return EMPTY_STRING;
      }

      std::string result;
      int cert_count = sk_CRYPTO_BUFFER_num(cert_stack);

      for (int i = 0; i < cert_count; i++) {
        const CRYPTO_BUFFER* cert = sk_CRYPTO_BUFFER_value(cert_stack, i);
        if (!cert)
          continue;

        const uint8_t* cert_data = CRYPTO_BUFFER_data(cert);
        size_t cert_len = CRYPTO_BUFFER_len(cert);

        X509* x509_cert = d2i_X509(nullptr, &cert_data, cert_len);
        if (!x509_cert)
          continue;

        bssl::UniquePtr<BIO> buf(BIO_new(BIO_s_mem()));
        if (buf && PEM_write_bio_X509(buf.get(), x509_cert) == 1) {
          const uint8_t* output;
          size_t length;
          if (BIO_mem_contents(buf.get(), &output, &length) == 1) {
            absl::string_view pem(reinterpret_cast<const char*>(output), length);
            absl::StrAppend(&result, Envoy::Http::Utility::PercentEncoding::urlEncode(pem));
          }
        }

        X509_free(x509_cert);
      }

      ENVOY_LOG(debug,
                "QuicSslConnectionInfo: Generated URL-encoded PEM certificate chain ({} bytes)",
                result.length());
      return result;
    });
  }

  absl::Span<const std::string> dnsSansPeerCertificate() const override {
    return getCachedCertificateValue<std::vector<std::string>>(
        &cached_dns_sans_, [this]() -> std::vector<std::string> {
          X509* x509_cert = getPeerLeafCertificateAsX509();
          if (!x509_cert) {
            return {};
          }

          auto dns_sans =
              Extensions::TransportSockets::Tls::Utility::getSubjectAltNames(*x509_cert, GEN_DNS);
          X509_free(x509_cert);
          ENVOY_LOG(debug, "QuicSslConnectionInfo: Extracted {} DNS SANs", dns_sans.size());
          return dns_sans;
        });
  }

  absl::optional<SystemTime> validFromPeerCertificate() const override {
    return getCachedCertificateValue<absl::optional<SystemTime>>(
        &cached_valid_from_, [this]() -> absl::optional<SystemTime> {
          X509* x509_cert = getPeerLeafCertificateAsX509();
          if (!x509_cert) {
            return absl::nullopt;
          }

          auto valid_from = Extensions::TransportSockets::Tls::Utility::getValidFrom(*x509_cert);
          X509_free(x509_cert);
          ENVOY_LOG(debug, "QuicSslConnectionInfo: Extracted certificate valid from time");
          return valid_from;
        });
  }

  absl::optional<SystemTime> expirationPeerCertificate() const override {
    return getCachedCertificateValue<absl::optional<SystemTime>>(
        &cached_expiration_, [this]() -> absl::optional<SystemTime> {
          X509* x509_cert = getPeerLeafCertificateAsX509();
          if (!x509_cert) {
            return absl::nullopt;
          }

          auto expiration =
              Extensions::TransportSockets::Tls::Utility::getExpirationTime(*x509_cert);
          X509_free(x509_cert);
          ENVOY_LOG(debug, "QuicSslConnectionInfo: Extracted certificate expiration time");
          return expiration;
        });
  }
  // QUIC SSL object doesn't cache local certs after the handshake.
  // TODO(danzh) cache these fields during cert chain retrieval.
  const std::string& subjectLocalCertificate() const override { return EMPTY_STRING; }
  absl::Span<const std::string> uriSanLocalCertificate() const override { return {}; }
  absl::Span<const std::string> dnsSansLocalCertificate() const override { return {}; }

  void onCertValidated() { cert_validated_ = true; };

  // Additional methods for QUIC certificate management
  void setCertificateValidated() const { cert_validated_ = true; }
  void onHandshakeComplete() const {
    // Mark handshake as complete for debugging
    ENVOY_LOG(debug, "QuicSslConnectionInfo: Handshake marked as complete");
  }

  // Missing method implementations for complete interface compliance
  absl::Span<const std::string> sha256PeerCertificateChainDigests() const override {
    return getCachedCertificateValue<std::vector<std::string>>(
        &cached_sha256_chain_digests_, [this]() -> std::vector<std::string> {
          SSL* ssl_conn = ssl();
          if (!ssl_conn) {
            return {};
          }

          const STACK_OF(CRYPTO_BUFFER)* cert_stack = SSL_get0_peer_certificates(ssl_conn);
          if (!cert_stack || sk_CRYPTO_BUFFER_num(cert_stack) == 0) {
            return {};
          }

          std::vector<std::string> digests;
          int cert_count = sk_CRYPTO_BUFFER_num(cert_stack);

          for (int i = 0; i < cert_count; i++) {
            const CRYPTO_BUFFER* cert = sk_CRYPTO_BUFFER_value(cert_stack, i);
            if (!cert)
              continue;

            std::vector<uint8_t> computed_hash(SHA256_DIGEST_LENGTH);
            SHA256(CRYPTO_BUFFER_data(cert), CRYPTO_BUFFER_len(cert), computed_hash.data());
            digests.push_back(Hex::encode(computed_hash));
          }

          ENVOY_LOG(debug, "QuicSslConnectionInfo: Generated {} SHA256 chain digests",
                    digests.size());
          return digests;
        });
  }

  absl::Span<const std::string> sha1PeerCertificateChainDigests() const override {
    return getCachedCertificateValue<std::vector<std::string>>(
        &cached_sha1_chain_digests_, [this]() -> std::vector<std::string> {
          SSL* ssl_conn = ssl();
          if (!ssl_conn) {
            return {};
          }

          const STACK_OF(CRYPTO_BUFFER)* cert_stack = SSL_get0_peer_certificates(ssl_conn);
          if (!cert_stack || sk_CRYPTO_BUFFER_num(cert_stack) == 0) {
            return {};
          }

          std::vector<std::string> digests;
          int cert_count = sk_CRYPTO_BUFFER_num(cert_stack);

          for (int i = 0; i < cert_count; i++) {
            const CRYPTO_BUFFER* cert = sk_CRYPTO_BUFFER_value(cert_stack, i);
            if (!cert)
              continue;

            std::vector<uint8_t> computed_hash(SHA_DIGEST_LENGTH);
            SHA1(CRYPTO_BUFFER_data(cert), CRYPTO_BUFFER_len(cert), computed_hash.data());
            digests.push_back(Hex::encode(computed_hash));
          }

          ENVOY_LOG(debug, "QuicSslConnectionInfo: Generated {} SHA1 chain digests",
                    digests.size());
          return digests;
        });
  }

  absl::Span<const std::string> serialNumbersPeerCertificates() const override {
    return getCachedCertificateValue<std::vector<std::string>>(
        &cached_serial_numbers_, [this]() -> std::vector<std::string> {
          SSL* ssl_conn = ssl();
          if (!ssl_conn) {
            return {};
          }

          const STACK_OF(CRYPTO_BUFFER)* cert_stack = SSL_get0_peer_certificates(ssl_conn);
          if (!cert_stack || sk_CRYPTO_BUFFER_num(cert_stack) == 0) {
            return {};
          }

          std::vector<std::string> serial_numbers;
          int cert_count = sk_CRYPTO_BUFFER_num(cert_stack);

          for (int i = 0; i < cert_count; i++) {
            const CRYPTO_BUFFER* cert = sk_CRYPTO_BUFFER_value(cert_stack, i);
            if (!cert)
              continue;

            const uint8_t* cert_data = CRYPTO_BUFFER_data(cert);
            size_t cert_len = CRYPTO_BUFFER_len(cert);

            X509* x509_cert = d2i_X509(nullptr, &cert_data, cert_len);
            if (!x509_cert)
              continue;

            std::string serial_number =
                Extensions::TransportSockets::Tls::Utility::getSerialNumberFromCertificate(
                    *x509_cert);
            serial_numbers.push_back(serial_number);
            X509_free(x509_cert);
          }

          ENVOY_LOG(debug, "QuicSslConnectionInfo: Extracted {} certificate serial numbers",
                    serial_numbers.size());
          return serial_numbers;
        });
  }

  bool peerCertificateSanMatches(const Ssl::SanMatcher& matcher) const override {
    X509* x509_cert = getPeerLeafCertificateAsX509();
    if (!x509_cert) {
      return false;
    }

    bssl::UniquePtr<GENERAL_NAMES> sans(static_cast<GENERAL_NAMES*>(
        X509_get_ext_d2i(x509_cert, NID_subject_alt_name, nullptr, nullptr)));
    X509_free(x509_cert);

    if (sans != nullptr) {
      for (const GENERAL_NAME* san : sans.get()) {
        if (matcher.match(san)) {
          return true;
        }
      }
    }

    return false;
  }

  absl::Span<const std::string> ipSansPeerCertificate() const override {
    return getCachedCertificateValue<std::vector<std::string>>(
        &cached_ip_sans_, [this]() -> std::vector<std::string> {
          X509* x509_cert = getPeerLeafCertificateAsX509();
          if (!x509_cert) {
            return {};
          }

          auto ip_sans =
              Extensions::TransportSockets::Tls::Utility::getSubjectAltNames(*x509_cert, GEN_IPADD);
          X509_free(x509_cert);
          ENVOY_LOG(debug, "QuicSslConnectionInfo: Extracted {} IP SANs", ip_sans.size());
          return ip_sans;
        });
  }

  absl::Span<const std::string> emailSansPeerCertificate() const override {
    return getCachedCertificateValue<std::vector<std::string>>(
        &cached_email_sans_, [this]() -> std::vector<std::string> {
          X509* x509_cert = getPeerLeafCertificateAsX509();
          if (!x509_cert) {
            return {};
          }

          auto email_sans =
              Extensions::TransportSockets::Tls::Utility::getSubjectAltNames(*x509_cert, GEN_EMAIL);
          X509_free(x509_cert);
          ENVOY_LOG(debug, "QuicSslConnectionInfo: Extracted {} Email SANs", email_sans.size());
          return email_sans;
        });
  }

  absl::Span<const std::string> othernameSansPeerCertificate() const override {
    return getCachedCertificateValue<std::vector<std::string>>(
        &cached_othername_sans_, [this]() -> std::vector<std::string> {
          X509* x509_cert = getPeerLeafCertificateAsX509();
          if (!x509_cert) {
            return {};
          }

          auto othername_sans = Extensions::TransportSockets::Tls::Utility::getSubjectAltNames(
              *x509_cert, GEN_OTHERNAME);
          X509_free(x509_cert);
          ENVOY_LOG(debug, "QuicSslConnectionInfo: Extracted {} OtherName SANs",
                    othername_sans.size());
          return othername_sans;
        });
  }

  absl::Span<const std::string> oidsPeerCertificate() const override {
    return getCachedCertificateValue<std::vector<std::string>>(
        &cached_oids_, [this]() -> std::vector<std::string> {
          X509* x509_cert = getPeerLeafCertificateAsX509();
          if (!x509_cert) {
            return {};
          }

          auto oids =
              Extensions::TransportSockets::Tls::Utility::getCertificateExtensionOids(*x509_cert);
          X509_free(x509_cert);
          ENVOY_LOG(debug, "QuicSslConnectionInfo: Extracted {} certificate OIDs", oids.size());
          return oids;
        });
  }

  // Local certificate methods - returning empty for QUIC as noted in TODO
  absl::Span<const std::string> ipSansLocalCertificate() const override { return {}; }
  absl::Span<const std::string> emailSansLocalCertificate() const override { return {}; }
  absl::Span<const std::string> othernameSansLocalCertificate() const override { return {}; }
  absl::Span<const std::string> oidsLocalCertificate() const override { return {}; }

  // SSL connection information methods that use QUIC-safe APIs
  const std::string& sessionId() const override {
    return getCachedCertificateValue<std::string>(&cached_session_id_, [this]() -> std::string {
      SSL* ssl_conn = ssl();
      if (!ssl_conn) {
        return EMPTY_STRING;
      }

      // For QUIC, session ID might not be meaningful, but we can safely call this
      const SSL_SESSION* session = SSL_get_session(ssl_conn);
      if (!session) {
        return EMPTY_STRING;
      }

      unsigned int session_id_length;
      const uint8_t* session_id = SSL_SESSION_get_id(session, &session_id_length);
      if (!session_id || session_id_length == 0) {
        return EMPTY_STRING;
      }

      return Hex::encode(session_id, session_id_length);
    });
  }

  uint16_t ciphersuiteId() const override {
    SSL* ssl_conn = ssl();
    if (!ssl_conn) {
      return 0xffff;
    }

    const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl_conn);
    if (cipher == nullptr) {
      return 0xffff;
    }

    return static_cast<uint16_t>(SSL_CIPHER_get_id(cipher));
  }

  std::string ciphersuiteString() const override {
    SSL* ssl_conn = ssl();
    if (!ssl_conn) {
      return {};
    }

    const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl_conn);
    if (cipher == nullptr) {
      return {};
    }

    return SSL_CIPHER_get_name(cipher);
  }

  const std::string& tlsVersion() const override {
    return getCachedCertificateValue<std::string>(&cached_tls_version_, [this]() -> std::string {
      SSL* ssl_conn = ssl();
      if (!ssl_conn) {
        return EMPTY_STRING;
      }

      return std::string(SSL_get_version(ssl_conn));
    });
  }

  const std::string& alpn() const override {
    return getCachedCertificateValue<std::string>(&cached_alpn_, [this]() -> std::string {
      SSL* ssl_conn = ssl();
      if (!ssl_conn) {
        return EMPTY_STRING;
      }

      const unsigned char* proto;
      unsigned int proto_len;
      SSL_get0_alpn_selected(ssl_conn, &proto, &proto_len);
      if (proto != nullptr) {
        return std::string(reinterpret_cast<const char*>(proto), proto_len);
      }
      return EMPTY_STRING;
    });
  }

  const std::string& sni() const override {
    return getCachedCertificateValue<std::string>(&cached_sni_, [this]() -> std::string {
      SSL* ssl_conn = ssl();
      if (!ssl_conn) {
        return EMPTY_STRING;
      }

      const char* proto = SSL_get_servername(ssl_conn, TLSEXT_NAMETYPE_host_name);
      if (proto != nullptr) {
        return std::string(proto);
      }
      return EMPTY_STRING;
    });
  }

private:
  // Helper template for caching certificate extraction results
  template <typename T>
  const T& getCachedCertificateValue(std::unique_ptr<T>* cache,
                                     std::function<T()> extractor) const {
    if (!*cache) {
      *cache = std::make_unique<T>(extractor());
    }
    return **cache;
  }

  // Helper method to get the leaf certificate from CRYPTO_BUFFER stack
  const CRYPTO_BUFFER* getPeerLeafCertificate() const {
    SSL* ssl_conn = ssl();
    if (!ssl_conn) {
      return nullptr;
    }

    const STACK_OF(CRYPTO_BUFFER)* cert_stack = SSL_get0_peer_certificates(ssl_conn);
    if (!cert_stack || sk_CRYPTO_BUFFER_num(cert_stack) == 0) {
      return nullptr;
    }

    return sk_CRYPTO_BUFFER_value(cert_stack, 0); // First certificate is the leaf
  }

  // Helper method to convert CRYPTO_BUFFER to X509 (caller must free)
  X509* getPeerLeafCertificateAsX509() const {
    const CRYPTO_BUFFER* cert = getPeerLeafCertificate();
    if (!cert) {
      return nullptr;
    }

    const uint8_t* cert_data = CRYPTO_BUFFER_data(cert);
    size_t cert_len = CRYPTO_BUFFER_len(cert);

    return d2i_X509(nullptr, &cert_data, cert_len);
  }

  quic::QuicSession& session_;
  mutable bool cert_validated_{false};

  // Cached certificate extraction results
  mutable std::unique_ptr<std::string> cached_sha256_digest_;
  mutable std::unique_ptr<std::string> cached_sha1_digest_;
  mutable std::unique_ptr<std::string> cached_subject_;
  mutable std::unique_ptr<std::string> cached_pem_cert_;
  mutable std::unique_ptr<std::string> cached_pem_chain_;
  mutable std::unique_ptr<std::vector<std::string>> cached_uri_sans_;
  mutable std::unique_ptr<std::vector<std::string>> cached_dns_sans_;
  mutable std::unique_ptr<std::string> cached_serial_number_;
  mutable std::unique_ptr<std::string> cached_issuer_;
  mutable std::unique_ptr<absl::optional<SystemTime>> cached_valid_from_;
  mutable std::unique_ptr<absl::optional<SystemTime>> cached_expiration_;
  mutable std::unique_ptr<Ssl::ParsedX509NamePtr> cached_parsed_subject_;

  // Additional cached results for missing methods
  mutable std::unique_ptr<std::vector<std::string>> cached_sha256_chain_digests_;
  mutable std::unique_ptr<std::vector<std::string>> cached_sha1_chain_digests_;
  mutable std::unique_ptr<std::vector<std::string>> cached_serial_numbers_;
  mutable std::unique_ptr<std::vector<std::string>> cached_ip_sans_;
  mutable std::unique_ptr<std::vector<std::string>> cached_email_sans_;
  mutable std::unique_ptr<std::vector<std::string>> cached_othername_sans_;
  mutable std::unique_ptr<std::vector<std::string>> cached_oids_;
  mutable std::unique_ptr<std::string> cached_session_id_;
  mutable std::unique_ptr<std::string> cached_tls_version_;
  mutable std::unique_ptr<std::string> cached_alpn_;
  mutable std::unique_ptr<std::string> cached_sni_;
};

} // namespace Quic
} // namespace Envoy
