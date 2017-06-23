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
   * @return whether the connection is Mutual TLS.
   **/
  virtual bool peerCertificatePresented() PURE;

  /**
   * @return the uri in the SAN feld of the local certificate. Returns "" if there is no local
   *         certificate, or no SAN field, or no uri.
   **/
  virtual std::string uriSanLocalCertificate() PURE;

  /**
   * @return the SHA256 digest of the peer certificate. Returns "" if there is no peer certificate
   *         which can happen in the case of server side connections.
   */
  virtual std::string sha256PeerCertificateDigest() PURE;

  /**
   * @return the subject field of the peer certificate. Returns "" if there is no peer certificate,
   *         or no subject.
   **/
  virtual std::string subjectPeerCertificate() PURE;

  /**
   * @return the uri in the SAN field of the peer certificate. Returns "" if there is no peer
   *         certificate, or no SAN field, or no uri.
   **/
  virtual std::string uriSanPeerCertificate() PURE;
};

} // Ssl
} // Envoy
