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

template <typename ValueType>
const ValueType&
ConnectionInfoImplBase::getCachedValueOrCreate(CachedValueTag tag,
                                               std::function<ValueType(SSL* ssl)> create) const {
  auto it = cached_values_.find(tag);
  if (it != cached_values_.end()) {
    const ValueType* val = absl::get_if<ValueType>(&it->second);
    ASSERT(val != nullptr, "Incorrect type in variant");
    if (val != nullptr) {
      return *val;
    }
  }

  auto [inserted_it, inserted] = cached_values_.emplace(tag, create(ssl()));
  return absl::get<ValueType>(inserted_it->second);
}

bool ConnectionInfoImplBase::peerCertificatePresented() const {
  const STACK_OF(CRYPTO_BUFFER)* cert(SSL_get0_peer_certificates(ssl()));
  return cert != nullptr;
}

absl::Span<const std::string> ConnectionInfoImplBase::uriSanLocalCertificate() const {
  return getCachedValueOrCreate<std::vector<std::string>>(
      CachedValueTag::UriSanLocalCertificate, [](SSL* ssl) {
        // The cert object is not owned.
        X509* cert = SSL_get_certificate(ssl);
        if (!cert) {
          return std::vector<std::string>{};
        }
        return Utility::getSubjectAltNames(*cert, GEN_URI);
      });
}

absl::Span<const std::string> ConnectionInfoImplBase::dnsSansLocalCertificate() const {
  return getCachedValueOrCreate<std::vector<std::string>>(
      CachedValueTag::DnsSansLocalCertificate, [](SSL* ssl) {
        X509* cert = SSL_get_certificate(ssl);
        if (!cert) {
          return std::vector<std::string>{};
        }
        return Utility::getSubjectAltNames(*cert, GEN_DNS);
      });
}

absl::Span<const std::string> ConnectionInfoImplBase::ipSansLocalCertificate() const {
  return getCachedValueOrCreate<std::vector<std::string>>(
      CachedValueTag::IpSansLocalCertificate, [](SSL* ssl) {
        X509* cert = SSL_get_certificate(ssl);
        if (!cert) {
          return std::vector<std::string>{};
        }
        return Utility::getSubjectAltNames(*cert, GEN_IPADD);
      });
}

absl::Span<const std::string> ConnectionInfoImplBase::emailSansLocalCertificate() const {
  return getCachedValueOrCreate<std::vector<std::string>>(
      CachedValueTag::EmailSansLocalCertificate, [](SSL* ssl) {
        X509* cert = SSL_get_certificate(ssl);
        if (!cert) {
          return std::vector<std::string>{};
        }
        return Utility::getSubjectAltNames(*cert, GEN_EMAIL);
      });
}

absl::Span<const std::string> ConnectionInfoImplBase::othernameSansLocalCertificate() const {
  return getCachedValueOrCreate<std::vector<std::string>>(
      CachedValueTag::OthernameSansLocalCertificate, [](SSL* ssl) {
        X509* cert = SSL_get_certificate(ssl);
        if (!cert) {
          return std::vector<std::string>{};
        }
        return Utility::getSubjectAltNames(*cert, GEN_OTHERNAME);
      });
}

const std::string& ConnectionInfoImplBase::sha256PeerCertificateDigest() const {
  return getCachedValueOrCreate<std::string>(
      CachedValueTag::Sha256PeerCertificateDigest, [](SSL* ssl) {
        bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl));
        if (!cert) {
          return std::string{};
        }

        std::vector<uint8_t> computed_hash(SHA256_DIGEST_LENGTH);
        unsigned int n;
        X509_digest(cert.get(), EVP_sha256(), computed_hash.data(), &n);
        RELEASE_ASSERT(n == computed_hash.size(), "");
        return Hex::encode(computed_hash);
      });
}

absl::Span<const std::string> ConnectionInfoImplBase::sha256PeerCertificateChainDigests() const {
  return getCachedValueOrCreate<std::vector<std::string>>(
      CachedValueTag::Sha256PeerCertificateChainDigests, [](SSL* ssl) {
        STACK_OF(X509)* cert_chain = SSL_get_peer_full_cert_chain(ssl);
        if (cert_chain == nullptr) {
          return std::vector<std::string>{};
        }

        return Utility::mapX509Stack(*cert_chain, [](X509& cert) -> std::string {
          std::vector<uint8_t> computed_hash(SHA256_DIGEST_LENGTH);
          unsigned int n;
          X509_digest(&cert, EVP_sha256(), computed_hash.data(), &n);
          RELEASE_ASSERT(n == computed_hash.size(), "");
          return Hex::encode(computed_hash);
        });
      });
}

const std::string& ConnectionInfoImplBase::sha1PeerCertificateDigest() const {
  return getCachedValueOrCreate<std::string>(
      CachedValueTag::Sha1PeerCertificateDigest, [](SSL* ssl) {
        bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl));
        if (!cert) {
          return std::string{};
        }

        std::vector<uint8_t> computed_hash(SHA_DIGEST_LENGTH);
        unsigned int n;
        X509_digest(cert.get(), EVP_sha1(), computed_hash.data(), &n);
        RELEASE_ASSERT(n == computed_hash.size(), "");
        return Hex::encode(computed_hash);
      });
}

absl::Span<const std::string> ConnectionInfoImplBase::sha1PeerCertificateChainDigests() const {
  return getCachedValueOrCreate<std::vector<std::string>>(
      CachedValueTag::Sha1PeerCertificateChainDigests, [](SSL* ssl) {
        STACK_OF(X509)* cert_chain = SSL_get_peer_full_cert_chain(ssl);
        if (cert_chain == nullptr) {
          return std::vector<std::string>{};
        }

        return Utility::mapX509Stack(*cert_chain, [](X509& cert) -> std::string {
          std::vector<uint8_t> computed_hash(SHA_DIGEST_LENGTH);
          unsigned int n;
          X509_digest(&cert, EVP_sha1(), computed_hash.data(), &n);
          RELEASE_ASSERT(n == computed_hash.size(), "");
          return Hex::encode(computed_hash);
        });
      });
}

const std::string& ConnectionInfoImplBase::urlEncodedPemEncodedPeerCertificate() const {
  return getCachedValueOrCreate<std::string>(
      CachedValueTag::UrlEncodedPemEncodedPeerCertificate, [](SSL* ssl) {
        bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl));
        if (!cert) {
          return std::string{};
        }

        bssl::UniquePtr<BIO> buf(BIO_new(BIO_s_mem()));
        RELEASE_ASSERT(buf != nullptr, "");
        RELEASE_ASSERT(PEM_write_bio_X509(buf.get(), cert.get()) == 1, "");
        const uint8_t* output;
        size_t length;
        RELEASE_ASSERT(BIO_mem_contents(buf.get(), &output, &length) == 1, "");
        absl::string_view pem(reinterpret_cast<const char*>(output), length);
        return absl::StrReplaceAll(
            pem, {{"\n", "%0A"}, {" ", "%20"}, {"+", "%2B"}, {"/", "%2F"}, {"=", "%3D"}});
      });
}

const std::string& ConnectionInfoImplBase::urlEncodedPemEncodedPeerCertificateChain() const {
  return getCachedValueOrCreate<std::string>(
      CachedValueTag::UrlEncodedPemEncodedPeerCertificateChain, [](SSL* ssl) {
        STACK_OF(X509)* cert_chain = SSL_get_peer_full_cert_chain(ssl);
        if (cert_chain == nullptr) {
          return std::string{};
        }

        std::string result;
        for (uint64_t i = 0; i < sk_X509_num(cert_chain); i++) {
          X509* cert = sk_X509_value(cert_chain, i);

          bssl::UniquePtr<BIO> buf(BIO_new(BIO_s_mem()));
          RELEASE_ASSERT(buf != nullptr, "");
          RELEASE_ASSERT(PEM_write_bio_X509(buf.get(), cert) == 1, "");
          const uint8_t* output;
          size_t length;
          RELEASE_ASSERT(BIO_mem_contents(buf.get(), &output, &length) == 1, "");

          absl::string_view pem(reinterpret_cast<const char*>(output), length);
          absl::StrAppend(
              &result,
              absl::StrReplaceAll(
                  pem, {{"\n", "%0A"}, {" ", "%20"}, {"+", "%2B"}, {"/", "%2F"}, {"=", "%3D"}}));
        }
        return result;
      });
}

absl::Span<const std::string> ConnectionInfoImplBase::uriSanPeerCertificate() const {
  return getCachedValueOrCreate<std::vector<std::string>>(
      CachedValueTag::UriSanPeerCertificate, [](SSL* ssl) {
        bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl));
        if (!cert) {
          return std::vector<std::string>{};
        }
        return Utility::getSubjectAltNames(*cert, GEN_URI);
      });
}

absl::Span<const std::string> ConnectionInfoImplBase::dnsSansPeerCertificate() const {
  return getCachedValueOrCreate<std::vector<std::string>>(
      CachedValueTag::DnsSansPeerCertificate, [](SSL* ssl) {
        bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl));
        if (!cert) {
          return std::vector<std::string>{};
        }
        return Utility::getSubjectAltNames(*cert, GEN_DNS);
      });
}

absl::Span<const std::string> ConnectionInfoImplBase::ipSansPeerCertificate() const {
  return getCachedValueOrCreate<std::vector<std::string>>(
      CachedValueTag::IpSansPeerCertificate, [](SSL* ssl) {
        bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl));
        if (!cert) {
          return std::vector<std::string>{};
        }
        return Utility::getSubjectAltNames(*cert, GEN_IPADD, true);
      });
}

absl::Span<const std::string> ConnectionInfoImplBase::emailSansPeerCertificate() const {
  return getCachedValueOrCreate<std::vector<std::string>>(
      CachedValueTag::EmailSansPeerCertificate, [](SSL* ssl) {
        bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl));
        if (!cert) {
          return std::vector<std::string>{};
        }
        return Utility::getSubjectAltNames(*cert, GEN_EMAIL);
      });
}

absl::Span<const std::string> ConnectionInfoImplBase::othernameSansPeerCertificate() const {
  return getCachedValueOrCreate<std::vector<std::string>>(
      CachedValueTag::OthernameSansPeerCertificate, [](SSL* ssl) {
        bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl));
        if (!cert) {
          return std::vector<std::string>{};
        }
        return Utility::getSubjectAltNames(*cert, GEN_OTHERNAME);
      });
}

absl::Span<const std::string> ConnectionInfoImplBase::oidsPeerCertificate() const {
  return getCachedValueOrCreate<std::vector<std::string>>(
      CachedValueTag::OidsPeerCertificate, [](SSL* ssl) {
        bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl));
        if (!cert) {
          return std::vector<std::string>{};
        }
        return Utility::getCertificateExtensionOids(*cert);
      });
}

absl::Span<const std::string> ConnectionInfoImplBase::oidsLocalCertificate() const {
  return getCachedValueOrCreate<std::vector<std::string>>(
      CachedValueTag::OidsLocalCertificate, [](SSL* ssl) {
        X509* cert = SSL_get_certificate(ssl);
        if (!cert) {
          return std::vector<std::string>{};
        }
        return Utility::getCertificateExtensionOids(*cert);
      });
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
  return getCachedValueOrCreate<std::string>(
      CachedValueTag::TlsVersion, [](SSL* ssl) { return std::string(SSL_get_version(ssl)); });
}

const std::string& ConnectionInfoImplBase::alpn() const {
  return getCachedValueOrCreate<std::string>(CachedValueTag::Alpn, [](SSL* ssl) {
    const unsigned char* proto;
    unsigned int proto_len;
    SSL_get0_alpn_selected(ssl, &proto, &proto_len);
    if (proto != nullptr) {
      return std::string(reinterpret_cast<const char*>(proto), proto_len);
    }
    return std::string{};
  });
}

const std::string& ConnectionInfoImplBase::sni() const {
  return getCachedValueOrCreate<std::string>(CachedValueTag::Sni, [](SSL* ssl) {
    const char* proto = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
    if (proto != nullptr) {
      return std::string(proto);
    }
    return std::string{};
  });
}

const std::string& ConnectionInfoImplBase::serialNumberPeerCertificate() const {
  return getCachedValueOrCreate<std::string>(
      CachedValueTag::SerialNumberPeerCertificate, [](SSL* ssl) {
        bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl));
        if (!cert) {
          return std::string{};
        }
        return Utility::getSerialNumberFromCertificate(*cert.get());
      });
}

absl::Span<const std::string> ConnectionInfoImplBase::serialNumbersPeerCertificates() const {
  return getCachedValueOrCreate<std::vector<std::string>>(
      CachedValueTag::SerialNumbersPeerCertificates, [](SSL* ssl) {
        STACK_OF(X509)* cert_chain = SSL_get_peer_full_cert_chain(ssl);
        if (cert_chain == nullptr) {
          return std::vector<std::string>{};
        }

        return Utility::mapX509Stack(*cert_chain, [](X509& cert) -> std::string {
          return Utility::getSerialNumberFromCertificate(cert);
        });
      });
}

const std::string& ConnectionInfoImplBase::issuerPeerCertificate() const {
  return getCachedValueOrCreate<std::string>(CachedValueTag::IssuerPeerCertificate, [](SSL* ssl) {
    bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl));
    if (!cert) {
      return std::string{};
    }
    return Utility::getIssuerFromCertificate(*cert);
  });
}

const std::string& ConnectionInfoImplBase::subjectPeerCertificate() const {
  return getCachedValueOrCreate<std::string>(CachedValueTag::SubjectPeerCertificate, [](SSL* ssl) {
    bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl));
    if (!cert) {
      return std::string{};
    }
    return Utility::getSubjectFromCertificate(*cert);
  });
}

const std::string& ConnectionInfoImplBase::subjectLocalCertificate() const {
  return getCachedValueOrCreate<std::string>(CachedValueTag::SubjectLocalCertificate, [](SSL* ssl) {
    X509* cert = SSL_get_certificate(ssl);
    if (!cert) {
      return std::string{};
    }
    return Utility::getSubjectFromCertificate(*cert);
  });
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
  return getCachedValueOrCreate<std::string>(CachedValueTag::SessionId, [](SSL* ssl) {
    SSL_SESSION* session = SSL_get_session(ssl);
    if (session == nullptr) {
      return std::string{};
    }

    unsigned int session_id_length = 0;
    const uint8_t* session_id = SSL_SESSION_get_id(session, &session_id_length);
    return Hex::encode(session_id, session_id_length);
  });
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
