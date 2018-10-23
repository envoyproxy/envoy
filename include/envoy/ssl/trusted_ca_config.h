#pragma once

#include <string>
#include <vector>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Ssl {

class TrustedCaConfig {
public:
  virtual ~TrustedCaConfig() {}

  /**
   * @return The CA certificate to use for peer validation.
   */
  virtual const std::string& caCert() const PURE;

  /**
   * @return Path of the CA certificate to use for peer validation or "<inline>"
   * if the CA certificate was inlined.
   */
  virtual const std::string& caCertPath() const PURE;
};

typedef std::unique_ptr<TrustedCaConfig> TrustedCaConfigPtr;

} // namespace Ssl
} // namespace Envoy
