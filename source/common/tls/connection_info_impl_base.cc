#include "source/common/tls/connection_info_impl_base.h"

#include <openssl/stack.h>

#include "source/common/common/hex.h"

#include "absl/strings/str_replace.h"
#include "openssl/err.h"
#include "openssl/safestack.h"
#include "openssl/x509v3.h"
#include "utility.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

bool ConnectionInfoImplBase::peerCertificatePresented() const {
  bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl()));
  return cert != nullptr;
}

absl::Span<const std::string> ConnectionInfoImplBase::uriSanLocalCertificate() const {
  if (!cached_uri_san_local_certificate_.empty()) {
    return cached_uri_san_local_certificate_;
  }

  // The cert object is not owned.
  X509* cert = SSL_get_certificate(ssl());
  if (!cert) {
    ASSERT(cached_uri_san_local_certificate_.empty());
    return cached_uri_san_local_certificate_;
  }
  cached_uri_san_local_certificate_ = Utility::getSubjectAltNames(*cert, GEN_URI);
  return cached_uri_san_local_certificate_;
}

absl::Span<const std::string> ConnectionInfoImplBase::dnsSansLocalCertificate() const {
  if (!cached_dns_san_local_certificate_.empty()) {
    return cached_dns_san_local_certificate_;
  }

  X509* cert = SSL_get_certificate(ssl());
  if (!cert) {
    ASSERT(cached_dns_san_local_certificate_.empty());
    return cached_dns_san_local_certificate_;
  }
  cached_dns_san_local_certificate_ = Utility::getSubjectAltNames(*cert, GEN_DNS);
  return cached_dns_san_local_certificate_;
}

absl::Span<const std::string> ConnectionInfoImplBase::ipSansLocalCertificate() const {
  if (!cached_ip_san_local_certificate_.empty()) {
    return cached_ip_san_local_certificate_;
  }

  X509* cert = SSL_get_certificate(ssl());
  if (!cert) {
    ASSERT(cached_ip_san_local_certificate_.empty());
    return cached_ip_san_local_certificate_;
  }
  cached_ip_san_local_certificate_ = Utility::getSubjectAltNames(*cert, GEN_IPADD);
  return cached_ip_san_local_certificate_;
}

const std::string& ConnectionInfoImplBase::sha256PeerCertificateDigest() const {
  if (!cached_sha_256_peer_certificate_digest_.empty()) {
    return cached_sha_256_peer_certificate_digest_;
  }
  bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl()));
  if (!cert) {
    ASSERT(cached_sha_256_peer_certificate_digest_.empty());
    return cached_sha_256_peer_certificate_digest_;
  }

  std::vector<uint8_t> computed_hash(SHA256_DIGEST_LENGTH);
  unsigned int n;
  X509_digest(cert.get(), EVP_sha256(), computed_hash.data(), &n);
  RELEASE_ASSERT(n == computed_hash.size(), "");
  cached_sha_256_peer_certificate_digest_ = Hex::encode(computed_hash);
  return cached_sha_256_peer_certificate_digest_;
}

absl::Span<const std::string> ConnectionInfoImplBase::sha256PeerCertificateChainDigests() const {
  if (!cached_sha_256_peer_certificate_digests_.empty()) {
    return cached_sha_256_peer_certificate_digests_;
  }

  STACK_OF(X509)* cert_chain = SSL_get_peer_full_cert_chain(ssl());
  if (cert_chain == nullptr) {
    ASSERT(cached_sha_256_peer_certificate_digests_.empty());
    return cached_sha_256_peer_certificate_digests_;
  }

  cached_sha_256_peer_certificate_digests_ =
      Utility::mapX509Stack(*cert_chain, [](X509& cert) -> std::string {
        std::vector<uint8_t> computed_hash(SHA256_DIGEST_LENGTH);
        unsigned int n;
        X509_digest(&cert, EVP_sha256(), computed_hash.data(), &n);
        RELEASE_ASSERT(n == computed_hash.size(), "");
        return Hex::encode(computed_hash);
      });

  return cached_sha_256_peer_certificate_digests_;
}

const std::string& ConnectionInfoImplBase::sha1PeerCertificateDigest() const {
  if (!cached_sha_1_peer_certificate_digest_.empty()) {
    return cached_sha_1_peer_certificate_digest_;
  }
  bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl()));
  if (!cert) {
    ASSERT(cached_sha_1_peer_certificate_digest_.empty());
    return cached_sha_1_peer_certificate_digest_;
  }

  std::vector<uint8_t> computed_hash(SHA_DIGEST_LENGTH);
  unsigned int n;
  X509_digest(cert.get(), EVP_sha1(), computed_hash.data(), &n);
  RELEASE_ASSERT(n == computed_hash.size(), "");
  cached_sha_1_peer_certificate_digest_ = Hex::encode(computed_hash);
  return cached_sha_1_peer_certificate_digest_;
}

absl::Span<const std::string> ConnectionInfoImplBase::sha1PeerCertificateChainDigests() const {
  if (!cached_sha_1_peer_certificate_digests_.empty()) {
    return cached_sha_1_peer_certificate_digests_;
  }

  STACK_OF(X509)* cert_chain = SSL_get_peer_full_cert_chain(ssl());
  if (cert_chain == nullptr) {
    ASSERT(cached_sha_1_peer_certificate_digests_.empty());
    return cached_sha_1_peer_certificate_digests_;
  }

  cached_sha_1_peer_certificate_digests_ =
      Utility::mapX509Stack(*cert_chain, [](X509& cert) -> std::string {
        std::vector<uint8_t> computed_hash(SHA_DIGEST_LENGTH);
        unsigned int n;
        X509_digest(&cert, EVP_sha1(), computed_hash.data(), &n);
        RELEASE_ASSERT(n == computed_hash.size(), "");
        return Hex::encode(computed_hash);
      });

  return cached_sha_1_peer_certificate_digests_;
}

const std::string& ConnectionInfoImplBase::urlEncodedPemEncodedPeerCertificate() const {
  if (!cached_url_encoded_pem_encoded_peer_certificate_.empty()) {
    return cached_url_encoded_pem_encoded_peer_certificate_;
  }
  bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl()));
  if (!cert) {
    ASSERT(cached_url_encoded_pem_encoded_peer_certificate_.empty());
    return cached_url_encoded_pem_encoded_peer_certificate_;
  }

  bssl::UniquePtr<BIO> buf(BIO_new(BIO_s_mem()));
  RELEASE_ASSERT(buf != nullptr, "");
  RELEASE_ASSERT(PEM_write_bio_X509(buf.get(), cert.get()) == 1, "");
  const uint8_t* output;
  size_t length;
  RELEASE_ASSERT(BIO_mem_contents(buf.get(), &output, &length) == 1, "");
  absl::string_view pem(reinterpret_cast<const char*>(output), length);
  cached_url_encoded_pem_encoded_peer_certificate_ = absl::StrReplaceAll(
      pem, {{"\n", "%0A"}, {" ", "%20"}, {"+", "%2B"}, {"/", "%2F"}, {"=", "%3D"}});
  return cached_url_encoded_pem_encoded_peer_certificate_;
}

const std::string& ConnectionInfoImplBase::urlEncodedPemEncodedPeerCertificateChain() const {
  if (!cached_url_encoded_pem_encoded_peer_cert_chain_.empty()) {
    return cached_url_encoded_pem_encoded_peer_cert_chain_;
  }

  STACK_OF(X509)* cert_chain = SSL_get_peer_full_cert_chain(ssl());
  if (cert_chain == nullptr) {
    ASSERT(cached_url_encoded_pem_encoded_peer_cert_chain_.empty());
    return cached_url_encoded_pem_encoded_peer_cert_chain_;
  }

  for (uint64_t i = 0; i < sk_X509_num(cert_chain); i++) {
    X509* cert = sk_X509_value(cert_chain, i);

    bssl::UniquePtr<BIO> buf(BIO_new(BIO_s_mem()));
    RELEASE_ASSERT(buf != nullptr, "");
    RELEASE_ASSERT(PEM_write_bio_X509(buf.get(), cert) == 1, "");
    const uint8_t* output;
    size_t length;
    RELEASE_ASSERT(BIO_mem_contents(buf.get(), &output, &length) == 1, "");

    absl::string_view pem(reinterpret_cast<const char*>(output), length);
    cached_url_encoded_pem_encoded_peer_cert_chain_ = absl::StrCat(
        cached_url_encoded_pem_encoded_peer_cert_chain_,
        absl::StrReplaceAll(
            pem, {{"\n", "%0A"}, {" ", "%20"}, {"+", "%2B"}, {"/", "%2F"}, {"=", "%3D"}}));
  }
  return cached_url_encoded_pem_encoded_peer_cert_chain_;
}

absl::Span<const std::string> ConnectionInfoImplBase::uriSanPeerCertificate() const {
  if (!cached_uri_san_peer_certificate_.empty()) {
    return cached_uri_san_peer_certificate_;
  }

  bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl()));
  if (!cert) {
    ASSERT(cached_uri_san_peer_certificate_.empty());
    return cached_uri_san_peer_certificate_;
  }
  cached_uri_san_peer_certificate_ = Utility::getSubjectAltNames(*cert, GEN_URI);
  return cached_uri_san_peer_certificate_;
}

absl::Span<const std::string> ConnectionInfoImplBase::dnsSansPeerCertificate() const {
  if (!cached_dns_san_peer_certificate_.empty()) {
    return cached_dns_san_peer_certificate_;
  }

  bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl()));
  if (!cert) {
    ASSERT(cached_dns_san_peer_certificate_.empty());
    return cached_dns_san_peer_certificate_;
  }
  cached_dns_san_peer_certificate_ = Utility::getSubjectAltNames(*cert, GEN_DNS);
  return cached_dns_san_peer_certificate_;
}

absl::Span<const std::string> ConnectionInfoImplBase::ipSansPeerCertificate() const {
  if (!cached_ip_san_peer_certificate_.empty()) {
    return cached_ip_san_peer_certificate_;
  }

  bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl()));
  if (!cert) {
    ASSERT(cached_ip_san_peer_certificate_.empty());
    return cached_ip_san_peer_certificate_;
  }
  cached_ip_san_peer_certificate_ = Utility::getSubjectAltNames(*cert, GEN_IPADD, true);
  return cached_ip_san_peer_certificate_;
}

absl::Span<const std::string> ConnectionInfoImplBase::oidsPeerCertificate() const {
  if (!cached_oid_peer_certificate_.empty()) {
    return cached_oid_peer_certificate_;
  }

  bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl()));
  if (!cert) {
    ASSERT(cached_oid_peer_certificate_.empty());
    return cached_oid_peer_certificate_;
  }
  cached_oid_peer_certificate_ = Utility::getCertificateExtensionOids(*cert);
  return cached_oid_peer_certificate_;
}

absl::Span<const std::string> ConnectionInfoImplBase::oidsLocalCertificate() const {
  if (!cached_oid_local_certificate_.empty()) {
    return cached_oid_local_certificate_;
  }

  X509* cert = SSL_get_certificate(ssl());
  if (!cert) {
    ASSERT(cached_oid_local_certificate_.empty());
    return cached_oid_local_certificate_;
  }
  cached_oid_local_certificate_ = Utility::getCertificateExtensionOids(*cert);
  return cached_oid_local_certificate_;
}

uint16_t ConnectionInfoImplBase::ciphersuiteId() const {
  const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl());
  if (cipher == nullptr) {
    return 0xffff;
  }

  // From the OpenSSL docs:
  //    SSL_CIPHER_get_id returns |cipher|'s id. It may be cast to a |uint16_t| to
  //    get the cipher suite value.
  return static_cast<uint16_t>(SSL_CIPHER_get_id(cipher));
}

std::string ConnectionInfoImplBase::ciphersuiteString() const {
  const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl());
  if (cipher == nullptr) {
    return {};
  }

  return SSL_CIPHER_get_name(cipher);
}

const std::string& ConnectionInfoImplBase::tlsVersion() const {
  if (!cached_tls_version_.empty()) {
    return cached_tls_version_;
  }
  cached_tls_version_ = SSL_get_version(ssl());
  return cached_tls_version_;
}

const std::string& ConnectionInfoImplBase::alpn() const {
  if (alpn_.empty()) {
    const unsigned char* proto;
    unsigned int proto_len;
    SSL_get0_alpn_selected(ssl(), &proto, &proto_len);
    if (proto != nullptr) {
      alpn_ = std::string(reinterpret_cast<const char*>(proto), proto_len);
    }
  }
  return alpn_;
}

const std::string& ConnectionInfoImplBase::sni() const {
  if (sni_.empty()) {
    const char* proto = SSL_get_servername(ssl(), TLSEXT_NAMETYPE_host_name);
    if (proto != nullptr) {
      sni_ = std::string(proto);
    }
  }
  return sni_;
}

const std::string& ConnectionInfoImplBase::serialNumberPeerCertificate() const {
  if (!cached_serial_number_peer_certificate_.empty()) {
    return cached_serial_number_peer_certificate_;
  }
  bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl()));
  if (!cert) {
    ASSERT(cached_serial_number_peer_certificate_.empty());
    return cached_serial_number_peer_certificate_;
  }
  cached_serial_number_peer_certificate_ = Utility::getSerialNumberFromCertificate(*cert.get());
  return cached_serial_number_peer_certificate_;
}

absl::Span<const std::string> ConnectionInfoImplBase::serialNumbersPeerCertificates() const {
  if (!cached_serial_numbers_peer_certificates_.empty()) {
    return cached_serial_numbers_peer_certificates_;
  }

  STACK_OF(X509)* cert_chain = SSL_get_peer_full_cert_chain(ssl());
  if (cert_chain == nullptr) {
    ASSERT(cached_serial_numbers_peer_certificates_.empty());
    return cached_serial_numbers_peer_certificates_;
  }

  cached_serial_numbers_peer_certificates_ =
      Utility::mapX509Stack(*cert_chain, [](X509& cert) -> std::string {
        return Utility::getSerialNumberFromCertificate(cert);
      });

  return cached_serial_numbers_peer_certificates_;
}

const std::string& ConnectionInfoImplBase::issuerPeerCertificate() const {
  if (!cached_issuer_peer_certificate_.empty()) {
    return cached_issuer_peer_certificate_;
  }
  bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl()));
  if (!cert) {
    ASSERT(cached_issuer_peer_certificate_.empty());
    return cached_issuer_peer_certificate_;
  }
  cached_issuer_peer_certificate_ = Utility::getIssuerFromCertificate(*cert);
  return cached_issuer_peer_certificate_;
}

const std::string& ConnectionInfoImplBase::subjectPeerCertificate() const {
  if (!cached_subject_peer_certificate_.empty()) {
    return cached_subject_peer_certificate_;
  }
  bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl()));
  if (!cert) {
    ASSERT(cached_subject_peer_certificate_.empty());
    return cached_subject_peer_certificate_;
  }
  cached_subject_peer_certificate_ = Utility::getSubjectFromCertificate(*cert);
  return cached_subject_peer_certificate_;
}

const std::string& ConnectionInfoImplBase::subjectLocalCertificate() const {
  if (!cached_subject_local_certificate_.empty()) {
    return cached_subject_local_certificate_;
  }
  X509* cert = SSL_get_certificate(ssl());
  if (!cert) {
    ASSERT(cached_subject_local_certificate_.empty());
    return cached_subject_local_certificate_;
  }
  cached_subject_local_certificate_ = Utility::getSubjectFromCertificate(*cert);
  return cached_subject_local_certificate_;
}

absl::optional<SystemTime> ConnectionInfoImplBase::validFromPeerCertificate() const {
  bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl()));
  if (!cert) {
    return absl::nullopt;
  }
  return Utility::getValidFrom(*cert);
}

absl::optional<SystemTime> ConnectionInfoImplBase::expirationPeerCertificate() const {
  bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl()));
  if (!cert) {
    return absl::nullopt;
  }
  return Utility::getExpirationTime(*cert);
}

const std::string& ConnectionInfoImplBase::sessionId() const {
  if (!cached_session_id_.empty()) {
    return cached_session_id_;
  }
  SSL_SESSION* session = SSL_get_session(ssl());
  if (session == nullptr) {
    ASSERT(cached_session_id_.empty());
    return cached_session_id_;
  }

  unsigned int session_id_length = 0;
  const uint8_t* session_id = SSL_SESSION_get_id(session, &session_id_length);
  cached_session_id_ = Hex::encode(session_id, session_id_length);
  return cached_session_id_;
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
