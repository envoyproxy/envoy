#pragma once

#include <string>

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
   * @return whether the peer certificate is presented.
   **/
  virtual bool peerCertificatePresented() const PURE;

  /**
   * @return the URI in the SAN feld of the local certificate. Returns "" if there is no local
   *         certificate, or no SAN field, or no URI.
   **/
  virtual std::string uriSanLocalCertificate() PURE;

  /**
   * @return the subject field of the local certificate in RFC 2253 format. Returns "" if there is
   *         no local certificate, or no subject.
   **/
  virtual std::string subjectLocalCertificate() const PURE;

  /**
   * @return the SHA256 digest of the peer certificate. Returns "" if there is no peer certificate
   *         which can happen in TLS (non mTLS) connections.
   */
  virtual std::string sha256PeerCertificateDigest() PURE;

  /**
   * @return the subject field of the peer certificate in RFC 2253 format. Returns "" if there is
   *         no peer certificate, or no subject.
   **/
  virtual std::string subjectPeerCertificate() const PURE;

  /**
   * @return the URI in the SAN field of the peer certificate. Returns "" if there is no peer
   *         certificate, or no SAN field, or no URI.
   **/
  virtual std::string uriSanPeerCertificate() PURE;
};

} // namespace Ssl
} // namespace Envoy
