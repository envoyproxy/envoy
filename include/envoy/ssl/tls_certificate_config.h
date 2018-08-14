#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Ssl {

class TlsCertificateConfig {
public:
  virtual ~TlsCertificateConfig() {}

  /**
   * @return a string of certificate chain
   */
  virtual const std::string& certificateChain() const PURE;

  /**
   * @return Path of the certificate chain used to identify the local side or "<inline>"
   * if the certificate chain was inlined.
   */
  virtual const std::string& certificateChainPath() const PURE;

  /**
   * @return a string of private key
   */
  virtual const std::string& privateKey() const PURE;

  /**
   * @return Path of the private key used to identify the local side or "<inline>"
   * if the private key was inlined.
   */
  virtual const std::string& privateKeyPath() const PURE;
};

typedef std::unique_ptr<TlsCertificateConfig> TlsCertificateConfigPtr;

} // namespace Ssl
} // namespace Envoy
