#pragma once

#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/common/time.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"

namespace Envoy {
namespace Ssl {

/**
 * Base connection interface for all SSL connections.
 */
class ConnectionInfo {
public:
  virtual ~ConnectionInfo() = default;

  /**
   * @return bool whether the peer certificate is presented.
   **/
  virtual bool peerCertificatePresented() const PURE;

  /**
   * @return absl::Span<const std::string> the URIs in the SAN field of the local certificate. Returns {} if
   *there is no local certificate, or no SAN field, or no URI.
   **/
  virtual absl::Span<const std::string> uriSanLocalCertificate() const PURE;

  /**
   * @return absl::string_view the subject field of the local certificate in RFC 2253 format.
   *Returns "" if there is no local certificate, or no subject.
   **/
  virtual absl::string_view subjectLocalCertificate() const PURE;

  /**
   * @return absl::string_view the SHA256 digest of the peer certificate. Returns "" if there is no
   * peer certificate which can happen in TLS (non mTLS) connections.
   */
  virtual absl::string_view sha256PeerCertificateDigest() const PURE;

  /**
   * @return absl::string_view the serial number field of the peer certificate. Returns "" if
   *         there is no peer certificate, or no serial number.
   **/
  virtual absl::string_view serialNumberPeerCertificate() const PURE;

  /**
   * @return absl::string_view the issuer field of the peer certificate in RFC 2253 format. Returns
   *"" if there is no peer certificate, or no issuer.
   **/
  virtual absl::string_view issuerPeerCertificate() const PURE;

  /**
   * @return absl::string_view the subject field of the peer certificate in RFC 2253 format. Returns
   *"" if there is no peer certificate, or no subject.
   **/
  virtual absl::string_view subjectPeerCertificate() const PURE;

  /**
   * @return absl::Span<const std::string> the URIs in the SAN field of the peer certificate. Returns {} if
   *there is no peer certificate, or no SAN field, or no URI.
   **/
  virtual absl::Span<const std::string> uriSanPeerCertificate() const PURE;

  /**
   * @return absl::string_view the URL-encoded PEM-encoded representation of the peer certificate.
   *Returns
   *         "" if there is no peer certificate or encoding fails.
   **/
  virtual absl::string_view urlEncodedPemEncodedPeerCertificate() const PURE;

  /**
   * @return absl::string_view the URL-encoded PEM-encoded representation of the full peer
   *certificate chain including the leaf certificate. Returns "" if there is no peer certificate or
   *         encoding fails.
   **/
  virtual absl::string_view urlEncodedPemEncodedPeerCertificateChain() const PURE;

  /**
   * @return absl::Span<const std::string> the DNS entries in the SAN field of the peer
   *certificate. Returns {} if there is no peer certificate, or no SAN field, or no DNS.
   **/
  virtual absl::Span<const std::string> dnsSansPeerCertificate() const PURE;

  /**
   * @return absl::Span<const std::string> the DNS entries in the SAN field of the local
   *certificate. Returns {} if there is no local certificate, or no SAN field, or no DNS.
   **/
  virtual absl::Span<const std::string> dnsSansLocalCertificate() const PURE;

  /**
   * @return absl::optional<SystemTime> the time that the peer certificate was issued and should be
   *         considered valid from. Returns empty absl::optional if there is no peer certificate.
   **/
  virtual absl::optional<SystemTime> validFromPeerCertificate() const PURE;

  /**
   * @return absl::optional<SystemTime> the time that the peer certificate expires and should not be
   *         considered valid after. Returns empty absl::optional if there is no peer certificate.
   **/
  virtual absl::optional<SystemTime> expirationPeerCertificate() const PURE;

  /**
   * @return absl::string_view the hex-encoded TLS session ID as defined in rfc5246.
   **/
  virtual absl::string_view sessionId() const PURE;

  /**
   * @return uint16_t the standard ID for the ciphers used in the established TLS connection.
   *         Returns 0xffff if there is no current negotiated ciphersuite.
   **/
  virtual uint16_t ciphersuiteId() const PURE;

  /**
   * @return absl::string_view the OpenSSL name for the set of ciphers used in the established TLS
   *         connection. Returns "" if there is no current negotiated ciphersuite.
   **/
  virtual absl::string_view ciphersuiteString() const PURE;

  /**
   * @return absl::string_view the TLS version (e.g., TLSv1.2, TLSv1.3) used in the established TLS
   *         connection.
   **/
  virtual absl::string_view tlsVersion() const PURE;

  /**
   * @return absl::string_view The server name used in the established TLS connection.
   */
  virtual absl::string_view serverName() const PURE;
};

using ConnectionInfoConstSharedPtr = std::shared_ptr<const ConnectionInfo>;

} // namespace Ssl
} // namespace Envoy
