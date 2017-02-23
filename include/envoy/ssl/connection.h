#pragma once

#include "envoy/common/pure.h"

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
   * @return the subject_alt_name.uri field of the peer certificate. Returns "" if there
   *         is no peer certificate or the uri field is empty.
   */
  virtual std::string uriPeerCertificateSAN();
};

} // Ssl
