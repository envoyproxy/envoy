#pragma once

#include <string>

#include "envoy/common/pure.h"

namespace Lyft {
namespace Ssl {

/**
 * Base connection interface for all SSL connections.
 */
class Connection {
public:
  virtual ~Connection() {}

  /**
   * @return the SHA256 digest of the peer certificate. Returns "" if there is no peer certificate
   *         which can happen in the case of server side connections.
   */
  virtual std::string sha256PeerCertificateDigest() PURE;

  /**
   * @return the uri in the SAN field of the peer certificate. Returns "" if there is no peer
   *         certificate, or no SAN field, or no uri.
   **/
  virtual std::string uriSanPeerCertificate() PURE;
};

} // Ssl
} // Lyft