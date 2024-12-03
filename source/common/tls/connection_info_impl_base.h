#pragma once

#include <string>

#include "envoy/ssl/connection.h"

#include "source/common/common/logger.h"
#include "source/common/tls/utility.h"

#include "absl/types/optional.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

// An implementation wraps struct SSL in BoringSSL.
class ConnectionInfoImplBase : public Ssl::ConnectionInfo {
public:
  // Ssl::ConnectionInfo
  bool peerCertificatePresented() const override;
  const std::string& sha256PeerCertificateDigest() const override;
  absl::Span<const std::string> sha256PeerCertificateChainDigests() const override;
  const std::string& sha1PeerCertificateDigest() const override;
  absl::Span<const std::string> sha1PeerCertificateChainDigests() const override;
  const std::string& serialNumberPeerCertificate() const override;
  absl::Span<const std::string> serialNumbersPeerCertificates() const override;
  const std::string& issuerPeerCertificate() const override;
  const std::string& subjectPeerCertificate() const override;
  Ssl::ParsedX509NameOptConstRef parsedSubjectPeerCertificate() const override;
  const std::string& subjectLocalCertificate() const override;
  const std::string& urlEncodedPemEncodedPeerCertificate() const override;
  const std::string& urlEncodedPemEncodedPeerCertificateChain() const override;
  absl::Span<const std::string> uriSanPeerCertificate() const override;
  absl::Span<const std::string> uriSanLocalCertificate() const override;
  absl::Span<const std::string> dnsSansPeerCertificate() const override;
  absl::Span<const std::string> dnsSansLocalCertificate() const override;
  absl::Span<const std::string> ipSansPeerCertificate() const override;
  absl::Span<const std::string> ipSansLocalCertificate() const override;
  absl::Span<const std::string> emailSansPeerCertificate() const override;
  absl::Span<const std::string> emailSansLocalCertificate() const override;
  absl::Span<const std::string> othernameSansPeerCertificate() const override;
  absl::Span<const std::string> othernameSansLocalCertificate() const override;
  absl::Span<const std::string> oidsPeerCertificate() const override;
  absl::Span<const std::string> oidsLocalCertificate() const override;
  absl::optional<SystemTime> validFromPeerCertificate() const override;
  absl::optional<SystemTime> expirationPeerCertificate() const override;
  const std::string& sessionId() const override;
  uint16_t ciphersuiteId() const override;
  std::string ciphersuiteString() const override;
  const std::string& tlsVersion() const override;
  const std::string& alpn() const override;
  const std::string& sni() const override;

  virtual SSL* ssl() const PURE;

private:
  // Enum values should be the name of the calling function, but capitalized.
  enum class CachedValueTag : uint8_t {
    Alpn,
    SessionId,
    Sni,
    TlsVersion,
    UriSanLocalCertificate,
    DnsSansLocalCertificate,
    IpSansLocalCertificate,
    Sha256PeerCertificateDigest,
    Sha256PeerCertificateChainDigests,
    Sha1PeerCertificateDigest,
    Sha1PeerCertificateChainDigests,
    SerialNumberPeerCertificate,
    SerialNumbersPeerCertificates,
    IssuerPeerCertificate,
    SubjectPeerCertificate,
    ParsedSubjectPeerCertificate,
    SubjectLocalCertificate,
    EmailSansLocalCertificate,
    OthernameSansLocalCertificate,
    UriSanPeerCertificate,
    EmailSansPeerCertificate,
    OthernameSansPeerCertificate,
    UrlEncodedPemEncodedPeerCertificate,
    UrlEncodedPemEncodedPeerCertificateChain,
    DnsSansPeerCertificate,
    IpSansPeerCertificate,
    OidsPeerCertificate,
    OidsLocalCertificate,
  };

  // Retrieve the given tag from the set of cached values, or create the value via the supplied
  // create function and cache it. The returned reference is valid for the lifetime of this object.
  template <typename ValueType>
  const ValueType& getCachedValueOrCreate(CachedValueTag tag,
                                          std::function<ValueType(SSL* ssl)> create) const;

  // For any given instance of this class, most of the accessors are never called, so
  // having fixed space for cached data that isn't used is a waste. Instead, create a lookup
  // table of cached values that are created on demand. Use a node_hash_map so that returned
  // references are not invalidated when additional items are added.
  using CachedValue = absl::variant<std::string, std::vector<std::string>, Ssl::ParsedX509NamePtr>;
  mutable absl::node_hash_map<CachedValueTag, CachedValue> cached_values_;
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
