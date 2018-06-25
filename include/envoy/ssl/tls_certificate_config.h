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
   * @return a string of private key
   */
  virtual const std::string& privateKey() const PURE;
};

typedef std::unique_ptr<TlsCertificateConfig> TlsCertificateConfigPtr;

} // namespace Ssl
} // namespace Envoy
