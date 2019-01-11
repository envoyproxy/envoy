#pragma once

#include <string>
#include <vector>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Ssl {

/**
 * Base connection interface for all SSL connections.
 */
class Connection {
public:
  virtual ~Connection() {}

  /**
   * @return bool whether the peer certificate is presented.
   **/
  virtual bool peerCertificatePresented() const PURE;

  /**
   * @return std::string the URI in the SAN feld of the local certificate. Returns "" if there is no
   *         local certificate, or no SAN field, or no URI.
   **/
  virtual std::string uriSanLocalCertificate() const PURE;

  /**
   * @return std::string the subject field of the local certificate in RFC 2253 format. Returns ""
   *         if there is no local certificate, or no subject.
   **/
  virtual std::string subjectLocalCertificate() const PURE;

  /**
   * @return std::string the SHA256 digest of the peer certificate. Returns "" if there is no peer
   *         certificate which can happen in TLS (non mTLS) connections.
   */
  virtual const std::string& sha256PeerCertificateDigest() const PURE;

  /**
   * @return std::string the serial number field of the peer certificate. Returns "" if
   *         there is no peer certificate, or no serial number.
   **/
  virtual std::string serialNumberPeerCertificate() const PURE;

  /**
   * @return std::string the subject field of the peer certificate in RFC 2253 format. Returns "" if
   *         there is no peer certificate, or no subject.
   **/
  virtual std::string subjectPeerCertificate() const PURE;

  /**
   * @return std::string the URI in the SAN field of the peer certificate. Returns "" if there is no
   *         peer certificate, or no SAN field, or no URI.
   **/
  virtual std::string uriSanPeerCertificate() const PURE;

  /**
   * @return std::string the URL-encoded PEM-encoded representation of the peer certificate. Returns
   *         "" if there is no peer certificate or encoding fails.
   **/
  virtual const std::string& urlEncodedPemEncodedPeerCertificate() const PURE;

  /**
   * @return std::vector<std::string> the DNS entries in the SAN field of the peer certificate.
   *         Returns {} if there is no peer certificate, or no SAN field, or no DNS.
   **/
  virtual std::vector<std::string> dnsSansPeerCertificate() const PURE;

  /**
   * @return std::vector<std::string> the DNS entries in the SAN field of the local certificate.
   *         Returns {} if there is no local certificate, or no SAN field, or no DNS.
   **/
  virtual std::vector<std::string> dnsSansLocalCertificate() const PURE;
};

} // namespace Ssl
} // namespace Envoy
