#pragma once

#include "source/common/common/empty_string.h"
#include "source/common/common/hex.h"
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
//
// QUICHE configures BoringSSL with the `CRYPTO_BUFFER`-based X509 method, so the base class
// certificate accessors abort on the QUIC `SSL` object. The peer certificate accessors are
// therefore overridden here to decode the chain from `SSL_get0_peer_certificates` on demand.
// Raw-PEM and local certificate accessors return empty.
class QuicSslConnectionInfo : public Extensions::TransportSockets::Tls::ConnectionInfoImplBase {
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

  uint16_t ciphersuiteId() const override {
    auto* crypto_stream = session_.GetCryptoStream();
    ASSERT(crypto_stream != nullptr);
    return crypto_stream->CiphersuiteId();
  }

  absl::string_view ciphersuiteString() const override {
    auto* crypto_stream = session_.GetCryptoStream();
    ASSERT(crypto_stream != nullptr);
    return crypto_stream->CiphersuiteString();
  }

  uint16_t tlsGroupId() const override {
    auto* crypto_stream = session_.GetCryptoStream();
    ASSERT(crypto_stream != nullptr);
    return crypto_stream->TlsGroupId();
  }

  absl::string_view tlsGroupString() const override {
    auto* crypto_stream = session_.GetCryptoStream();
    ASSERT(crypto_stream != nullptr);
    return crypto_stream->TlsGroupString();
  }

  const std::string& tlsVersion() const override {
    static const std::string version("TLSv1.3");
    return version;
  }

  const std::string& alpn() const override {
    if (!alpn_.has_value()) {
      auto* crypto_stream = session_.GetCryptoStream();
      ASSERT(crypto_stream != nullptr);
      alpn_ = std::string(crypto_stream->Alpn());
    }
    return *alpn_;
  }

  const std::string& sni() const override {
    if (!sni_.has_value()) {
      auto* crypto_stream = session_.GetCryptoStream();
      ASSERT(crypto_stream != nullptr);
      sni_ = std::string(crypto_stream->Sni());
    }
    return *sni_;
  }

  // Peer certificate accessors. QUICHE stores the peer chain as `CRYPTO_BUFFER`s, so these decode
  // certificates on demand instead of using the base class X509 accessors, which abort on the QUIC
  // `SSL` object.
  const std::string& sha256PeerCertificateDigest() const override {
    return getCachedCertificateValue<std::string>(&cached_sha256_digest_, [this]() -> std::string {
      const CRYPTO_BUFFER* cert = getPeerLeafCertificate();
      if (cert == nullptr) {
        return EMPTY_STRING;
      }
      std::vector<uint8_t> hash(SHA256_DIGEST_LENGTH);
      SHA256(CRYPTO_BUFFER_data(cert), CRYPTO_BUFFER_len(cert), hash.data());
      return Hex::encode(hash);
    });
  }

  const std::string& sha1PeerCertificateDigest() const override {
    return getCachedCertificateValue<std::string>(&cached_sha1_digest_, [this]() -> std::string {
      const CRYPTO_BUFFER* cert = getPeerLeafCertificate();
      if (cert == nullptr) {
        return EMPTY_STRING;
      }
      std::vector<uint8_t> hash(SHA_DIGEST_LENGTH);
      SHA1(CRYPTO_BUFFER_data(cert), CRYPTO_BUFFER_len(cert), hash.data());
      return Hex::encode(hash);
    });
  }

  absl::Span<const std::string> sha256PeerCertificateChainDigests() const override {
    return getCachedCertificateValue<std::vector<std::string>>(
        &cached_sha256_chain_digests_, [this]() -> std::vector<std::string> {
          return computeChainDigests(SHA256_DIGEST_LENGTH, &SHA256);
        });
  }

  absl::Span<const std::string> sha1PeerCertificateChainDigests() const override {
    return getCachedCertificateValue<std::vector<std::string>>(
        &cached_sha1_chain_digests_, [this]() -> std::vector<std::string> {
          return computeChainDigests(SHA_DIGEST_LENGTH, &SHA1);
        });
  }

  const std::string& serialNumberPeerCertificate() const override {
    return getCachedCertificateValue<std::string>(&cached_serial_number_, [this]() -> std::string {
      bssl::UniquePtr<X509> cert = getPeerLeafCertificateAsX509();
      if (cert == nullptr) {
        return EMPTY_STRING;
      }
      return Extensions::TransportSockets::Tls::Utility::getSerialNumberFromCertificate(*cert);
    });
  }

  absl::Span<const std::string> serialNumbersPeerCertificates() const override {
    return getCachedCertificateValue<std::vector<std::string>>(
        &cached_serial_numbers_, [this]() -> std::vector<std::string> {
          const STACK_OF(CRYPTO_BUFFER)* cert_stack = peerCertificateStack();
          if (cert_stack == nullptr) {
            return {};
          }
          std::vector<std::string> serials;
          const int cert_count = sk_CRYPTO_BUFFER_num(cert_stack);
          serials.reserve(cert_count);
          for (int i = 0; i < cert_count; ++i) {
            bssl::UniquePtr<X509> cert =
                decodeCryptoBufferAsX509(sk_CRYPTO_BUFFER_value(cert_stack, i));
            if (cert == nullptr) {
              continue;
            }
            serials.push_back(
                Extensions::TransportSockets::Tls::Utility::getSerialNumberFromCertificate(*cert));
          }
          return serials;
        });
  }

  const std::string& issuerPeerCertificate() const override {
    return getCachedCertificateValue<std::string>(&cached_issuer_, [this]() -> std::string {
      bssl::UniquePtr<X509> cert = getPeerLeafCertificateAsX509();
      if (cert == nullptr) {
        return EMPTY_STRING;
      }
      return Extensions::TransportSockets::Tls::Utility::getIssuerFromCertificate(*cert);
    });
  }

  const std::string& subjectPeerCertificate() const override {
    return getCachedCertificateValue<std::string>(&cached_subject_, [this]() -> std::string {
      bssl::UniquePtr<X509> cert = getPeerLeafCertificateAsX509();
      if (cert == nullptr) {
        return EMPTY_STRING;
      }
      return Extensions::TransportSockets::Tls::Utility::getSubjectFromCertificate(*cert);
    });
  }

  Ssl::ParsedX509NameOptConstRef parsedSubjectPeerCertificate() const override {
    const auto& parsed_name = getCachedCertificateValue<Ssl::ParsedX509NamePtr>(
        &cached_parsed_subject_, [this]() -> Ssl::ParsedX509NamePtr {
          bssl::UniquePtr<X509> cert = getPeerLeafCertificateAsX509();
          if (cert == nullptr) {
            return nullptr;
          }
          return Extensions::TransportSockets::Tls::Utility::parseSubjectFromCertificate(*cert);
        });
    if (parsed_name) {
      return {*parsed_name};
    }
    return std::nullopt;
  }

  // Raw PEM accessors are not implemented for QUIC. The URL-encoded variants below cover the XFCC
  // use case.
  const std::string& pemEncodedPeerCertificate() const override { return EMPTY_STRING; }
  absl::Span<const std::string> pemEncodedPeerCertificateChain() const override { return {}; }

  const std::string& urlEncodedPemEncodedPeerCertificate() const override {
    return getCachedCertificateValue<std::string>(&cached_pem_cert_, [this]() -> std::string {
      bssl::UniquePtr<X509> cert = getPeerLeafCertificateAsX509();
      if (cert == nullptr) {
        return EMPTY_STRING;
      }
      bssl::UniquePtr<BIO> buf(BIO_new(BIO_s_mem()));
      if (buf == nullptr || PEM_write_bio_X509(buf.get(), cert.get()) != 1) {
        return EMPTY_STRING;
      }
      const uint8_t* output;
      size_t length;
      if (BIO_mem_contents(buf.get(), &output, &length) != 1) {
        return EMPTY_STRING;
      }
      return Envoy::Http::Utility::PercentEncoding::urlEncode(
          absl::string_view(reinterpret_cast<const char*>(output), length));
    });
  }

  const std::string& urlEncodedPemEncodedPeerCertificateChain() const override {
    return getCachedCertificateValue<std::string>(&cached_pem_chain_, [this]() -> std::string {
      const STACK_OF(CRYPTO_BUFFER)* cert_stack = peerCertificateStack();
      if (cert_stack == nullptr) {
        return EMPTY_STRING;
      }
      std::string result;
      const int cert_count = sk_CRYPTO_BUFFER_num(cert_stack);
      for (int i = 0; i < cert_count; ++i) {
        bssl::UniquePtr<X509> cert =
            decodeCryptoBufferAsX509(sk_CRYPTO_BUFFER_value(cert_stack, i));
        if (cert == nullptr) {
          continue;
        }
        bssl::UniquePtr<BIO> buf(BIO_new(BIO_s_mem()));
        if (buf == nullptr || PEM_write_bio_X509(buf.get(), cert.get()) != 1) {
          continue;
        }
        const uint8_t* output;
        size_t length;
        if (BIO_mem_contents(buf.get(), &output, &length) != 1) {
          continue;
        }
        absl::StrAppend(&result, Envoy::Http::Utility::PercentEncoding::urlEncode(absl::string_view(
                                     reinterpret_cast<const char*>(output), length)));
      }
      return result;
    });
  }

  bool peerCertificateSanMatches(const Ssl::SanMatcher& matcher) const override {
    bssl::UniquePtr<X509> cert = getPeerLeafCertificateAsX509();
    if (cert == nullptr) {
      return false;
    }
    bssl::UniquePtr<GENERAL_NAMES> sans(static_cast<GENERAL_NAMES*>(
        X509_get_ext_d2i(cert.get(), NID_subject_alt_name, nullptr, nullptr)));
    if (sans == nullptr) {
      return false;
    }
    for (const GENERAL_NAME* san : sans.get()) {
      if (matcher.match(san)) {
        return true;
      }
    }
    return false;
  }

  absl::Span<const std::string> uriSanPeerCertificate() const override {
    return getCachedCertificateValue<std::vector<std::string>>(
        &cached_uri_sans_, [this]() -> std::vector<std::string> {
          bssl::UniquePtr<X509> cert = getPeerLeafCertificateAsX509();
          if (cert == nullptr) {
            return {};
          }
          return Extensions::TransportSockets::Tls::Utility::getSubjectAltNames(*cert, GEN_URI);
        });
  }

  absl::Span<const std::string> dnsSansPeerCertificate() const override {
    return getCachedCertificateValue<std::vector<std::string>>(
        &cached_dns_sans_, [this]() -> std::vector<std::string> {
          bssl::UniquePtr<X509> cert = getPeerLeafCertificateAsX509();
          if (cert == nullptr) {
            return {};
          }
          return Extensions::TransportSockets::Tls::Utility::getSubjectAltNames(*cert, GEN_DNS);
        });
  }

  absl::Span<const std::string> ipSansPeerCertificate() const override {
    return getCachedCertificateValue<std::vector<std::string>>(
        &cached_ip_sans_, [this]() -> std::vector<std::string> {
          bssl::UniquePtr<X509> cert = getPeerLeafCertificateAsX509();
          if (cert == nullptr) {
            return {};
          }
          return Extensions::TransportSockets::Tls::Utility::getSubjectAltNames(*cert, GEN_IPADD);
        });
  }

  absl::Span<const std::string> emailSansPeerCertificate() const override {
    return getCachedCertificateValue<std::vector<std::string>>(
        &cached_email_sans_, [this]() -> std::vector<std::string> {
          bssl::UniquePtr<X509> cert = getPeerLeafCertificateAsX509();
          if (cert == nullptr) {
            return {};
          }
          return Extensions::TransportSockets::Tls::Utility::getSubjectAltNames(*cert, GEN_EMAIL);
        });
  }

  absl::Span<const std::string> othernameSansPeerCertificate() const override {
    return getCachedCertificateValue<std::vector<std::string>>(
        &cached_othername_sans_, [this]() -> std::vector<std::string> {
          bssl::UniquePtr<X509> cert = getPeerLeafCertificateAsX509();
          if (cert == nullptr) {
            return {};
          }
          return Extensions::TransportSockets::Tls::Utility::getSubjectAltNames(*cert,
                                                                                GEN_OTHERNAME);
        });
  }

  absl::Span<const std::string> oidsPeerCertificate() const override {
    return getCachedCertificateValue<std::vector<std::string>>(
        &cached_oids_, [this]() -> std::vector<std::string> {
          bssl::UniquePtr<X509> cert = getPeerLeafCertificateAsX509();
          if (cert == nullptr) {
            return {};
          }
          return Extensions::TransportSockets::Tls::Utility::getCertificateExtensionOids(*cert);
        });
  }

  std::optional<SystemTime> validFromPeerCertificate() const override {
    return getCachedCertificateValue<std::optional<SystemTime>>(
        &cached_valid_from_, [this]() -> std::optional<SystemTime> {
          bssl::UniquePtr<X509> cert = getPeerLeafCertificateAsX509();
          if (cert == nullptr) {
            return std::nullopt;
          }
          return Extensions::TransportSockets::Tls::Utility::getValidFrom(*cert);
        });
  }

  std::optional<SystemTime> expirationPeerCertificate() const override {
    return getCachedCertificateValue<std::optional<SystemTime>>(
        &cached_expiration_, [this]() -> std::optional<SystemTime> {
          bssl::UniquePtr<X509> cert = getPeerLeafCertificateAsX509();
          if (cert == nullptr) {
            return std::nullopt;
          }
          return Extensions::TransportSockets::Tls::Utility::getExpirationTime(*cert);
        });
  }

  // QUIC SSL object doesn't cache local certs after the handshake.
  // TODO(danzh) cache these fields during cert chain retrieval.
  const std::string& subjectLocalCertificate() const override { return EMPTY_STRING; }
  absl::Span<const std::string> uriSanLocalCertificate() const override { return {}; }
  absl::Span<const std::string> dnsSansLocalCertificate() const override { return {}; }
  absl::Span<const std::string> ipSansLocalCertificate() const override { return {}; }
  absl::Span<const std::string> emailSansLocalCertificate() const override { return {}; }
  absl::Span<const std::string> othernameSansLocalCertificate() const override { return {}; }
  absl::Span<const std::string> oidsLocalCertificate() const override { return {}; }

  void onCertValidated() { cert_validated_ = true; };

private:
  // Caches the result of `extractor`. An empty cached value is treated as "not yet populated" so a
  // value materialized before the handshake completes is recomputed on the next call.
  template <typename T, typename Fn>
  const T& getCachedCertificateValue(std::unique_ptr<T>* cache, Fn&& extractor) const {
    if (*cache && !isCachedValueEmpty(**cache)) {
      return **cache;
    }
    *cache = std::make_unique<T>(extractor());
    return **cache;
  }

  static bool isCachedValueEmpty(const std::string& v) { return v.empty(); }
  template <typename U> static bool isCachedValueEmpty(const std::vector<U>& v) {
    return v.empty();
  }
  template <typename U> static bool isCachedValueEmpty(const std::optional<U>& v) {
    return !v.has_value();
  }
  static bool isCachedValueEmpty(const Ssl::ParsedX509NamePtr& v) { return v == nullptr; }

  // Returns the peer certificate stack from the SSL connection, or nullptr if unavailable.
  const STACK_OF(CRYPTO_BUFFER)* peerCertificateStack() const {
    SSL* ssl_conn = ssl();
    if (ssl_conn == nullptr) {
      return nullptr;
    }
    const STACK_OF(CRYPTO_BUFFER)* cert_stack = SSL_get0_peer_certificates(ssl_conn);
    if (cert_stack == nullptr || sk_CRYPTO_BUFFER_num(cert_stack) == 0) {
      return nullptr;
    }
    return cert_stack;
  }

  // Returns the leaf peer certificate as a non-owning `CRYPTO_BUFFER`, or nullptr if unavailable.
  const CRYPTO_BUFFER* getPeerLeafCertificate() const {
    const STACK_OF(CRYPTO_BUFFER)* cert_stack = peerCertificateStack();
    if (cert_stack == nullptr) {
      return nullptr;
    }
    return sk_CRYPTO_BUFFER_value(cert_stack, 0);
  }

  // Returns the leaf peer certificate as an owned X509, or nullptr if unavailable.
  bssl::UniquePtr<X509> getPeerLeafCertificateAsX509() const {
    return decodeCryptoBufferAsX509(getPeerLeafCertificate());
  }

  static bssl::UniquePtr<X509> decodeCryptoBufferAsX509(const CRYPTO_BUFFER* cert) {
    if (cert == nullptr) {
      return nullptr;
    }
    const uint8_t* data = CRYPTO_BUFFER_data(cert);
    return bssl::UniquePtr<X509>(d2i_X509(nullptr, &data, CRYPTO_BUFFER_len(cert)));
  }

  // Computes a hex-encoded digest for each certificate in the peer chain.
  std::vector<std::string> computeChainDigests(size_t digest_length,
                                               uint8_t* (*digest_fn)(const uint8_t*, size_t,
                                                                     uint8_t*)) const {
    const STACK_OF(CRYPTO_BUFFER)* cert_stack = peerCertificateStack();
    if (cert_stack == nullptr) {
      return {};
    }
    std::vector<std::string> digests;
    const int cert_count = sk_CRYPTO_BUFFER_num(cert_stack);
    digests.reserve(cert_count);
    for (int i = 0; i < cert_count; ++i) {
      const CRYPTO_BUFFER* cert = sk_CRYPTO_BUFFER_value(cert_stack, i);
      if (cert == nullptr) {
        continue;
      }
      std::vector<uint8_t> hash(digest_length);
      digest_fn(CRYPTO_BUFFER_data(cert), CRYPTO_BUFFER_len(cert), hash.data());
      digests.push_back(Hex::encode(hash));
    }
    return digests;
  }

  quic::QuicSession& session_;
  bool cert_validated_{false};
  mutable std::optional<std::string> alpn_;
  mutable std::optional<std::string> sni_;

  // Cached peer certificate fields. Populated lazily on first access.
  mutable std::unique_ptr<std::string> cached_sha256_digest_;
  mutable std::unique_ptr<std::string> cached_sha1_digest_;
  mutable std::unique_ptr<std::string> cached_subject_;
  mutable std::unique_ptr<std::string> cached_pem_cert_;
  mutable std::unique_ptr<std::string> cached_pem_chain_;
  mutable std::unique_ptr<std::vector<std::string>> cached_uri_sans_;
  mutable std::unique_ptr<std::vector<std::string>> cached_dns_sans_;
  mutable std::unique_ptr<std::string> cached_serial_number_;
  mutable std::unique_ptr<std::string> cached_issuer_;
  mutable std::unique_ptr<std::optional<SystemTime>> cached_valid_from_;
  mutable std::unique_ptr<std::optional<SystemTime>> cached_expiration_;
  mutable std::unique_ptr<Ssl::ParsedX509NamePtr> cached_parsed_subject_;
  mutable std::unique_ptr<std::vector<std::string>> cached_sha256_chain_digests_;
  mutable std::unique_ptr<std::vector<std::string>> cached_sha1_chain_digests_;
  mutable std::unique_ptr<std::vector<std::string>> cached_serial_numbers_;
  mutable std::unique_ptr<std::vector<std::string>> cached_ip_sans_;
  mutable std::unique_ptr<std::vector<std::string>> cached_email_sans_;
  mutable std::unique_ptr<std::vector<std::string>> cached_othername_sans_;
  mutable std::unique_ptr<std::vector<std::string>> cached_oids_;
};

} // namespace Quic
} // namespace Envoy
