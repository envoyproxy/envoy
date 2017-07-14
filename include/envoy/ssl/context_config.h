#pragma once

#include <string>
#include <vector>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Ssl {

/**
 * Supplies the configuration for an SSL context.
 */
class ContextConfig {
public:
  virtual ~ContextConfig() {}

  /**
   * The list of supported protocols exposed via ALPN. Client connections will send these
   * protocols to the server. Server connections will use these protocols to select the next
   * protocol if the client supports ALPN.
   */
  virtual const std::string& alpnProtocols() const PURE;

  /**
   * The alternate list of ALPN protocols served via kill switch. @see alpnProtocols().
   */
  virtual const std::string& altAlpnProtocols() const PURE;

  /**
   * The ':' delimited list of supported cipher suites
   */
  virtual const std::string& cipherSuites() const PURE;

  /**
   * The ':' delimited list of supported ECDH curves.
   */
  virtual const std::string& ecdhCurves() const PURE;

  /**
   * @return The CA certificate file to use for peer validation.
   */
  virtual const std::string& caCertFile() const PURE;

  /**
   * @return The certificate chain file used to identify the local side.
   */
  virtual const std::string& certChainFile() const PURE;

  /**
   * @return The private key chain file used to identify the local side.
   */
  virtual const std::string& privateKeyFile() const PURE;

  /**
   * @return The subject alt names to be verified, if enabled. Otherwise, ""
   */
  virtual const std::vector<std::string>& verifySubjectAltNameList() const PURE;

  /**
   * @return The hex string representation of the certificate hash to be verified, if enabled.
   * Otherwise, ""
   */
  virtual const std::string& verifyCertificateHash() const PURE;

  /**
   * @return The server name indication if it's set and ssl enabled
   * Otherwise, ""
   */
  virtual const std::string& serverNameIndication() const PURE;
};

class ServerContextConfig : public virtual ContextConfig {
public:
  /**
   * @return True if client certificate is required, false otherwise.
   */
  virtual bool requireClientCertificate() const PURE;
};

} // namespace Ssl
} // namespace Envoy
