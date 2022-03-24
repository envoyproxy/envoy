#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Ssl {

class TlsRootCACertificateConfig {
public:
  virtual ~TlsRootCACertificateConfig() = default;

  /**
   * @return a string of certificate chain.
   */
  virtual const std::string& cert() const PURE;

  /**
   * @return path of the certificate chain used to identify the local side or "<inline>" if the
   * certificate chain was inlined.
   */
  virtual const std::string& certPath() const PURE;

  /**
   * @return a string of private key.
   */
  virtual const std::string& privateKey() const PURE;

  /**
   * @return path of the private key used to identify the local side or "<inline>" if the private
   * key was inlined.
   */
  virtual const std::string& privateKeyPath() const PURE;
};

using TlsRootCACertificateConfigPtr = std::unique_ptr<TlsRootCACertificateConfig>;

} // namespace Ssl
} // namespace Envoy
