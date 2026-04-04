#include "source/common/tls/connection_info_impl_base.h"

#include <openssl/stack.h>

#include "source/common/common/hex.h"
#include "source/common/http/utility.h"
#include "source/common/tls/cert_validator/san_matcher.h"

#include "absl/strings/str_replace.h"
#include "openssl/safestack.h"
#include "openssl/x509v3.h"
#include "utility.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

namespace {
// There must be an version of this function for each type possible in variant `CachedValue`.
bool shouldRecalculateCachedEntry(const std::string& str) { return str.empty(); }
bool shouldRecalculateCachedEntry(const std::vector<std::string>& vec) { return vec.empty(); }
bool shouldRecalculateCachedEntry(const Ssl::ParsedX509NamePtr& ptr) { return ptr == nullptr; }
bool shouldRecalculateCachedEntry(const bssl::UniquePtr<GENERAL_NAMES>& ptr) {
  return ptr == nullptr;
}

// Convert a single X509 certificate to a PEM-encoded string.
std::string certToPem(X509& cert) {
  bssl::UniquePtr<BIO> buf(BIO_new(BIO_s_mem()));
  RELEASE_ASSERT(buf != nullptr, "");
  RELEASE_ASSERT(PEM_write_bio_X509(buf.get(), &cert) == 1, "");
  const uint8_t* output;
  size_t length;
  RELEASE_ASSERT(BIO_mem_contents(buf.get(), &output, &length) == 1, "");
  return std::string(reinterpret_cast<const char*>(output), length);
}

// Iterate the peer certificate chain, converting each certificate to PEM and calling the provided
// callback. Does nothing if the chain is not available.
void forEachPeerCertPem(SSL* ssl, std::function<void(const std::string&)> cb) {
  STACK_OF(X509)* cert_chain = SSL_get_peer_full_cert_chain(ssl);
  if (cert_chain == nullptr) {
    return;
  }
  for (uint64_t i = 0; i < sk_X509_num(cert_chain); i++) {
    cb(certToPem(*sk_X509_value(cert_chain, i)));
  }
}
} // namespace

template <typename ValueType>
const ValueType&
ConnectionInfoImplBase::getCachedValueOrCreate(CachedValueTag tag,
                                               std::function<ValueType(SSL* ssl)> create) const {
  auto it = cached_values_.find(tag);
  if (it != cached_values_.end()) {
    const ValueType* val = absl::get_if<ValueType>(&it->second);
    ASSERT(val != nullptr, "Incorrect type in variant");
    if (val != nullptr) {

      // Some values are retrieved too early, for example if properties of a peer certificate are
      // retrieved before the handshake is complete, an empty value is cached. The value must be
      // in the cache, so that we can return a valid reference, but in those cases if another caller
      // later retrieves the same value, we must recalculate the value.
      if (shouldRecalculateCachedEntry(*val)) {
        it->second = create(ssl());
        val = &absl::get<ValueType>(it->second);
      }

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
        return Utility::getSha256DigestFromCertificate(*cert);
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
          return Utility::getSha256DigestFromCertificate(cert);
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
        return Utility::getSha1DigestFromCertificate(*cert);
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
          return Utility::getSha1DigestFromCertificate(cert);
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
        return Envoy::Http::Utility::PercentEncoding::urlEncode(certToPem(*cert));
      });
}

const std::string& ConnectionInfoImplBase::pemEncodedPeerCertificate() const {
  return getCachedValueOrCreate<std::string>(
      CachedValueTag::PemEncodedPeerCertificate, [](SSL* ssl) {
        bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl));
        if (!cert) {
          return std::string{};
        }
        return certToPem(*cert);
      });
}

const std::string& ConnectionInfoImplBase::urlEncodedPemEncodedPeerCertificateChain() const {
  return getCachedValueOrCreate<std::string>(
      CachedValueTag::UrlEncodedPemEncodedPeerCertificateChain, [](SSL* ssl) {
        std::string result;
        forEachPeerCertPem(ssl, [&result](const std::string& pem) {
          absl::StrAppend(&result, Envoy::Http::Utility::PercentEncoding::urlEncode(pem));
        });
        return result;
      });
}

absl::Span<const std::string> ConnectionInfoImplBase::pemEncodedPeerCertificateChain() const {
  return getCachedValueOrCreate<std::vector<std::string>>(
      CachedValueTag::PemEncodedPeerCertificateChain, [](SSL* ssl) {
        std::vector<std::string> result;
        forEachPeerCertPem(ssl, [&result](const std::string& pem) { result.emplace_back(pem); });
        return result;
      });
}

bool ConnectionInfoImplBase::peerCertificateSanMatches(const Ssl::SanMatcher& matcher) const {
  const bssl::UniquePtr<GENERAL_NAMES>& sans =
      getCachedValueOrCreate<bssl::UniquePtr<GENERAL_NAMES>>(
          CachedValueTag::PeerCertificateSanMatches,
          [](SSL* ssl) -> bssl::UniquePtr<GENERAL_NAMES> {
            bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl));
            if (!cert) {
              return nullptr;
            }
            return bssl::UniquePtr<GENERAL_NAMES>(static_cast<GENERAL_NAMES*>(
                X509_get_ext_d2i(cert.get(), NID_subject_alt_name, nullptr, nullptr)));
          });

  if (sans != nullptr) {
    for (const GENERAL_NAME* san : sans.get()) {
      if (matcher.match(san)) {
        return true;
      }
    }
  }

  return false;
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
        return Utility::getSubjectAltNames(*cert, GEN_IPADD);
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

const std::string& ConnectionInfoImplBase::sha256PeerCertificateIssuerDigest() const {
  return getCachedValueOrCreate<std::string>(
      CachedValueTag::Sha256PeerCertificateIssuerDigest, [this](SSL*) -> std::string {
        X509* issuer = validatedPeerIssuer();
        if (!issuer) {
          return std::string{};
        }
        return Utility::getSha256DigestFromCertificate(*issuer);
      });
}

const std::string& ConnectionInfoImplBase::serialNumberPeerCertificateIssuer() const {
  return getCachedValueOrCreate<std::string>(
      CachedValueTag::SerialNumberPeerCertificateIssuer, [this](SSL*) -> std::string {
        X509* issuer = validatedPeerIssuer();
        if (!issuer) {
          return std::string{};
        }
        return Utility::getSerialNumberFromCertificate(*issuer);
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

Ssl::ParsedX509NameOptConstRef ConnectionInfoImplBase::parsedSubjectPeerCertificate() const {
  const auto& parsedName = getCachedValueOrCreate<Ssl::ParsedX509NamePtr>(
      CachedValueTag::ParsedSubjectPeerCertificate, [](SSL* ssl) {
        bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl));
        if (!cert) {
          return Ssl::ParsedX509NamePtr();
        }
        return Utility::parseSubjectFromCertificate(*cert);
      });

  if (parsedName) {
    return {*parsedName};
  }
  return absl::nullopt;
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
